#include <cmath>
#include <cstdlib>
#include <iostream>

#include "cwassistant/core/cw_turn_timing_fingerprint.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void appendRun(cwassistant::core::CwEventLattice& lattice,
               const bool keyed, const double duration_ms,
               std::uint64_t& now_ns, const float confidence = 0.90F) {
  const std::uint64_t started_ns = now_ns;
  now_ns += static_cast<std::uint64_t>(duration_ms * 1'000'000.0);
  const auto appended = lattice.append({.keyed = keyed,
                                        .duration_ms = duration_ms,
                                        .confidence = confidence,
                                        .started_ns = started_ns,
                                        .ended_ns = now_ns});
  expect(appended.has_value(), "test observation is accepted");
}

cwassistant::core::CwEventLatticeResult exactHandSentTurn() {
  cwassistant::core::CwEventLattice lattice;
  std::uint64_t now_ns = 1'000'000U;
  appendRun(lattice, true, 58.0, now_ns);
  appendRun(lattice, false, 62.0, now_ns);
  appendRun(lattice, true, 180.0, now_ns);
  appendRun(lattice, false, 178.0, now_ns);
  appendRun(lattice, true, 60.0, now_ns);
  appendRun(lattice, false, 420.0, now_ns);
  appendRun(lattice, true, 174.0, now_ns);
  return lattice.decode(60.0,
                        cwassistant::core::CwLatticeDecodeMode::Flush);
}

void testExactPhysicalTimingSummary() {
  const auto fingerprint = cwassistant::core::makeCwTurnTimingFingerprint(
      exactHandSentTurn(), 60.0);
  expect(fingerprint.has_value(), "complete aligned turn is summarized");
  expect(fingerprint->first_observation_id == 1U &&
             fingerprint->last_observation_id == 7U &&
             fingerprint->evidence_started_ns == 1'000'000U &&
             fingerprint->evidence_ended_ns == 1'133'000'000U,
         "fingerprint retains exact physical evidence bounds");
  expect(fingerprint->mark_count == 4U && fingerprint->gap_count == 3U &&
             fingerprint->dit_count == 2U &&
             fingerprint->dah_count == 2U &&
             fingerprint->element_gap_count == 1U &&
             fingerprint->character_gap_count == 1U &&
             fingerprint->word_gap_count == 1U,
         "physical runs are classified without semantic inference");
  expect(std::abs(fingerprint->dit_median_ms - 59.0) < 0.001 &&
             std::abs(fingerprint->dah_median_ms - 177.0) < 0.001 &&
             std::abs(fingerprint->keying_weight - 1.0) < 0.001 &&
             std::abs(fingerprint->normalized_mark_residual -
                      (1.0 / 60.0)) < 0.001,
         "medians expose cadence and manual key weighting");
}

void testIncompleteOrMisalignedEvidenceFailsClosed() {
  auto misaligned = exactHandSentTurn();
  misaligned.observations[2].started_ns += 1U;
  expect(!cwassistant::core::makeCwTurnTimingFingerprint(misaligned, 60.0),
         "a timestamp discontinuity cannot be fingerprinted");

  auto truncated = exactHandSentTurn();
  truncated.input_truncated = true;
  expect(!cwassistant::core::makeCwTurnTimingFingerprint(truncated, 60.0),
         "a truncated physical prefix cannot be fingerprinted");

  auto too_short = exactHandSentTurn();
  too_short.observations.resize(3U);
  expect(!cwassistant::core::makeCwTurnTimingFingerprint(too_short, 60.0),
         "insufficient timing evidence remains absent");
}

}  // namespace

int main() {
  testExactPhysicalTimingSummary();
  testIncompleteOrMisalignedEvidenceFailsClosed();
  std::cout << "cw_turn_timing_fingerprint_tests: PASS\n";
  return EXIT_SUCCESS;
}
