#include "cwassistant/core/iq_writer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cwassistant::core {
namespace {

// SigMF's `_le` datatypes are little-endian on disk regardless of the host, so
// every field is serialized byte by byte rather than by copying host storage.
// This costs nothing measurable and keeps a capture made on a big-endian host
// readable by every standard reader.
void put_i16_le(char* destination, const std::int16_t value) noexcept {
  const auto bits = static_cast<std::uint16_t>(value);
  destination[0] = static_cast<char>(bits & 0xFFU);
  destination[1] = static_cast<char>((bits >> 8U) & 0xFFU);
}

void put_f32_le(char* destination, const float value) noexcept {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  destination[0] = static_cast<char>(bits & 0xFFU);
  destination[1] = static_cast<char>((bits >> 8U) & 0xFFU);
  destination[2] = static_cast<char>((bits >> 16U) & 0xFFU);
  destination[3] = static_cast<char>((bits >> 24U) & 0xFFU);
}

// Normalized float to signed 16-bit. The float is clamped before scaling so a
// provider that overshoots unity saturates at full scale instead of wrapping
// to the opposite rail -- a wrapped sample would look like a violent
// discontinuity and would be read as a real RF event by any later analysis.
// The clamp is symmetric (+/-32767 rather than -32768) so the two rails stay
// equidistant from zero and no DC bias is introduced by clipping alone.
std::int16_t to_i16(const float value) noexcept {
  if (!std::isfinite(value)) return 0;
  const float clamped = std::clamp(value, -1.0F, 1.0F);
  return static_cast<std::int16_t>(std::lround(clamped * 32'767.0F));
}

void append_escaped(std::string& out, const std::string_view text) {
  out.push_back('"');
  for (const char character : text) {
    switch (character) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U) {
          char escape[7]{};
          std::snprintf(escape, sizeof(escape), "\\u%04x",
                        static_cast<unsigned>(
                            static_cast<unsigned char>(character)));
          out.append(escape);
        } else {
          out.push_back(character);
        }
        break;
    }
  }
  out.push_back('"');
}

// A reader recovers absolute RF from `core:frequency` and bin spacing from
// `core:sample_rate`, so the printed value must convert back to exactly the
// double that was recorded. The shortest of the three precisions that still
// round-trips is used, which keeps a human-readable sidecar (8000000, 0.98)
// instead of the 17-digit noise a blanket %.17g produces. Non-finite values
// cannot be expressed in JSON at all, so they become 0 rather than producing
// a file no parser will accept.
void append_number(std::string& out, const double value) {
  if (!std::isfinite(value)) {
    out.append("0");
    return;
  }
  char buffer[40]{};
  for (int precision = 15; precision <= 17; ++precision) {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (std::strtod(buffer, nullptr) == value) break;
  }
  out.append(buffer);
}

void append_unsigned(std::string& out, const std::uint64_t value) {
  char buffer[24]{};
  std::snprintf(buffer, sizeof(buffer), "%llu",
                static_cast<unsigned long long>(value));
  out.append(buffer);
}

void append_key(std::string& out, const std::string_view key) {
  append_escaped(out, key);
  out.append(": ");
}

// Days-from-civil, inverted. Implemented arithmetically instead of through
// gmtime() so the core stays free of the C time API's thread-safety and
// deprecation problems and behaves identically on every supported compiler.
void civil_from_days(std::int64_t serial_day, int& year, unsigned& month,
                     unsigned& day) noexcept {
  serial_day += 719'468;
  const std::int64_t era =
      (serial_day >= 0 ? serial_day : serial_day - 146'096) / 146'097;
  const auto day_of_era =
      static_cast<std::uint64_t>(serial_day - era * 146'097);
  const std::uint64_t year_of_era =
      (day_of_era - day_of_era / 1'460 + day_of_era / 36'524 -
       day_of_era / 146'096) /
      365;
  const std::int64_t civil_year =
      static_cast<std::int64_t>(year_of_era) + era * 400;
  const std::uint64_t day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const std::uint64_t shifted_month = (5 * day_of_year + 2) / 153;
  day = static_cast<unsigned>(day_of_year - (153 * shifted_month + 2) / 5 + 1);
  month =
      static_cast<unsigned>(shifted_month < 10 ? shifted_month + 3
                                               : shifted_month - 9);
  year = static_cast<int>(civil_year + (month <= 2 ? 1 : 0));
}

std::string iso8601_utc_now() {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  const auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch)
          .count();
  auto seconds = total_ms / 1'000;
  auto milliseconds = total_ms % 1'000;
  if (milliseconds < 0) {
    milliseconds += 1'000;
    --seconds;
  }
  std::int64_t serial_day = seconds / 86'400;
  auto second_of_day = seconds % 86'400;
  if (second_of_day < 0) {
    second_of_day += 86'400;
    --serial_day;
  }
  int year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  civil_from_days(serial_day, year, month, day);
  char buffer[40]{};
  std::snprintf(buffer, sizeof(buffer),
                "%04d-%02u-%02uT%02u:%02u:%02u.%03uZ", year, month, day,
                static_cast<unsigned>(second_of_day / 3'600),
                static_cast<unsigned>((second_of_day / 60) % 60),
                static_cast<unsigned>(second_of_day % 60),
                static_cast<unsigned>(milliseconds));
  return std::string(buffer);
}

}  // namespace

IqWriter::~IqWriter() { close(); }

std::string_view IqWriter::datatypeName(const IqSampleFormat format) noexcept {
  return format == IqSampleFormat::Cf32Le ? std::string_view("cf32_le")
                                          : std::string_view("ci16_le");
}

std::size_t IqWriter::bytesPerSample(const IqSampleFormat format) noexcept {
  return format == IqSampleFormat::Cf32Le ? 2U * sizeof(float)
                                          : 2U * sizeof(std::int16_t);
}

std::string IqWriter::metadataPathFor(const std::string_view data_path) {
  constexpr std::string_view kDataExtension = ".sigmf-data";
  constexpr std::string_view kMetaExtension = ".sigmf-meta";
  std::string path(data_path);
  if (path.size() >= kDataExtension.size() &&
      path.compare(path.size() - kDataExtension.size(), kDataExtension.size(),
                   kDataExtension) == 0) {
    path.resize(path.size() - kDataExtension.size());
  }
  path.append(kMetaExtension);
  return path;
}

bool IqWriter::open(const std::string_view data_path,
                    const IqCaptureMetadata& metadata,
                    const IqWriterLimits limits) {
  close();
  last_error_.clear();
  stop_reason_ = IqCaptureStopReason::None;
  stopped_ = false;
  samples_written_ = 0;
  last_block_levels_ = {};
  capture_levels_ = {};
  capture_sum_real_ = 0.0;
  capture_sum_imaginary_ = 0.0;
  segments_.clear();

  if (!(metadata.sample_rate_hz > 0.0) ||
      !std::isfinite(metadata.sample_rate_hz)) {
    last_error_ = "Invalid IQ sample rate";
    return false;
  }
  if (!(limits.maximum_seconds > 0.0) ||
      !std::isfinite(limits.maximum_seconds) ||
      limits.maximum_data_bytes < bytesPerSample(metadata.format)) {
    last_error_ = "Invalid IQ capture limits";
    return false;
  }

  metadata_ = metadata;
  limits_ = limits;
  if (metadata_.datetime_utc.empty()) {
    metadata_.datetime_utc = iso8601_utc_now();
  }

  // Both bounds become one sample count so the write path performs a single
  // comparison per block, and the binding bound is decided here so the stop
  // reason reported to the operator is exact.
  const auto byte_capacity = static_cast<std::uint64_t>(
      limits_.maximum_data_bytes / bytesPerSample(metadata_.format));
  const double duration_samples =
      limits_.maximum_seconds * metadata_.sample_rate_hz;
  const auto duration_capacity =
      duration_samples >= 18'000'000'000'000'000.0
          ? ~std::uint64_t{0}
          : static_cast<std::uint64_t>(duration_samples);
  if (byte_capacity < duration_capacity) {
    maximum_samples_ = byte_capacity;
    budget_reason_ = IqCaptureStopReason::ByteBudget;
  } else {
    maximum_samples_ = duration_capacity;
    budget_reason_ = IqCaptureStopReason::DurationBudget;
  }
  if (maximum_samples_ == 0) {
    last_error_ = "IQ capture limits leave no room for any sample";
    return false;
  }

  data_path_.assign(data_path);
  metadata_path_ = metadataPathFor(data_path);
  file_.open(data_path_, std::ios::binary | std::ios::trunc);
  if (!file_) {
    last_error_ = "Could not create IQ capture file";
    return false;
  }
  segments_.reserve(16);
  segments_.push_back({.sample_start = 0,
                       .frequency_hz = metadata_.center_frequency_hz,
                       .datetime_utc = metadata_.datetime_utc});

  // The sidecar is written now and rewritten on close. A recording
  // interrupted by a crash or a power loss then still describes itself well
  // enough to be read, which is the whole point of capturing for post-hoc
  // analysis; only the final counters are missing.
  if (!writeSidecar()) {
    close();
    return false;
  }
  return true;
}

bool IqWriter::writeBlock(const RealtimeSampleBlock& block) {
  if (!file_.is_open()) {
    last_error_ = "IQ capture file is not open";
    return false;
  }
  if (stopped_) return false;
  if (block.stream.kind != StreamKind::ComplexIq) {
    // Writing a real audio block here would silently produce a file whose Q
    // channel is meaningless. Refuse rather than record something misleading.
    last_error_ = "IQ capture received a non-complex block";
    stop(IqCaptureStopReason::WriteError);
    return false;
  }
  if (block.sample_count == 0 || block.sample_count > block.samples.size()) {
    return true;
  }
  // The rate is a single global field in SigMF. If the provider changes it
  // mid-stream the remaining samples cannot be described by this file, so the
  // capture ends here instead of mislabelling them.
  const double rate_difference =
      std::abs(block.stream.sample_rate_hz - metadata_.sample_rate_hz);
  if (rate_difference > 1e-6 * std::max(1.0, metadata_.sample_rate_hz)) {
    last_error_ = "IQ sample rate changed during capture";
    stop(IqCaptureStopReason::SampleRateChanged);
    return false;
  }
  // A retune is expressible: start a new capture segment at this sample.
  if (block.stream.center_frequency_hz != segments_.back().frequency_hz &&
      segments_.size() < kMaximumSegments) {
    segments_.push_back({.sample_start = samples_written_,
                         .frequency_hz = block.stream.center_frequency_hz,
                         .datetime_utc = iso8601_utc_now()});
  }

  const std::uint64_t remaining = maximum_samples_ - samples_written_;
  if (remaining == 0) {
    stop(budget_reason_);
    return false;
  }
  const auto to_write = static_cast<std::size_t>(
      std::min<std::uint64_t>(remaining, block.sample_count));

  const std::size_t sample_bytes = bytesPerSample(metadata_.format);
  const bool complex_float = metadata_.format == IqSampleFormat::Cf32Le;
  double sum_real = 0.0;
  double sum_imaginary = 0.0;
  double peak_power = 0.0;
  std::uint64_t near_full_scale = 0;
  for (std::size_t index = 0; index < to_write; ++index) {
    const std::complex<float> sample = block.samples[index];
    const float real = sample.real();
    const float imaginary = sample.imag();
    // Level telemetry is gathered in the same pass that encodes, while the
    // samples are already in cache, so a diagnostic recording costs barely
    // more than a blind one.
    sum_real += static_cast<double>(real);
    sum_imaginary += static_cast<double>(imaginary);
    const double power = static_cast<double>(real) * static_cast<double>(real) +
                         static_cast<double>(imaginary) *
                             static_cast<double>(imaginary);
    if (power > peak_power) peak_power = power;
    if (static_cast<double>(std::abs(real)) >= kNearFullScale ||
        static_cast<double>(std::abs(imaginary)) >= kNearFullScale) {
      ++near_full_scale;
    }
    char* const destination = staging_.data() + index * sample_bytes;
    if (complex_float) {
      put_f32_le(destination, real);
      put_f32_le(destination + sizeof(float), imaginary);
    } else {
      put_i16_le(destination, to_i16(real));
      put_i16_le(destination + sizeof(std::int16_t), to_i16(imaginary));
    }
  }

  file_.write(staging_.data(),
              static_cast<std::streamsize>(to_write * sample_bytes));
  if (!file_) {
    last_error_ = "Could not write IQ capture samples";
    stop(IqCaptureStopReason::WriteError);
    return false;
  }
  samples_written_ += to_write;

  const double peak_magnitude = std::sqrt(peak_power);
  const auto written = static_cast<std::uint64_t>(to_write);
  last_block_levels_ = {
      .samples = written,
      .peak_magnitude = peak_magnitude,
      .near_full_scale_samples = near_full_scale,
      .mean_real = sum_real / static_cast<double>(written),
      .mean_imaginary = sum_imaginary / static_cast<double>(written),
  };
  capture_sum_real_ += sum_real;
  capture_sum_imaginary_ += sum_imaginary;
  capture_levels_.samples = samples_written_;
  capture_levels_.peak_magnitude =
      std::max(capture_levels_.peak_magnitude, peak_magnitude);
  capture_levels_.near_full_scale_samples += near_full_scale;

  if (to_write < block.sample_count) {
    stop(budget_reason_);
    return false;
  }
  if (samples_written_ >= maximum_samples_) {
    stop(budget_reason_);
    return false;
  }
  return true;
}

void IqWriter::stop(const IqCaptureStopReason reason) {
  if (stopped_) return;
  stopped_ = true;
  stop_reason_ = reason;
  if (last_error_.empty()) {
    last_error_.assign(stopReasonText());
  }
}

bool IqWriter::writeSidecar() {
  std::string json;
  json.reserve(2'048);
  json.append("{\n  \"global\": {\n    ");
  append_key(json, "core:datatype");
  append_escaped(json, datatypeName(metadata_.format));
  json.append(",\n    ");
  append_key(json, "core:sample_rate");
  append_number(json, metadata_.sample_rate_hz);
  json.append(",\n    ");
  append_key(json, "core:version");
  append_escaped(json, "1.0.0");
  json.append(",\n    ");
  append_key(json, "core:hw");
  append_escaped(json, metadata_.hardware);
  json.append(",\n    ");
  append_key(json, "core:description");
  append_escaped(json, metadata_.description);
  json.append(",\n    ");
  append_key(json, "core:recorder");
  append_escaped(json, "CW Buddy");

  // Everything below is namespaced away from `core:` so a standard reader can
  // ignore it. It is written because a capture without the gain state and
  // level history documents the symptom and not the cause: the same spectrum
  // can mean a clipping front end or a starved one.
  json.append(",\n    ");
  append_key(json, "cwbuddy:automatic_gain_known");
  json.append(metadata_.automatic_gain_known ? "true" : "false");
  json.append(",\n    ");
  append_key(json, "cwbuddy:automatic_gain");
  json.append(metadata_.automatic_gain ? "true" : "false");
  json.append(",\n    ");
  append_key(json, "cwbuddy:gain_db");
  append_number(json, metadata_.gain_db);
  json.append(",\n    ");
  append_key(json, "cwbuddy:sample_count");
  append_unsigned(json, samples_written_);
  json.append(",\n    ");
  append_key(json, "cwbuddy:duration_seconds");
  append_number(json, secondsWritten());
  json.append(",\n    ");
  append_key(json, "cwbuddy:data_bytes");
  append_unsigned(json, bytesWritten());
  json.append(",\n    ");
  append_key(json, "cwbuddy:maximum_data_bytes");
  append_unsigned(json, limits_.maximum_data_bytes);
  json.append(",\n    ");
  append_key(json, "cwbuddy:maximum_seconds");
  append_number(json, limits_.maximum_seconds);
  json.append(",\n    ");
  append_key(json, "cwbuddy:stop_reason");
  append_escaped(json, stopReasonText());
  json.append(",\n    ");
  append_key(json, "cwbuddy:peak_magnitude");
  append_number(json, capture_levels_.peak_magnitude);
  json.append(",\n    ");
  append_key(json, "cwbuddy:near_full_scale_threshold");
  append_number(json, kNearFullScale);
  json.append(",\n    ");
  append_key(json, "cwbuddy:near_full_scale_samples");
  append_unsigned(json, capture_levels_.near_full_scale_samples);
  const auto levels = captureLevels();
  json.append(",\n    ");
  append_key(json, "cwbuddy:dc_offset_real");
  append_number(json, levels.mean_real);
  json.append(",\n    ");
  append_key(json, "cwbuddy:dc_offset_imaginary");
  append_number(json, levels.mean_imaginary);
  json.append("\n  },\n  \"captures\": [");
  for (std::size_t index = 0; index < segments_.size(); ++index) {
    const auto& segment = segments_[index];
    json.append(index == 0 ? "\n    {" : ",\n    {");
    json.append("\n      ");
    append_key(json, "core:sample_start");
    append_unsigned(json, segment.sample_start);
    json.append(",\n      ");
    append_key(json, "core:frequency");
    append_number(json, segment.frequency_hz);
    json.append(",\n      ");
    append_key(json, "core:datetime");
    append_escaped(json, segment.datetime_utc);
    json.append("\n    }");
  }
  json.append("\n  ],\n  \"annotations\": []\n}\n");

  std::ofstream sidecar(metadata_path_, std::ios::binary | std::ios::trunc);
  if (!sidecar) {
    last_error_ = "Could not create IQ capture metadata file";
    return false;
  }
  sidecar.write(json.data(), static_cast<std::streamsize>(json.size()));
  if (!sidecar) {
    last_error_ = "Could not write IQ capture metadata";
    return false;
  }
  sidecar.flush();
  return static_cast<bool>(sidecar);
}

void IqWriter::close() noexcept {
  if (!file_.is_open()) return;
  file_.flush();
  file_.close();
  try {
    static_cast<void>(writeSidecar());
  } catch (...) {
    // Closing must never propagate: this runs from the destructor as well,
    // and the sidecar written at open() is already on disk.
  }
}

bool IqWriter::isOpen() const noexcept { return file_.is_open(); }

std::uint64_t IqWriter::samplesWritten() const noexcept {
  return samples_written_;
}

std::uint64_t IqWriter::bytesWritten() const noexcept {
  return samples_written_ *
         static_cast<std::uint64_t>(bytesPerSample(metadata_.format));
}

double IqWriter::secondsWritten() const noexcept {
  if (!(metadata_.sample_rate_hz > 0.0)) return 0.0;
  return static_cast<double>(samples_written_) / metadata_.sample_rate_hz;
}

IqCaptureStopReason IqWriter::stopReason() const noexcept {
  return stop_reason_;
}

std::string_view IqWriter::stopReasonText() const noexcept {
  switch (stop_reason_) {
    case IqCaptureStopReason::ByteBudget:
      return "Reached the capture size budget";
    case IqCaptureStopReason::DurationBudget:
      return "Reached the capture duration budget";
    case IqCaptureStopReason::SampleRateChanged:
      return "The receiver sample rate changed during capture";
    case IqCaptureStopReason::WriteError:
      return "Could not continue writing the capture";
    case IqCaptureStopReason::None:
      break;
  }
  return "Still recording or stopped by the operator";
}

const IqLevelSummary& IqWriter::lastBlockLevels() const noexcept {
  return last_block_levels_;
}

IqLevelSummary IqWriter::captureLevels() const noexcept {
  IqLevelSummary levels = capture_levels_;
  if (samples_written_ > 0) {
    const auto count = static_cast<double>(samples_written_);
    levels.mean_real = capture_sum_real_ / count;
    levels.mean_imaginary = capture_sum_imaginary_ / count;
  }
  return levels;
}

const std::string& IqWriter::dataPath() const noexcept { return data_path_; }

const std::string& IqWriter::metadataPath() const noexcept {
  return metadata_path_;
}

const std::string& IqWriter::lastError() const noexcept { return last_error_; }

}  // namespace cwassistant::core
