#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cwassistant/core/cw_event_lattice.hpp"

namespace cwassistant::core {

struct CwCharacterEvidence {
  std::string symbol;
  float confidence{0.0F};
  float timing_quality{0.0F};
  bool known{false};
};

// A segment-scoped acoustic alternative from the mark/gap timing lattice.
// Unlike `text`, alternatives may change as more envelope evidence arrives.
// Observation IDs let a consumer relate every suggestion back to immutable
// physical runs without treating a plausible Morse string as ground truth.
struct CwAcousticAlternative {
  std::string text;
  std::string provisional_elements;
  double wpm{0.0};
  double acoustic_cost{0.0};
  float evidence_confidence{0.0F};
  std::uint64_t first_observation_id{0};
  std::uint64_t last_observation_id{0};
};

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
  float timing_quality{0.0F};
  float cadence_quality{0.0F};
  float mean_character_confidence{0.0F};
  std::uint32_t decoded_symbols{0};
  std::uint32_t unknown_symbols{0};
  std::uint32_t key_transitions{0};
  std::uint32_t cadence_observations{0};
  std::vector<CwCharacterEvidence> characters;
  // Append-only text agreed by every acoustically competitive lattice path at
  // completed symbol boundaries. It is deliberately separate from `text`
  // until capture-calibrated promotion into the primary decoder is proven.
  std::string refined_text;
  std::vector<CwAcousticAlternative> acoustic_alternatives;
  // Verification and hypothesis selection use this bounded recent window so
  // a track can recover from an earlier bad acquisition instead of carrying
  // lifetime-average evidence forever. The lifetime counters above remain
  // available for diagnostics.
  std::uint32_t recent_decoded_symbols{0};
  std::uint32_t recent_unknown_symbols{0};
  std::uint32_t recent_cadence_observations{0};
  // Independent run-length fit of recent marks/gaps to Morse's 1:3 and
  // 1:3:7 timing ratios. This is acoustic cadence evidence, not a language
  // prediction and not necessarily the currently selected decoder speed.
  double acoustic_wpm{0.0};
  float acoustic_cadence_confidence{0.0F};
};

class CwTimingDecoder {
 public:
  explicit CwTimingDecoder(CwDecoderConfig config = {});
  void reset() noexcept;
  [[nodiscard]] CwDecoderUpdate process(std::uint64_t timestamp_ns,
                                        float snr_db);
  [[nodiscard]] CwDecoderUpdate flush(std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t stateBytes() const noexcept;

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
  // Pure element-duration-ratio precision, deliberately excluding the
  // amplitude/keying-probability (mark_confidence) component folded into
  // element_confidence_sum_/confidence_ above, so it measures cadence
  // precision independently of SNR-driven
  // character confidence, rather than duplicating it.
  float timing_confidence_sum_{0.0F};
  float mark_probability_sum_{0.0F};
  double mark_probability_duration_ms_{0.0};
  std::uint32_t decoded_symbol_count_{0};
  std::uint32_t unknown_symbol_count_{0};
  std::uint32_t key_transition_count_{0};
  std::uint32_t cadence_observation_count_{0};
  std::uint8_t element_count_{0};
  std::vector<CwCharacterEvidence> characters_;
  std::vector<float> recent_cadence_quality_;
  CwCharacterEvidence provisional_character_;
  bool initialized_{false};
  bool key_down_{false};
  bool character_finished_{false};
  bool word_space_emitted_{false};
};

struct CwMultiSpeedConfig {
  double preferred_wpm{20.0};
  double minimum_acquisition_ms{2'500.0};
  double reacquire_after_silence_ms{2'500.0};
  std::uint8_t lock_after_symbols{2};
  float lock_score_margin{0.10F};
  double lattice_checkpoint_ms{500.0};
  double lattice_competitive_cost_margin{1.0};
  float minimum_lattice_evidence_confidence{0.40F};
};

class CwMultiSpeedDecoder {
 public:
  explicit CwMultiSpeedDecoder(CwDecoderConfig decoder_config = {},
                               CwMultiSpeedConfig config = {});
  void reset();
  [[nodiscard]] CwDecoderUpdate process(std::uint64_t timestamp_ns,
                                        float snr_db);
  [[nodiscard]] CwDecoderUpdate flush(std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t hypothesisCount() const noexcept;
  [[nodiscard]] std::size_t stateBytes() const noexcept;

 private:
  struct Hypothesis {
    Hypothesis(double speed_wpm, CwDecoderConfig config);

    double seed_wpm;
    CwTimingDecoder decoder;
    CwDecoderUpdate update;
  };

  [[nodiscard]] float score(const Hypothesis& hypothesis) const noexcept;
  [[nodiscard]] std::size_t selectLeader(float* margin = nullptr) const;
  [[nodiscard]] CwDecoderUpdate snapshot(bool changed) const;
  void considerLock(float margin);
  void resetHypotheses();
  void observeCadence(bool key_down, std::uint64_t timestamp_ns);
  void recomputeCadenceEstimate();
  void observeLattice(bool key_down, float key_down_probability,
                      std::uint64_t timestamp_ns);
  void refreshLattice(CwLatticeDecodeMode mode);
  void resetLatticeSegment() noexcept;

  CwDecoderConfig decoder_config_;
  CwMultiSpeedConfig config_;
  std::vector<Hypothesis> hypotheses_;
  std::size_t leader_index_{0};
  std::size_t locked_index_{0};
  std::uint64_t first_timestamp_ns_{0};
  std::uint64_t last_signal_timestamp_ns_{0};
  std::string committed_prefix_;
  CwEventLattice event_lattice_;
  std::string refined_text_;
  std::vector<CwAcousticAlternative> acoustic_alternatives_;
  std::uint64_t lattice_state_started_ns_{0};
  std::uint64_t lattice_last_timestamp_ns_{0};
  std::uint64_t lattice_last_decode_ns_{0};
  std::uint64_t lattice_committed_observation_id_{0};
  double lattice_confidence_sum_{0.0};
  double lattice_confidence_duration_ms_{0.0};
  bool lattice_initialized_{false};
  bool lattice_key_down_{false};
  static constexpr std::size_t kCadenceDurationWindow = 64;
  std::array<double, kCadenceDurationWindow> recent_mark_ms_{};
  std::array<double, kCadenceDurationWindow> recent_gap_ms_{};
  std::size_t recent_mark_count_{0};
  std::size_t recent_gap_count_{0};
  std::size_t recent_mark_index_{0};
  std::size_t recent_gap_index_{0};
  std::uint64_t cadence_state_started_ns_{0};
  double cadence_dot_ms_{0.0};
  float cadence_confidence_{0.0F};
  bool cadence_initialized_{false};
  bool cadence_key_down_{false};
  bool locked_{false};
  bool initialized_{false};
  bool signal_seen_{false};
};

}  // namespace cwassistant::core
