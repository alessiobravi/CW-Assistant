#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cwassistant::core {

// One observation from the keyed-envelope transition detector. Alternating
// physical runs are retained exactly; adjacent fragments with the same key
// state may only be coalesced into their duration-weighted physical run. The
// lattice never rewrites run evidence to make a decoded symbol legal.
struct CwRunObservation {
  // Assigned by CwEventLattice::append(). Caller-provided values are ignored.
  // IDs are never renumbered when the bounded history drops its left edge.
  std::uint64_t observation_id{0};
  bool keyed{false};
  double duration_ms{0.0};
  float confidence{0.0F};
  std::uint64_t started_ns{0};
  std::uint64_t ended_ns{0};
};

struct CwLatticeSymbol {
  std::string symbol;
  std::string elements;
  std::uint64_t first_observation_id{0};
  std::uint64_t last_observation_id{0};
  bool known{false};
  bool word_boundary_after{false};
};

struct CwLatticeAlternative {
  std::vector<CwLatticeSymbol> symbols;
  // A trailing element sequence is exposed separately until a following gap
  // proves a character boundary or the caller explicitly requests Flush.
  std::string provisional_elements;
  std::uint64_t provisional_first_observation_id{0};
  std::uint64_t provisional_last_observation_id{0};
  double acoustic_cost{0.0};
  float evidence_confidence{0.0F};

  // UNKNOWN is rendered with unknown_marker. A decoded question-mark
  // punctuation symbol remains distinguishable through CwLatticeSymbol::known.
  [[nodiscard]] std::string text(char unknown_marker = '?') const;
};

struct CwEventLatticeResult {
  std::vector<CwRunObservation> observations;
  std::vector<CwLatticeAlternative> alternatives;
  bool input_truncated{false};
  bool left_prefix_discarded{false};
  std::size_t rejected_observations{0};
  std::size_t coalesced_observations{0};
  double effective_timing_tolerance_scale{1.0};
};

enum class CwLatticeDecodeMode : std::uint8_t {
  Provisional,
  Flush,
};

struct CwEventLatticeConfig {
  std::size_t maximum_observations{128};
  std::size_t beam_width{16};
  std::size_t maximum_alternatives{4};
  // Standard symbols use at most nine elements. Extra capacity lets a hard
  // negative remain one UNKNOWN span instead of becoming a legal suffix.
  std::size_t maximum_elements_per_symbol{16};
  double compressed_character_gap_dots{2.0};
  double character_gap_dots{3.0};
  double compressed_word_gap_dots{4.5};
  double word_gap_dots{7.0};
  double unknown_symbol_cost{2.5};
  // Conservative floor and hard ceiling for the automatically estimated
  // per-segment timing tolerance. Estimation never moves Morse timing centers.
  // A larger effective value also reduces evidence_confidence, so hand-sent
  // variability is never presented as additional certainty.
  double timing_tolerance_scale{1.0};
  double maximum_timing_tolerance_scale{2.5};
  std::size_t minimum_tolerance_observations{12};
  float minimum_tolerance_observation_confidence{0.55F};
};

// Bounded acoustic candidate generator. It deliberately has no language,
// callsign, database, frequency, or QSO-context input. All alternatives arise
// only from the supplied mark and gap durations.
class CwEventLattice {
 public:
  explicit CwEventLattice(CwEventLatticeConfig config = {});

  void reset() noexcept;
  // Invalid/non-monotonic observations are rejected. Adjacent observations
  // with the same key state are coalesced into one physical run and return
  // that run's existing ID.
  [[nodiscard]] std::optional<std::uint64_t> append(
      CwRunObservation observation);

  // This bounded batch operation is intended for a segment boundary or a
  // coarse fixed-lag checkpoint, not for every audio sample.
  [[nodiscard]] CwEventLatticeResult decode(
      double dot_ms,
      CwLatticeDecodeMode mode = CwLatticeDecodeMode::Provisional) const;
  [[nodiscard]] std::size_t observationCount() const noexcept;
  [[nodiscard]] std::size_t stateBytes() const noexcept;

 private:
  CwEventLatticeConfig config_;
  std::vector<CwRunObservation> observations_;
  bool input_truncated_{false};
  std::uint64_t next_observation_id_{1};
  std::size_t rejected_observations_{0};
  std::size_t coalesced_observations_{0};
};

}  // namespace cwassistant::core
