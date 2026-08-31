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
      config.averaging_frames > 32) {
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
  stream_initialized_ = false;
  average_initialized_ = false;
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
      accumulator_.clear();
    }
  }
  return output;
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
  for (std::size_t index = 0; index < config_.fft_size; ++index) {
    workspace_[index] = accumulator_[index] * window_[index];
  }
  fft(workspace_);

  const bool audio = stream_.kind == StreamKind::Audio;
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

  SpectrumSnapshot snapshot{
      .sequence = output_sequence_++,
      .timestamp_ns = timestamp_ns,
      .lower_frequency_hz =
          audio ? 0.0
                : stream_.center_frequency_hz - stream_.sample_rate_hz / 2.0,
      .upper_frequency_hz =
          audio ? stream_.sample_rate_hz / 2.0
                : stream_.center_frequency_hz + stream_.sample_rate_hz / 2.0,
      .bin_width_hz = stream_.sample_rate_hz /
                      static_cast<double>(config_.fft_size),
      .bins_dbfs = std::vector<float>(bin_count),
  };
  std::transform(averaged_power_.begin(), averaged_power_.end(),
                 snapshot.bins_dbfs.begin(), [](const float power) {
                   return 10.0F * std::log10(std::max(power, 1.0e-24F));
                 });
  return snapshot;
}

}  // namespace cwassistant::core
