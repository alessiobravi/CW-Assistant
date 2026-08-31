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
};

struct SpectrumSnapshot {
  std::uint64_t sequence{0};
  std::uint64_t timestamp_ns{0};
  double lower_frequency_hz{0.0};
  double upper_frequency_hz{0.0};
  double bin_width_hz{0.0};
  std::vector<float> bins_dbfs;
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

  SpectrumAnalyzerConfig config_{};
  StreamDescriptor stream_{};
  std::vector<float> window_;
  float window_sum_{0.0F};
  std::vector<std::complex<float>> accumulator_;
  std::vector<std::complex<float>> workspace_;
  std::vector<float> averaged_power_;
  std::uint64_t frame_timestamp_ns_{0};
  std::uint64_t output_sequence_{0};
  bool stream_initialized_{false};
  bool average_initialized_{false};
};

}  // namespace cwassistant::core
