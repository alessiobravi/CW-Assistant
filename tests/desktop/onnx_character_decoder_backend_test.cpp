#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "decoder/onnx_character_decoder_backend.hpp"
#include "cwassistant/core/wav_replay_source.hpp"

namespace {

QByteArray validMetadata() {
  return R"json({
    "chars":[",",".","/","0","1","2","3","4","5","6","7","8","9","?","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z"," "],
    "blank_index":41,
    "sample_rate":3200,
    "fft_length":256,
    "hop_length":48,
    "spectrogram_min_freq_hz":400.0,
    "spectrogram_max_freq_hz":1200.0,
    "spectrogram_frequency_bins":65,
    "normalization":"log1p",
    "onnx_input_name":"spectrogram",
    "onnx_output_name":"log_probs",
    "onnx_input_layout":["batch","channel","time","frequency"],
    "onnx_output_layout":["batch","time","class"],
    "channel_count":1,
    "num_classes":42,
    "onnx_input_dtype":"float32",
    "onnx_output_dtype":"float32"
  })json";
}

bool writeFile(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes) == bytes.size();
}

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return 1;
}

int replayWav(cwassistant::desktop::CwOnnxCharacterDecoderBackend& backend,
              const std::string& path, const double center_frequency_hz) {
  using namespace std::chrono_literals;
  cwassistant::core::WavReplaySource source;
  if (!source.open(path, {.kind = cwassistant::core::StreamKind::Audio}) ||
      !source.start()) {
    return fail("WAV could not be opened: " + source.last_error());
  }
  cwassistant::core::CwCharacterLaneFrontend frontend;
  frontend.reset({.track_id = 1, .track_generation = 1,
                  .frontend_generation = 1},
                 center_frequency_hz);
  cwassistant::core::CwCharacterConsensusMerger merger;
  cwassistant::core::RealtimeSampleBlock block;
  std::size_t window_count = 0;
  while (source.read(block, 0ms)) {
    frontend.process(block);
    cwassistant::core::CwCharacterFeatureWindow window;
    if (!frontend.takeWindow(window)) continue;
    cwassistant::core::CwCharacterHypothesis hypothesis;
    if (!backend.infer(window, hypothesis))
      return fail("WAV inference failed: " + backend.diagnostics().message);
    std::string raw;
    raw.reserve(hypothesis.characters.size());
    for (const auto& character : hypothesis.characters)
      raw.push_back(character.symbol);
    const auto merged = merger.process(std::move(hypothesis));
    ++window_count;
    std::cout << "window=" << window_count << " raw=\"" << raw
              << "\" stable=\"" << merged.stable_text
              << "\" provisional=\"" << merged.provisional_text << "\"\n";
  }
  if (window_count == 0) return fail("WAV produced no complete feature window");
  std::cout << "summary windows=" << window_count << " stable=\""
            << merger.stableText() << "\" provisional=\""
            << merger.provisionalText() << "\"\n";
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  if (argc != 1 && argc != 3 && argc != 5) {
    return fail("usage: cwa_onnx_character_decoder_backend_test "
                "[model.onnx metadata.json [audio.wav center_hz]]");
  }
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("temporary directory unavailable");

  const QString valid_path = temporary.filePath(QStringLiteral("valid.json"));
  if (!writeFile(valid_path, validMetadata()))
    return fail("could not write valid metadata fixture");
  const QString invalid_path = temporary.filePath(QStringLiteral("invalid.json"));
  auto invalid_metadata = validMetadata();
  invalid_metadata.replace("\"time\",\"frequency\"",
                           "\"frequency\",\"time\"");
  if (!writeFile(invalid_path, invalid_metadata))
    return fail("could not write invalid metadata fixture");

  cwassistant::desktop::CwOnnxCharacterDecoderBackend invalid_backend({
      .model_path = temporary.filePath(QStringLiteral("absent.onnx"))
                        .toStdString(),
      .metadata_path = invalid_path.toStdString(),
  });
  if (invalid_backend.ready() ||
      invalid_backend.diagnostics().status !=
          cwassistant::desktop::CwCharacterBackendStatus::invalid_metadata ||
      invalid_backend.diagnostics().message.empty() ||
      invalid_backend.diagnostics().message.size() > 512U) {
    return fail("invalid metadata was not rejected safely");
  }

  cwassistant::desktop::CwOnnxCharacterDecoderBackend missing_model({
      .model_path = temporary.filePath(QStringLiteral("absent.onnx"))
                        .toStdString(),
      .metadata_path = valid_path.toStdString(),
  });
  if (missing_model.ready() ||
      missing_model.diagnostics().status !=
          cwassistant::desktop::CwCharacterBackendStatus::model_load_failed) {
    return fail("missing model was not rejected after valid metadata");
  }

  // Supplying a real model and its metadata is an explicit local integration
  // mode. CI does not bundle or download a model.
  if (argc == 3 || argc == 5) {
    const QString mismatched_path =
        temporary.filePath(QStringLiteral("mismatched.json"));
    auto mismatched_metadata = validMetadata();
    mismatched_metadata.replace("\"spectrogram\"", "\"wrong_input\"");
    if (!writeFile(mismatched_path, mismatched_metadata))
      return fail("could not write mismatched metadata fixture");
    cwassistant::desktop::CwOnnxCharacterDecoderBackend mismatched_backend({
        .model_path = argv[1],
        .metadata_path = mismatched_path.toStdString(),
    });
    if (mismatched_backend.ready() ||
        mismatched_backend.diagnostics().status !=
            cwassistant::desktop::CwCharacterBackendStatus::invalid_metadata) {
      return fail("model tensor names were not checked against metadata");
    }

    cwassistant::desktop::CwOnnxCharacterDecoderBackend backend({
        .model_path = argv[1],
        .metadata_path = argv[2],
    });
    if (!backend.ready())
      return fail("supplied model did not load: " +
                  backend.diagnostics().message);

    cwassistant::core::CwCharacterFeatureWindow window;
    window.track = {.track_id = 7, .track_generation = 3,
                    .frontend_generation = 2};
    window.sequence = 1;
    window.started_ns = 1'000'000'000ULL;
    window.frame_count = 512;
    window.frequency_bins = 65;
    window.features.assign(window.frame_count * window.frequency_bins, 0.0F);
    window.frame_timestamps_ns.resize(window.frame_count);
    for (std::size_t frame = 0; frame < window.frame_count; ++frame) {
      window.frame_timestamps_ns[frame] =
          window.started_ns + 40'000'000ULL + frame * 15'000'000ULL;
    }
    window.ended_ns = window.frame_timestamps_ns.back() + 40'000'000ULL;

    cwassistant::core::CwCharacterHypothesis hypothesis;
    if (!backend.infer(window, hypothesis))
      return fail("zero-feature inference failed: " +
                  backend.diagnostics().message);
    if (hypothesis.track != window.track ||
        hypothesis.window_sequence != window.sequence ||
        hypothesis.characters.size() > 256U ||
        backend.diagnostics().inference_count != 1 ||
        backend.diagnostics().last_input_frames != window.frame_count) {
      return fail("inference result did not preserve the bounded contract");
    }
    for (const auto& character : hypothesis.characters) {
      if (character.started_ns < window.started_ns ||
          character.ended_ns > window.ended_ns ||
          character.ended_ns < character.started_ns ||
          !std::isfinite(character.confidence) || character.confidence < 0.0F ||
          character.confidence > 1.0F) {
        return fail("inference emitted invalid character timing/confidence");
      }
    }

    window.features.front() = std::numeric_limits<float>::quiet_NaN();
    if (backend.infer(window, hypothesis) ||
        backend.diagnostics().status !=
            cwassistant::desktop::CwCharacterBackendStatus::invalid_input ||
        !hypothesis.characters.empty()) {
      return fail("non-finite input was not rejected");
    }

    if (argc == 5) {
      bool valid_center = false;
      const double center_frequency_hz =
          QString::fromLocal8Bit(argv[4]).toDouble(&valid_center);
      if (!valid_center || !std::isfinite(center_frequency_hz) ||
          center_frequency_hz <= 0.0) {
        return fail("WAV center frequency must be a positive number");
      }
      return replayWav(backend, argv[3], center_frequency_hz);
    }
  }
  return 0;
}
