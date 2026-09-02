#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cwassistant/core/cw_event_lattice.hpp"

namespace cwassistant::core {

struct CwProbabilityObservation {
  std::uint64_t timestamp_ns{0};
  float key_down_probability{0.0F};
};

struct CwProbabilisticAlternative {
  std::string text;
  std::string provisional_elements;
  double wpm{0.0};
  double acoustic_cost{0.0};
  float evidence_confidence{0.0F};
  std::uint64_t first_observation_id{0};
  std::uint64_t last_observation_id{0};
};

struct CwProbabilisticDecoderUpdate {
  // Transcript and alternative payloads are populated only when changed is
  // true. This keeps the per-probability-sample fast path allocation-free.
  bool changed{false};
  bool key_down{false};
  std::string stable_text;
  std::string provisional_text;
  std::string provisional_elements;
  double estimated_wpm{0.0};
  float evidence_confidence{0.0F};
  std::vector<CwProbabilisticAlternative> alternatives;
};

struct CwProbabilisticDecoderConfig {
  float key_on_probability{0.65F};
  float key_off_probability{0.35F};
  // A state change must remain beyond the hysteresis threshold for this long.
  // This rejects isolated probability spikes without smoothing legitimate
  // high-speed dits (about 20 ms at the supported 60 WPM ceiling).
  double minimum_transition_ms{8.0};
  double initial_wpm{20.0};
  double minimum_wpm{8.0};
  double maximum_wpm{60.0};
  double checkpoint_ms{250.0};
  double competitive_cost_margin{1.0};
  float minimum_stable_evidence_confidence{0.40F};
  std::size_t maximum_alternatives{8};
  std::size_t maximum_stable_characters{4'096};
  CwEventLatticeConfig lattice{};
};

// Streaming acoustic decoder boundary for a key/noise probability producer.
// It has no language, callsign, database, or conversation input. Probability
// samples become immutable physical runs; a bounded semi-Markov timing lattice
// retains N-best Morse/UNKNOWN segmentations. Only a time-aligned prefix on
// which competitive paths agree crosses the append-only stable boundary.
class CwProbabilisticMorseDecoder {
 public:
  explicit CwProbabilisticMorseDecoder(
      CwProbabilisticDecoderConfig config = {});

  void reset() noexcept;
  [[nodiscard]] CwProbabilisticDecoderUpdate process(
      CwProbabilityObservation observation);
  [[nodiscard]] CwProbabilisticDecoderUpdate flush(
      std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t stateBytes() const noexcept;

 private:
  void finishRun(std::uint64_t timestamp_ns);
  [[nodiscard]] CwProbabilisticDecoderUpdate decode(bool flush,
                                                     bool partial_gap);
  [[nodiscard]] CwProbabilisticDecoderUpdate snapshot(bool changed) const;

  CwProbabilisticDecoderConfig config_;
  CwEventLattice lattice_;
  std::string stable_text_;
  std::string provisional_text_;
  std::string provisional_elements_;
  std::vector<CwProbabilisticAlternative> alternatives_;
  std::uint64_t state_started_ns_{0};
  std::uint64_t last_timestamp_ns_{0};
  std::uint64_t last_decode_ns_{0};
  std::uint64_t committed_observation_id_{0};
  double state_probability_sum_{0.0};
  double state_probability_duration_ms_{0.0};
  double estimated_dot_ms_{60.0};
  float evidence_confidence_{0.0F};
  bool initialized_{false};
  bool key_down_{false};
  bool pending_transition_{false};
  bool pending_key_down_{false};
  std::uint64_t pending_transition_started_ns_{0};
  double pending_current_probability_sum_{0.0};
  double pending_new_probability_sum_{0.0};
  double pending_probability_duration_ms_{0.0};
};

}  // namespace cwassistant::core
