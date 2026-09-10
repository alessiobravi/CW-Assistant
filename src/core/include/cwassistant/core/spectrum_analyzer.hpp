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
  // Equivalent noise bandwidth of one bin, in hertz: the width of the ideal
  // rectangular filter that would collect the same noise power this analysis
  // window collects at one bin. It carries the Hann window's 1.5-bin noise
  // bandwidth (1.76 dB) as well as the bin spacing.
  //
  // The bins themselves stay calibrated with the window's coherent gain
  // (1/sum(w)), which is what a signal-level readout needs and what keeps a
  // full-scale carrier at 0 dBFS at every transform size. That calibration
  // cannot also make a noise reading size-independent: bin noise power is
  // proportional to bin bandwidth, so an unchanged floor reads
  // 10*log10(8) = 9 dB lower when the transform grows 2'048 -> 16'384, and no
  // single scale factor can hold a tone and a noise floor constant at once
  // (one requires a factor independent of the transform size, the other a
  // factor proportional to it). The choice made here is to keep the tone
  // calibration, which detection and every dBFS readout depend on, and to
  // publish the noise bandwidth so a consumer that judges a *floor* can
  // subtract 10*log10(noise_bandwidth_hz) and compare densities in dBFS/Hz
  // across transform sizes and sample rates.
  double noise_bandwidth_hz{0.0};
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
  // Equivalent noise bandwidth of the window, expressed in bins.
  float noise_bandwidth_bins_{1.0F};
  std::vector<std::complex<float>> accumulator_;
  std::vector<std::complex<float>> workspace_;
  std::vector<float> averaged_power_;
  std::uint64_t frame_timestamp_ns_{0};
  std::uint64_t expected_input_timestamp_ns_{0};
  std::uint64_t output_sequence_{0};
  std::size_t samples_to_skip_{0};
  float applied_audio_gain_db_{0.0F};
  bool stream_initialized_{false};
  bool average_initialized_{false};
  bool audio_gain_initialized_{false};
  bool input_timing_initialized_{false};
};

}  // namespace cwassistant::core
