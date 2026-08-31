#pragma once

#include <cstdint>
#include <string>

namespace cwassistant::core {

struct CwDecoderConfig {
  float key_on_snr_db{6.0F};
  float key_off_snr_db{3.0F};
  double initial_wpm{20.0};
};

struct CwDecoderUpdate {
  bool changed{false};
  bool key_down{false};
  double wpm{0.0};
  float confidence{0.0F};
  std::string text;
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
  [[nodiscard]] CwDecoderUpdate snapshot(bool changed) const;

  CwDecoderConfig config_;
  std::string text_;
  std::string elements_;
  std::uint64_t state_started_ns_{0};
  double dot_ms_{60.0};
  float last_snr_db_{0.0F};
  float confidence_{0.0F};
  bool initialized_{false};
  bool key_down_{false};
  bool character_finished_{false};
  bool word_space_emitted_{false};
};

}  // namespace cwassistant::core
