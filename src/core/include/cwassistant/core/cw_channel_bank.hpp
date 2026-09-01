#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cwassistant/core/cw_decoder.hpp"
#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

struct CwChannelBankConfig {
  float acquisition_snr_db{7.0F};
  float retention_snr_db{2.5F};
  float detection_dynamic_range_db{96.0F};
  double minimum_separation_hz{45.0};
  double tracking_tolerance_hz{70.0};
  double empty_track_retention_seconds{2.0};
  double decoded_track_retention_seconds{8.0};
  double narrowband_width_hz{120.0};
  double noise_reference_offset_hz{180.0};
  double evidence_rate_hz{500.0};
  std::size_t maximum_tracks{24};
};

struct CwChannelSnapshot {
  std::uint64_t id{0};
  std::uint8_t color_index{0};
  double frequency_hz{0.0};
  float snr_db{0.0F};
  double wpm{0.0};
  float confidence{0.0F};
  float key_down_probability{0.0F};
  bool key_down{false};
  bool active{false};
  std::string text;
  std::string provisional_text;
  std::string pending_elements;
};

class CwChannelBank {
 public:
  explicit CwChannelBank(CwChannelBankConfig config = {});
  void reset() noexcept;
  [[nodiscard]] const std::vector<CwChannelSnapshot>& updateSpectrum(
      std::uint64_t timestamp_ns, double lower_frequency_hz,
      double upper_frequency_hz, std::span<const float> bins_dbfs);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& processSamples(
      const RealtimeSampleBlock& block);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& channels() const noexcept;

 private:
  struct Track {
    Track(std::uint64_t track_id, double frequency,
          std::uint64_t timestamp_ns);

    std::uint64_t id;
    double frequency_hz;
    std::uint64_t last_detected_ns;
    CwTimingDecoder decoder;
    CwDecoderUpdate update;
    float snr_db{0.0F};
    float spectral_snr_db{0.0F};
    bool matched{false};

    std::array<std::complex<float>, 3> center_filter{};
    std::array<std::complex<float>, 3> lower_filter{};
    std::array<std::complex<float>, 3> upper_filter{};
    std::complex<float> center_oscillator{1.0F, 0.0F};
    std::complex<float> lower_oscillator{1.0F, 0.0F};
    std::complex<float> upper_oscillator{1.0F, 0.0F};
    float center_power_sum{0.0F};
    float lower_power_sum{0.0F};
    float upper_power_sum{0.0F};
    std::size_t accumulated_samples{0};
    bool filter_initialized{false};
  };

  struct Candidate {
    double frequency_hz{0.0};
    float snr_db{0.0F};
  };

  [[nodiscard]] float estimateNoise(std::span<const float> bins_dbfs) const;
  [[nodiscard]] float spectralSnr(const Track& track,
                                  double lower_frequency_hz,
                                  double bin_width_hz,
                                  std::span<const float> bins_dbfs,
                                  float noise_dbfs) const;
  void resetFilter(Track& track) noexcept;
  void rebuildSnapshots();

  CwChannelBankConfig config_;
  std::vector<Track> tracks_;
  std::vector<CwChannelSnapshot> snapshots_;
  std::uint64_t next_track_id_{1};
  StreamDescriptor stream_{};
  std::uint64_t expected_sample_timestamp_ns_{0};
  bool stream_initialized_{false};
  bool sample_timing_initialized_{false};
};

}  // namespace cwassistant::core
