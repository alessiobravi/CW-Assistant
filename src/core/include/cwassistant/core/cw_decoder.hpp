#pragma once

#include <cstdint>
#include <string>

namespace cwassistant::core {

struct CwDecoderConfig {
  float key_on_snr_db{6.0F};
  float key_off_snr_db{3.0F};
  double initial_wpm{20.0};
  float key_on_probability{0.68F};
  float key_off_probability{0.32F};
  double evidence_time_constant_ms{12.0};
  double character_gap_dots{2.2};
  double stable_gap_dots{3.1};
  double word_gap_dots{6.0};
};

struct CwDecoderUpdate {
  bool changed{false};
  bool key_down{false};
  float key_down_probability{0.0F};
  double wpm{0.0};
  float confidence{0.0F};
  std::string text;
  std::string provisional_text;
  std::string pending_elements;
};

class CwTimingDecoder {
 public:
  explicit CwTimingDecoder(CwDecoderConfig config = {});
  void reset() noexcept;
  [[nodiscard]] CwDecoderUpdate process(std::uint64_t timestamp_ns,
                                        float snr_db);
 [[nodiscard]] CwDecoderUpdate flush(std::uint64_t timestamp_ns);

 private:
  void finishElement(double duration_ms);
  void finishCharacter();
  void promoteProvisional();
  [[nodiscard]] float probabilityForSnr(float snr_db) const noexcept;
  [[nodiscard]] CwDecoderUpdate snapshot(bool changed) const;

  CwDecoderConfig config_;
  std::string stable_text_;
  std::string provisional_text_;
  std::string elements_;
  std::uint64_t state_started_ns_{0};
  std::uint64_t last_timestamp_ns_{0};
  double dot_ms_{60.0};
  float last_snr_db_{0.0F};
  float key_down_probability_{0.0F};
  float confidence_{0.0F};
  float element_confidence_sum_{0.0F};
  float mark_probability_sum_{0.0F};
  double mark_probability_duration_ms_{0.0};
  std::uint8_t element_count_{0};
  bool initialized_{false};
  bool key_down_{false};
  bool character_finished_{false};
  bool word_space_emitted_{false};
};

}  // namespace cwassistant::core
