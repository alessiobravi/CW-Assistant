#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cwassistant/core/cw_event_lattice.hpp"

namespace cwassistant::core {

// A completed turn's bounded physical timing summary. This deliberately
// carries no callsign, language, carrier-frequency, or sender-confidence
// fields: it describes only the timestamped key runs retained by the event
// lattice. A missing value means that the run sequence was incomplete,
// truncated, or not sufficiently aligned to support a truthful measurement.
struct CwTurnTimingFingerprint {
  std::uint64_t first_observation_id{0};
  std::uint64_t last_observation_id{0};
  std::uint64_t evidence_started_ns{0};
  std::uint64_t evidence_ended_ns{0};
  std::size_t mark_count{0};
  std::size_t gap_count{0};
  std::size_t dit_count{0};
  std::size_t dah_count{0};
  std::size_t element_gap_count{0};
  std::size_t character_gap_count{0};
  std::size_t word_gap_count{0};
  double dit_median_ms{0.0};
  double dah_median_ms{0.0};
  double element_gap_median_ms{0.0};
  double character_gap_median_ms{0.0};
  double word_gap_median_ms{0.0};
  // Dash duration divided by three times dit duration. One is nominal;
  // values above/below one describe manual key weighting, not identity.
  double keying_weight{0.0};
  // Median absolute mark residual in dot units from the nearest 1/3-dot
  // Morse timing centre.
  double normalized_mark_residual{0.0};
  float evidence_confidence{0.0F};
};

// Derives a fingerprint only from a complete, contiguous, alternating lattice
// observation sequence. The fixed capacity is an intentional diagnostic bound;
// oversized or left-truncated turns fail closed instead of being summarized
// from a misleading suffix.
[[nodiscard]] std::optional<CwTurnTimingFingerprint>
makeCwTurnTimingFingerprint(const CwEventLatticeResult& lattice,
                            double dot_ms) noexcept;

}  // namespace cwassistant::core
