#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

struct SpectrumAnalyzerConfig {
  std::size_t fft_size{2'048};
  std::uint8_t averaging_frames{3};
  std::uint16_t frame_rate_hz{60};
  bool audio_dc_rejection{false};
  bool audio_automatic_gain{false};
  float audio_gain_db{0.0F};
  float audio_automatic_gain_target_dbfs{-12.0F};
  bool audio_automatic_bandwidth{false};
  double audio_lower_frequency_hz{0.0};
  double audio_upper_frequency_hz{0.0};
};

struct SpectrumSnapshot {
  std::uint64_t sequence{0};
  std::uint64_t timestamp_ns{0};
  double lower_frequency_hz{0.0};
  double upper_frequency_hz{0.0};
  double bin_width_hz{0.0};
  std::vector<float> bins_dbfs;
  // Unaveraged bins from the same FFT. The decoder continues to use the
  // averaged spectrum for stable carrier tracking; the UI can select these
  // bins to preserve the time edges of dits, dahs, and gaps.
  std::vector<float> instantaneous_bins_dbfs;
};

class SpectrumAnalyzer {
 public:
  explicit SpectrumAnalyzer(SpectrumAnalyzerConfig config = {});

  [[nodiscard]] bool configure(SpectrumAnalyzerConfig config);
  void reset() noexcept;
  [[nodiscard]] const SpectrumAnalyzerConfig& config() const noexcept;
  [[nodiscard]] std::vector<SpectrumSnapshot> process(
      const RealtimeSampleBlock& block);

 private:
  void rebuild();
  [[nodiscard]] SpectrumSnapshot transform(std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t hopSize() const noexcept;

  SpectrumAnalyzerConfig config_{};
  StreamDescriptor stream_{};
  std::vector<float> window_;
  float window_sum_{0.0F};
  std::vector<std::complex<float>> accumulator_;
  std::vector<std::complex<float>> workspace_;
  std::vector<float> averaged_power_;
  std::uint64_t frame_timestamp_ns_{0};
  std::uint64_t expected_input_timestamp_ns_{0};
  std::uint64_t output_sequence_{0};
  float applied_audio_gain_db_{0.0F};
  bool stream_initialized_{false};
  bool average_initialized_{false};
  bool audio_gain_initialized_{false};
  bool input_timing_initialized_{false};
};

}  // namespace cwassistant::core
