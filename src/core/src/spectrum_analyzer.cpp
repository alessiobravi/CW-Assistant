#include "cwassistant/core/spectrum_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cwassistant::core {
namespace {

bool valid_fft_size(const std::size_t size) noexcept {
  return size >= 64 && size <= RealtimeSampleBlock{}.samples.size() &&
         (size & (size - 1U)) == 0;
}

void fft(std::vector<std::complex<float>>& values) {
  const std::size_t size = values.size();
  for (std::size_t index = 1, reversed = 0; index < size; ++index) {
    std::size_t bit = size >> 1U;
    for (; (reversed & bit) != 0; bit >>= 1U) {
      reversed ^= bit;
    }
    reversed ^= bit;
    if (index < reversed) {
      std::swap(values[index], values[reversed]);
    }
  }

  for (std::size_t length = 2; length <= size; length <<= 1U) {
    const float angle =
        -2.0F * std::numbers::pi_v<float> / static_cast<float>(length);
    const std::complex<float> step{std::cos(angle), std::sin(angle)};
    for (std::size_t base = 0; base < size; base += length) {
      std::complex<float> rotation{1.0F, 0.0F};
      const std::size_t half = length / 2U;
      for (std::size_t offset = 0; offset < half; ++offset) {
        const auto even = values[base + offset];
        const auto odd = values[base + offset + half] * rotation;
        values[base + offset] = even + odd;
        values[base + offset + half] = even - odd;
        rotation *= step;
      }
    }
  }
}

bool same_stream(const StreamDescriptor& left,
                 const StreamDescriptor& right) noexcept {
  return left.kind == right.kind &&
         left.sample_rate_hz == right.sample_rate_hz &&
         left.center_frequency_hz == right.center_frequency_hz;
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(const SpectrumAnalyzerConfig config) {
  if (!configure(config)) {
    throw std::invalid_argument("invalid spectrum analyzer configuration");
  }
}

bool SpectrumAnalyzer::configure(SpectrumAnalyzerConfig config) {
  if (!valid_fft_size(config.fft_size) || config.averaging_frames == 0 ||
      config.averaging_frames > 32 || config.frame_rate_hz == 0 ||
      config.frame_rate_hz > 240 || config.audio_gain_db < -40.0F ||
      config.audio_gain_db > 40.0F ||
      config.audio_automatic_gain_target_dbfs < -40.0F ||
      config.audio_automatic_gain_target_dbfs > -1.0F ||
      config.audio_lower_frequency_hz < 0.0 ||
      config.audio_upper_frequency_hz < 0.0 ||
      (config.audio_upper_frequency_hz > 0.0 &&
       config.audio_upper_frequency_hz <= config.audio_lower_frequency_hz)) {
    return false;
  }
  config_ = config;
  rebuild();
  return true;
}

void SpectrumAnalyzer::reset() noexcept {
  accumulator_.clear();
  averaged_power_.clear();
  output_sequence_ = 0;
  frame_timestamp_ns_ = 0;
  expected_input_timestamp_ns_ = 0;
  stream_initialized_ = false;
  average_initialized_ = false;
  audio_gain_initialized_ = false;
  input_timing_initialized_ = false;
  applied_audio_gain_db_ = 0.0F;
}

const SpectrumAnalyzerConfig& SpectrumAnalyzer::config() const noexcept {
  return config_;
}

std::vector<SpectrumSnapshot> SpectrumAnalyzer::process(
    const RealtimeSampleBlock& block) {
  std::vector<SpectrumSnapshot> output;
  if (block.sample_count == 0 ||
      block.sample_count > block.samples.size() ||
      block.stream.sample_rate_hz <= 0.0) {
    return output;
  }

  if (!stream_initialized_ || !same_stream(stream_, block.stream)) {
    reset();
    stream_ = block.stream;
    stream_initialized_ = true;
  }

  if (input_timing_initialized_) {
    const std::uint64_t difference =
        block.timestamp_ns > expected_input_timestamp_ns_
            ? block.timestamp_ns - expected_input_timestamp_ns_
            : expected_input_timestamp_ns_ - block.timestamp_ns;
    const auto tolerance_ns = static_cast<std::uint64_t>(
        std::ceil(2.0 * 1'000'000'000.0 / block.stream.sample_rate_hz));
    if (difference > tolerance_ns) {
      accumulator_.clear();
      frame_timestamp_ns_ = block.timestamp_ns;
    }
  }

  accumulator_.reserve(config_.fft_size);
  for (std::size_t index = 0; index < block.sample_count; ++index) {
    if (accumulator_.empty()) {
      frame_timestamp_ns_ = block.timestamp_ns + static_cast<std::uint64_t>(
          static_cast<long double>(index) * 1'000'000'000.0L /
          block.stream.sample_rate_hz);
    }
    accumulator_.push_back(block.samples[index]);
    if (accumulator_.size() == config_.fft_size) {
      output.push_back(transform(frame_timestamp_ns_));
      const std::size_t hop = hopSize();
      if (hop >= accumulator_.size()) {
        accumulator_.clear();
      } else {
        accumulator_.erase(
            accumulator_.begin(),
            accumulator_.begin() + static_cast<std::ptrdiff_t>(hop));
        frame_timestamp_ns_ += static_cast<std::uint64_t>(
            static_cast<long double>(hop) * 1'000'000'000.0L /
            block.stream.sample_rate_hz);
      }
    }
  }
  expected_input_timestamp_ns_ =
      block.timestamp_ns + static_cast<std::uint64_t>(
                               static_cast<long double>(block.sample_count) *
                               1'000'000'000.0L /
                               block.stream.sample_rate_hz);
  input_timing_initialized_ = true;
  return output;
}

std::size_t SpectrumAnalyzer::hopSize() const noexcept {
  if (!stream_initialized_ || stream_.sample_rate_hz <= 0.0) {
    return config_.fft_size;
  }
  const auto requested = static_cast<std::size_t>(std::max<long long>(
      1, std::llround(stream_.sample_rate_hz /
                      static_cast<double>(config_.frame_rate_hz))));
  return std::min(requested, config_.fft_size);
}

void SpectrumAnalyzer::rebuild() {
  window_.resize(config_.fft_size);
  window_sum_ = 0.0F;
  for (std::size_t index = 0; index < config_.fft_size; ++index) {
    window_[index] =
        0.5F - 0.5F * std::cos(2.0F * std::numbers::pi_v<float> *
                              static_cast<float>(index) /
                              static_cast<float>(config_.fft_size - 1U));
    window_sum_ += window_[index];
  }
  workspace_.resize(config_.fft_size);
  reset();
}

SpectrumSnapshot SpectrumAnalyzer::transform(const std::uint64_t timestamp_ns) {
  const bool audio = stream_.kind == StreamKind::Audio;
  std::complex<float> audio_mean{0.0F, 0.0F};
  if (audio && config_.audio_dc_rejection) {
    for (const auto& sample : accumulator_) {
      audio_mean += sample;
    }
    audio_mean /= static_cast<float>(config_.fft_size);
  }

  float audio_peak = 0.0F;
  if (audio) {
    for (const auto& sample : accumulator_) {
      audio_peak = std::max(audio_peak, std::abs(sample - audio_mean));
    }
    float requested_gain_db = config_.audio_gain_db;
    if (config_.audio_automatic_gain && audio_peak > 1.0e-9F) {
      requested_gain_db = std::clamp(
          config_.audio_automatic_gain_target_dbfs -
              20.0F * std::log10(audio_peak),
          -40.0F, 40.0F);
    }
    if (!audio_gain_initialized_ || !config_.audio_automatic_gain) {
      applied_audio_gain_db_ = requested_gain_db;
      audio_gain_initialized_ = true;
    } else {
      const float smoothing =
          requested_gain_db < applied_audio_gain_db_ ? 0.5F : 0.1F;
      applied_audio_gain_db_ +=
          smoothing * (requested_gain_db - applied_audio_gain_db_);
    }
  }
  const float audio_gain =
      audio ? std::pow(10.0F, applied_audio_gain_db_ / 20.0F) : 1.0F;
  for (std::size_t index = 0; index < config_.fft_size; ++index) {
    workspace_[index] =
        (accumulator_[index] - audio_mean) * audio_gain * window_[index];
  }
  fft(workspace_);

  const std::size_t bin_count =
      audio ? config_.fft_size / 2U + 1U : config_.fft_size;
  std::vector<float> current_power(bin_count);
  for (std::size_t output_index = 0; output_index < bin_count;
       ++output_index) {
    const std::size_t fft_index =
        audio ? output_index
              : (output_index + config_.fft_size / 2U) % config_.fft_size;
    float scale = 1.0F / window_sum_;
    if (audio && output_index != 0 &&
        output_index != config_.fft_size / 2U) {
      scale *= 2.0F;
    }
    current_power[output_index] =
        std::norm(workspace_[fft_index]) * scale * scale;
  }

  if (!average_initialized_ || averaged_power_.size() != bin_count) {
    averaged_power_ = current_power;
    average_initialized_ = true;
  } else {
    const float alpha = 1.0F / static_cast<float>(config_.averaging_frames);
    for (std::size_t index = 0; index < bin_count; ++index) {
      averaged_power_[index] += alpha *
                                (current_power[index] - averaged_power_[index]);
    }
  }

  const double bin_width_hz = stream_.sample_rate_hz /
                              static_cast<double>(config_.fft_size);
  std::size_t first_bin = 0;
  std::size_t last_bin = bin_count - 1U;
  if (audio) {
    const double nyquist_hz = stream_.sample_rate_hz / 2.0;
    double lower_hz = config_.audio_lower_frequency_hz;
    double upper_hz = config_.audio_upper_frequency_hz <= 0.0
                          ? nyquist_hz
                          : config_.audio_upper_frequency_hz;
    if (config_.audio_automatic_bandwidth) {
      lower_hz = nyquist_hz > 200.0 ? 100.0 : 0.0;
      upper_hz = std::min(3'000.0, nyquist_hz);
    }
    lower_hz = std::clamp(lower_hz, 0.0, nyquist_hz);
    upper_hz = std::clamp(upper_hz, lower_hz, nyquist_hz);
    first_bin = std::min(
        last_bin, static_cast<std::size_t>(std::ceil(lower_hz / bin_width_hz)));
    last_bin = std::clamp(
        static_cast<std::size_t>(std::floor(upper_hz / bin_width_hz)),
        first_bin, last_bin);
  }

  SpectrumSnapshot snapshot{
      .sequence = output_sequence_++,
      .timestamp_ns = timestamp_ns,
      .lower_frequency_hz =
          audio ? static_cast<double>(first_bin) * bin_width_hz
                : stream_.center_frequency_hz - stream_.sample_rate_hz / 2.0,
      .upper_frequency_hz =
          audio ? static_cast<double>(last_bin) * bin_width_hz
                : stream_.center_frequency_hz + stream_.sample_rate_hz / 2.0,
      .bin_width_hz = bin_width_hz,
      .bins_dbfs = std::vector<float>(last_bin - first_bin + 1U),
      .instantaneous_bins_dbfs =
          std::vector<float>(last_bin - first_bin + 1U),
  };
  std::transform(averaged_power_.begin() + static_cast<std::ptrdiff_t>(first_bin),
                 averaged_power_.begin() +
                     static_cast<std::ptrdiff_t>(last_bin + 1U),
                 snapshot.bins_dbfs.begin(), [](const float power) {
                   return 10.0F * std::log10(std::max(power, 1.0e-24F));
                 });
  std::transform(current_power.begin() + static_cast<std::ptrdiff_t>(first_bin),
                 current_power.begin() +
                     static_cast<std::ptrdiff_t>(last_bin + 1U),
                 snapshot.instantaneous_bins_dbfs.begin(),
                 [](const float power) {
                   return 10.0F * std::log10(std::max(power, 1.0e-24F));
                 });
  return snapshot;
}

}  // namespace cwassistant::core
