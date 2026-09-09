#include "cwassistant/core/cw_turn_timing_fingerprint.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace cwassistant::core {
namespace {

constexpr std::size_t kMaximumFingerprintObservations = 512U;

template <std::size_t Capacity>
double median(std::array<double, Capacity>& values,
              const std::size_t count) noexcept {
  if (count == 0U) return 0.0;
  std::sort(values.begin(), values.begin() +
                                static_cast<std::ptrdiff_t>(count));
  const std::size_t middle = count / 2U;
  if ((count % 2U) != 0U) return values[middle];
  return 0.5 * (values[middle - 1U] + values[middle]);
}

bool durationMatchesTimestamps(const CwRunObservation& observation) noexcept {
  if (observation.ended_ns <= observation.started_ns) return false;
  const double timestamp_ms = static_cast<double>(
      observation.ended_ns - observation.started_ns) / 1'000'000.0;
  const double tolerance_ms = std::max(0.05, timestamp_ms * 0.005);
  return std::isfinite(observation.duration_ms) &&
         observation.duration_ms > 0.0 &&
         std::abs(observation.duration_ms - timestamp_ms) <= tolerance_ms;
}

}  // namespace

std::optional<CwTurnTimingFingerprint> makeCwTurnTimingFingerprint(
    const CwEventLatticeResult& lattice, const double dot_ms) noexcept {
  if (!std::isfinite(dot_ms) || dot_ms < 15.0 || dot_ms > 240.0 ||
      lattice.input_truncated || lattice.left_prefix_discarded ||
      lattice.observations.size() < 5U ||
      lattice.observations.size() > kMaximumFingerprintObservations ||
      !lattice.observations.front().keyed) {
    return std::nullopt;
  }

  std::array<double, kMaximumFingerprintObservations> dits{};
  std::array<double, kMaximumFingerprintObservations> dahs{};
  std::array<double, kMaximumFingerprintObservations> element_gaps{};
  std::array<double, kMaximumFingerprintObservations> character_gaps{};
  std::array<double, kMaximumFingerprintObservations> word_gaps{};
  std::array<double, kMaximumFingerprintObservations> mark_residuals{};
  std::size_t dit_count = 0U;
  std::size_t dah_count = 0U;
  std::size_t element_gap_count = 0U;
  std::size_t character_gap_count = 0U;
  std::size_t word_gap_count = 0U;
  double confidence_duration_sum = 0.0;
  double duration_sum = 0.0;

  for (std::size_t index = 0; index < lattice.observations.size(); ++index) {
    const auto& observation = lattice.observations[index];
    if (observation.observation_id == 0U ||
        !durationMatchesTimestamps(observation) ||
        !std::isfinite(observation.confidence) ||
        observation.confidence < 0.0F || observation.confidence > 1.0F) {
      return std::nullopt;
    }
    if (index > 0U) {
      const auto& previous = lattice.observations[index - 1U];
      if (observation.observation_id <= previous.observation_id ||
          observation.started_ns != previous.ended_ns ||
          observation.keyed == previous.keyed) {
        return std::nullopt;
      }
    }

    const double dots = observation.duration_ms / dot_ms;
    if (!std::isfinite(dots) || dots <= 0.0) return std::nullopt;
    confidence_duration_sum += static_cast<double>(observation.confidence) *
                               observation.duration_ms;
    duration_sum += observation.duration_ms;
    if (observation.keyed) {
      const bool dit = dots < 2.0;
      (dit ? dits[dit_count++] : dahs[dah_count++]) = observation.duration_ms;
      mark_residuals[dit_count + dah_count - 1U] =
          std::abs(dots - (dit ? 1.0 : 3.0));
    } else if (dots < 2.0) {
      element_gaps[element_gap_count++] = observation.duration_ms;
    } else if (dots < 5.0) {
      character_gaps[character_gap_count++] = observation.duration_ms;
    } else {
      word_gaps[word_gap_count++] = observation.duration_ms;
    }
  }

  const std::size_t mark_count = dit_count + dah_count;
  const std::size_t gap_count = element_gap_count + character_gap_count +
                                word_gap_count;
  if (mark_count < 3U || gap_count < 2U) return std::nullopt;

  CwTurnTimingFingerprint result{
      .first_observation_id = lattice.observations.front().observation_id,
      .last_observation_id = lattice.observations.back().observation_id,
      .evidence_started_ns = lattice.observations.front().started_ns,
      .evidence_ended_ns = lattice.observations.back().ended_ns,
      .mark_count = mark_count,
      .gap_count = gap_count,
      .dit_count = dit_count,
      .dah_count = dah_count,
      .element_gap_count = element_gap_count,
      .character_gap_count = character_gap_count,
      .word_gap_count = word_gap_count,
      .dit_median_ms = median(dits, dit_count),
      .dah_median_ms = median(dahs, dah_count),
      .element_gap_median_ms = median(element_gaps, element_gap_count),
      .character_gap_median_ms = median(character_gaps, character_gap_count),
      .word_gap_median_ms = median(word_gaps, word_gap_count),
      .normalized_mark_residual = median(mark_residuals, mark_count),
      .evidence_confidence = static_cast<float>(
          confidence_duration_sum / duration_sum),
  };
  if (dit_count > 0U && dah_count > 0U) {
    result.keying_weight = result.dah_median_ms /
                            (3.0 * result.dit_median_ms);
  }
  return result;
}

}  // namespace cwassistant::core
