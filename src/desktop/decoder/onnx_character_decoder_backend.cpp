#include "decoder/onnx_character_decoder_backend.hpp"

#if !defined(CWA_HAVE_ONNX_RUNTIME) || !CWA_HAVE_ONNX_RUNTIME
#error "The ONNX character backend must only be built with ONNX Runtime"
#endif

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cwassistant::desktop {
namespace {

constexpr qint64 kMaximumMetadataBytes = 64 * 1'024;
constexpr qint64 kMaximumModelBytes = 64 * 1'024 * 1'024;
constexpr std::size_t kFrequencyBins = 65;
constexpr std::size_t kMaximumClasses = 128;
constexpr std::size_t kMaximumDiagnosticBytes = 512;

struct ModelMetadata {
  std::vector<char> characters;
  std::size_t blank_index{0};
  std::string input_name;
  std::string output_name;
};

class ModelContractError final : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::string boundedMessage(std::string message) {
  if (message.size() > kMaximumDiagnosticBytes)
    message.resize(kMaximumDiagnosticBytes);
  return message;
}

bool exactNumber(const QJsonObject& object, const char* name,
                 const double expected) {
  const auto value = object.value(QLatin1StringView(name));
  return value.isDouble() && std::isfinite(value.toDouble()) &&
         std::abs(value.toDouble() - expected) <= 1e-9;
}

bool exactString(const QJsonObject& object, const char* name,
                 const char* expected) {
  const auto value = object.value(QLatin1StringView(name));
  return value.isString() && value.toString() == QLatin1StringView(expected);
}

bool exactLayout(const QJsonObject& object, const char* name,
                 const std::initializer_list<const char*> expected) {
  const auto value = object.value(QLatin1StringView(name));
  if (!value.isArray()) return false;
  const auto array = value.toArray();
  if (array.size() != static_cast<qsizetype>(expected.size())) return false;
  qsizetype index = 0;
  for (const auto* part : expected) {
    if (!array[index].isString() ||
        array[index].toString() != QLatin1StringView(part)) {
      return false;
    }
    ++index;
  }
  return true;
}

bool readMetadata(const std::string& path, ModelMetadata& metadata,
                  std::string& error) {
  const QString file_path = QString::fromUtf8(path);
  const QFileInfo info(file_path);
  if (!info.isFile() || info.size() <= 0 ||
      info.size() > kMaximumMetadataBytes) {
    error = "metadata must be a regular JSON file no larger than 64 KiB";
    return false;
  }
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = "metadata file could not be opened";
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    error = "metadata is not a valid JSON object";
    return false;
  }
  const auto object = document.object();
  const bool fixed_contract =
      exactNumber(object, "sample_rate", 3'200.0) &&
      exactNumber(object, "fft_length", 256.0) &&
      exactNumber(object, "hop_length", 48.0) &&
      exactNumber(object, "spectrogram_min_freq_hz", 400.0) &&
      exactNumber(object, "spectrogram_max_freq_hz", 1'200.0) &&
      exactNumber(object, "spectrogram_frequency_bins", 65.0) &&
      exactNumber(object, "channel_count", 1.0) &&
      exactString(object, "normalization", "log1p") &&
      exactString(object, "onnx_input_dtype", "float32") &&
      exactString(object, "onnx_output_dtype", "float32") &&
      exactLayout(object, "onnx_input_layout",
                  {"batch", "channel", "time", "frequency"}) &&
      exactLayout(object, "onnx_output_layout", {"batch", "time", "class"});
  if (!fixed_contract) {
    error = "metadata does not match the supported feature/tensor contract";
    return false;
  }

  const auto input_name = object.value(QStringLiteral("onnx_input_name"));
  const auto output_name = object.value(QStringLiteral("onnx_output_name"));
  if (!input_name.isString() || input_name.toString().isEmpty() ||
      input_name.toString().size() > 128 || !output_name.isString() ||
      output_name.toString().isEmpty() || output_name.toString().size() > 128) {
    error = "metadata tensor names are missing or invalid";
    return false;
  }
  metadata.input_name = input_name.toString().toStdString();
  metadata.output_name = output_name.toString().toStdString();

  const auto characters = object.value(QStringLiteral("chars"));
  const auto blank = object.value(QStringLiteral("blank_index"));
  const auto class_count = object.value(QStringLiteral("num_classes"));
  if (!characters.isArray() || !blank.isDouble() ||
      !class_count.isDouble()) {
    error = "metadata character vocabulary is missing";
    return false;
  }
  const auto character_array = characters.toArray();
  const double blank_value = blank.toDouble();
  const double class_value = class_count.toDouble();
  if (character_array.isEmpty() ||
      character_array.size() + 1 > static_cast<qsizetype>(kMaximumClasses) ||
      !std::isfinite(blank_value) || std::floor(blank_value) != blank_value ||
      blank_value < 0.0 || blank_value > character_array.size() ||
      !std::isfinite(class_value) || std::floor(class_value) != class_value ||
      class_value != character_array.size() + 1) {
    error = "metadata character vocabulary dimensions are invalid";
    return false;
  }
  metadata.blank_index = static_cast<std::size_t>(blank_value);
  metadata.characters.clear();
  metadata.characters.reserve(static_cast<std::size_t>(character_array.size()));
  std::array<bool, 128> seen{};
  for (const auto value : character_array) {
    if (!value.isString()) {
      error = "metadata characters must be single printable ASCII symbols";
      return false;
    }
    const auto encoded = value.toString().toLatin1();
    if (encoded.size() != 1 || encoded[0] < 0x20 || encoded[0] > 0x7e) {
      error = "metadata characters must be single printable ASCII symbols";
      return false;
    }
    const auto symbol = static_cast<unsigned char>(encoded[0]);
    if (seen[symbol]) {
      error = "metadata characters must be unique";
      return false;
    }
    seen[symbol] = true;
    metadata.characters.push_back(static_cast<char>(symbol));
  }
  return true;
}

bool compatibleDimension(const std::int64_t actual,
                         const std::int64_t expected) noexcept {
  return actual < 0 || actual == expected;
}

}  // namespace

struct CwOnnxCharacterDecoderBackend::Impl {
  explicit Impl(CwCharacterBackendConfig supplied_config)
      : config(std::move(supplied_config)),
        environment(ORT_LOGGING_LEVEL_WARNING, "cw-character-decoder") {
    load();
  }

  void setStatus(const CwCharacterBackendStatus status, std::string message,
                 const bool failure = false) noexcept {
    diagnostics.status = status;
    diagnostics.message = boundedMessage(std::move(message));
    if (failure) ++diagnostics.failure_count;
  }

  void load() noexcept {
    try {
      if (config.maximum_input_frames == 0 ||
          config.maximum_input_frames > 4'096 ||
          config.maximum_output_steps == 0 ||
          config.maximum_output_steps > 8'192 ||
          config.maximum_characters == 0 ||
          config.maximum_characters > 1'024 ||
          config.edge_context_frames >= config.maximum_input_frames / 2U) {
        setStatus(CwCharacterBackendStatus::invalid_metadata,
                  "backend resource limits are invalid", true);
        return;
      }
      std::string metadata_error;
      if (!readMetadata(config.metadata_path, metadata, metadata_error)) {
        setStatus(CwCharacterBackendStatus::invalid_metadata,
                  std::move(metadata_error), true);
        return;
      }
      const QFileInfo model_info(QString::fromUtf8(config.model_path));
      if (!model_info.isFile() || model_info.size() <= 0 ||
          model_info.size() > kMaximumModelBytes) {
        setStatus(CwCharacterBackendStatus::model_load_failed,
                  "model must be a regular file no larger than 64 MiB", true);
        return;
      }

      Ort::SessionOptions options;
      options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      options.SetIntraOpNumThreads(1);
      options.SetInterOpNumThreads(1);
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
#if defined(_WIN32)
      const auto model_path = QString::fromUtf8(config.model_path).toStdWString();
      session = std::make_unique<Ort::Session>(environment, model_path.c_str(),
                                               options);
#else
      session = std::make_unique<Ort::Session>(environment,
                                               config.model_path.c_str(), options);
#endif
      validateModelContract();
      if (session)
        setStatus(CwCharacterBackendStatus::ready, {});
    } catch (const ModelContractError& exception) {
      session.reset();
      setStatus(CwCharacterBackendStatus::invalid_metadata,
                std::string("model and metadata are incompatible: ") +
                    exception.what(),
                true);
    } catch (const Ort::Exception& exception) {
      session.reset();
      setStatus(CwCharacterBackendStatus::model_load_failed,
                std::string("model could not be loaded: ") + exception.what(),
                true);
    } catch (const std::exception& exception) {
      session.reset();
      setStatus(CwCharacterBackendStatus::model_load_failed,
                std::string("model setup failed: ") + exception.what(), true);
    } catch (...) {
      session.reset();
      setStatus(CwCharacterBackendStatus::model_load_failed,
                "model setup failed", true);
    }
  }

  void validateModelContract() {
    if (session->GetInputCount() != 1 || session->GetOutputCount() != 1)
      throw ModelContractError("model must have exactly one input and output");
    Ort::AllocatorWithDefaultOptions allocator;
    const auto input_name = session->GetInputNameAllocated(0, allocator);
    const auto output_name = session->GetOutputNameAllocated(0, allocator);
    if (!input_name || metadata.input_name != input_name.get() ||
        !output_name || metadata.output_name != output_name.get()) {
      throw ModelContractError("model tensor names do not match metadata");
    }

    const auto input_type = session->GetInputTypeInfo(0);
    const auto output_type = session->GetOutputTypeInfo(0);
    const auto input = input_type.GetTensorTypeAndShapeInfo();
    const auto output = output_type.GetTensorTypeAndShapeInfo();
    if (input.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        output.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      throw ModelContractError(
          "model tensors must use float32 (input=" +
          std::to_string(static_cast<int>(input.GetElementType())) +
          ", output=" +
          std::to_string(static_cast<int>(output.GetElementType())) + ")");
    }
    const auto input_shape = input.GetShape();
    const auto output_shape = output.GetShape();
    const auto classes = static_cast<std::int64_t>(metadata.characters.size() + 1U);
    if (input_shape.size() != 4 || !compatibleDimension(input_shape[0], 1) ||
        !compatibleDimension(input_shape[1], 1) || input_shape[2] == 0 ||
        !compatibleDimension(input_shape[3],
                             static_cast<std::int64_t>(kFrequencyBins)) ||
        output_shape.size() != 3 || !compatibleDimension(output_shape[0], 1) ||
        output_shape[1] == 0 || !compatibleDimension(output_shape[2], classes)) {
      throw ModelContractError("model tensor shapes do not match metadata");
    }
    if (input_shape[2] > static_cast<std::int64_t>(config.maximum_input_frames) ||
        output_shape[1] >
            static_cast<std::int64_t>(config.maximum_output_steps)) {
      throw ModelContractError("model time dimensions exceed resource limits");
    }
    fixed_input_frames = input_shape[2] > 0
        ? static_cast<std::size_t>(input_shape[2]) : 0U;
  }

  [[nodiscard]] bool infer(
      const cwassistant::core::CwCharacterFeatureWindow& window,
      cwassistant::core::CwCharacterHypothesis& destination) noexcept {
    destination = {};
    if (!session) return false;
    try {
      if (const auto* error = windowError(window)) {
        setStatus(CwCharacterBackendStatus::invalid_input,
                  error, true);
        return false;
      }
      const std::array<std::int64_t, 4> input_shape{
          1, 1, static_cast<std::int64_t>(window.frame_count),
          static_cast<std::int64_t>(window.frequency_bins)};
      const auto memory = Ort::MemoryInfo::CreateCpu(
          OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault);
      auto input = Ort::Value::CreateTensor<float>(
          memory, const_cast<float*>(window.features.data()),
          window.features.size(), input_shape.data(), input_shape.size());
      const std::array<const char*, 1> input_names{metadata.input_name.c_str()};
      const std::array<const char*, 1> output_names{metadata.output_name.c_str()};
      Ort::RunOptions run_options;
      {
        const std::scoped_lock lock(run_options_mutex);
        active_run_options = &run_options;
      }
      std::vector<Ort::Value> outputs;
      try {
        outputs = session->Run(run_options, input_names.data(), &input, 1,
                               output_names.data(), 1);
      } catch (...) {
        clearActiveRunOptions(&run_options);
        throw;
      }
      clearActiveRunOptions(&run_options);
      if (outputs.size() != 1 || !outputs.front().IsTensor())
        throw std::runtime_error("model returned no tensor output");
      const auto output_info = outputs.front().GetTensorTypeAndShapeInfo();
      const auto output_shape = output_info.GetShape();
      const std::size_t class_count = metadata.characters.size() + 1U;
      if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
          output_shape.size() != 3 || output_shape[0] != 1 ||
          output_shape[1] <= 0 ||
          output_shape[2] != static_cast<std::int64_t>(class_count) ||
          static_cast<std::uint64_t>(output_shape[1]) >
              config.maximum_output_steps) {
        setStatus(CwCharacterBackendStatus::invalid_output,
                  "model output tensor has an invalid shape or type", true);
        return false;
      }
      const auto output_steps = static_cast<std::size_t>(output_shape[1]);
      const float* scores = outputs.front().GetTensorData<float>();
      cwassistant::core::CwCharacterHypothesis hypothesis;
      hypothesis.track = window.track;
      hypothesis.window_sequence = window.sequence;
      hypothesis.window_started_ns = window.started_ns;
      hypothesis.window_ended_ns = window.ended_ns;
      const std::size_t edge = std::min(config.edge_context_frames,
                                        window.frame_count / 2U);
      hypothesis.valid_started_ns = window.frame_timestamps_ns[edge];
      hypothesis.valid_ended_ns =
          window.frame_timestamps_ns[window.frame_count - 1U - edge];
      decode(scores, output_steps, class_count, window, hypothesis);
      destination = std::move(hypothesis);
      ++diagnostics.inference_count;
      diagnostics.last_input_frames = window.frame_count;
      diagnostics.last_output_steps = output_steps;
      diagnostics.last_emitted_characters = destination.characters.size();
      setStatus(CwCharacterBackendStatus::ready, {});
      return true;
    } catch (const Ort::Exception& exception) {
      setStatus(CwCharacterBackendStatus::inference_failed,
                std::string("inference failed: ") + exception.what(), true);
    } catch (const std::exception& exception) {
      setStatus(CwCharacterBackendStatus::invalid_output,
                std::string("model output failed validation: ") +
                    exception.what(),
                true);
    } catch (...) {
      setStatus(CwCharacterBackendStatus::inference_failed,
                "inference failed", true);
    }
    destination = {};
    return false;
  }

  void requestCancellation() noexcept {
    const std::scoped_lock lock(run_options_mutex);
    if (active_run_options == nullptr) return;
    try {
      active_run_options->SetTerminate();
    } catch (...) {
    }
  }

  void clearActiveRunOptions(Ort::RunOptions* expected) noexcept {
    const std::scoped_lock lock(run_options_mutex);
    if (active_run_options == expected) active_run_options = nullptr;
  }

  [[nodiscard]] const char* windowError(
      const cwassistant::core::CwCharacterFeatureWindow& window) const noexcept {
    if (window.track.track_id == 0 || window.sequence == 0)
      return "feature window identity is invalid";
    if (window.frame_count == 0 ||
        window.frame_count > config.maximum_input_frames ||
        (fixed_input_frames != 0 && window.frame_count != fixed_input_frames))
      return "feature window frame count is incompatible with the model";
    if (window.frequency_bins != kFrequencyBins)
      return "feature window must contain exactly 65 frequency bins";
    if (window.features.size() != window.frame_count * window.frequency_bins)
      return "feature window tensor size is inconsistent";
    if (window.frame_timestamps_ns.size() != window.frame_count)
      return "feature window timestamp count is inconsistent";
    if (window.ended_ns < window.started_ns)
      return "feature window time range is invalid";
    for (std::size_t index = 0; index < window.frame_count; ++index) {
      const auto timestamp = window.frame_timestamps_ns[index];
      if (timestamp < window.started_ns || timestamp > window.ended_ns ||
          (index != 0 && timestamp < window.frame_timestamps_ns[index - 1U])) {
        return "feature window timestamps are not monotonic";
      }
    }
    if (!std::all_of(window.features.begin(), window.features.end(),
                     [](const float value) { return std::isfinite(value); }))
      return "feature window contains a non-finite value";
    return nullptr;
  }

  void decode(const float* scores, const std::size_t steps,
              const std::size_t classes,
              const cwassistant::core::CwCharacterFeatureWindow& window,
              cwassistant::core::CwCharacterHypothesis& hypothesis) const {
    std::size_t active_class = metadata.blank_index;
    std::size_t active_start = 0;
    float active_confidence_sum = 0.0F;
    std::size_t active_count = 0;

    const auto append_character = [&](const std::size_t end_step) {
      if (active_class == metadata.blank_index || active_count == 0) return;
      if (hypothesis.characters.size() >= config.maximum_characters)
        throw std::runtime_error("decoded character count exceeds the limit");
      const std::size_t character_index = active_class < metadata.blank_index
          ? active_class : active_class - 1U;
      if (character_index >= metadata.characters.size())
        throw std::runtime_error("model emitted an invalid class index");
      const std::size_t start_frame = std::min(
          window.frame_count - 1U,
          active_start * window.frame_count / steps);
      const std::size_t end_frame = std::min(
          window.frame_count - 1U,
          ((end_step + 1U) * window.frame_count + steps - 1U) / steps - 1U);
      const std::size_t next_frame = std::min(window.frame_count - 1U,
                                              end_frame + 1U);
      const auto ended_ns = next_frame == end_frame
          ? window.ended_ns : window.frame_timestamps_ns[next_frame];
      hypothesis.characters.push_back({
          .symbol = metadata.characters[character_index],
          .started_ns = window.frame_timestamps_ns[start_frame],
          .ended_ns = std::max(ended_ns,
                               window.frame_timestamps_ns[start_frame]),
          .confidence = active_confidence_sum /
                        static_cast<float>(active_count),
      });
    };

    for (std::size_t step = 0; step < steps; ++step) {
      const float* row = scores + step * classes;
      if (!std::all_of(row, row + classes,
                       [](const float value) { return std::isfinite(value); })) {
        throw std::runtime_error("model emitted a non-finite score");
      }
      const auto maximum = std::max_element(row, row + classes);
      const std::size_t predicted = static_cast<std::size_t>(maximum - row);
      double denominator = 0.0;
      for (std::size_t index = 0; index < classes; ++index)
        denominator += std::exp(static_cast<double>(row[index] - *maximum));
      const float confidence = static_cast<float>(1.0 / denominator);
      if (predicted == active_class) {
        if (predicted != metadata.blank_index) {
          active_confidence_sum += confidence;
          ++active_count;
        }
        continue;
      }
      if (step != 0) append_character(step - 1U);
      active_class = predicted;
      active_start = step;
      active_confidence_sum = predicted == metadata.blank_index ? 0.0F
                                                                 : confidence;
      active_count = predicted == metadata.blank_index ? 0U : 1U;
    }
    append_character(steps - 1U);
  }

  CwCharacterBackendConfig config;
  CwCharacterBackendDiagnostics diagnostics;
  ModelMetadata metadata;
  Ort::Env environment;
  std::unique_ptr<Ort::Session> session;
  std::mutex run_options_mutex;
  Ort::RunOptions* active_run_options{nullptr};
  std::size_t fixed_input_frames{0};
};

CwOnnxCharacterDecoderBackend::CwOnnxCharacterDecoderBackend(
    CwCharacterBackendConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CwOnnxCharacterDecoderBackend::~CwOnnxCharacterDecoderBackend() = default;
CwOnnxCharacterDecoderBackend::CwOnnxCharacterDecoderBackend(
    CwOnnxCharacterDecoderBackend&&) noexcept = default;
CwOnnxCharacterDecoderBackend& CwOnnxCharacterDecoderBackend::operator=(
    CwOnnxCharacterDecoderBackend&&) noexcept = default;

bool CwOnnxCharacterDecoderBackend::ready() const noexcept {
  return impl_ && impl_->session != nullptr;
}

const CwCharacterBackendDiagnostics&
CwOnnxCharacterDecoderBackend::diagnostics() const noexcept {
  static const CwCharacterBackendDiagnostics unavailable{
      .status = CwCharacterBackendStatus::unavailable,
      .message = "backend has no implementation",
  };
  if (!impl_) return unavailable;
  return impl_->diagnostics;
}

bool CwOnnxCharacterDecoderBackend::infer(
    const cwassistant::core::CwCharacterFeatureWindow& window,
    cwassistant::core::CwCharacterHypothesis& destination) noexcept {
  return impl_ && impl_->infer(window, destination);
}

void CwOnnxCharacterDecoderBackend::requestCancellation() noexcept {
  if (impl_) impl_->requestCancellation();
}

}  // namespace cwassistant::desktop
