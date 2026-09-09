#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "cwassistant/core/cw_decoder.hpp"
#include "cwassistant/core/cw_event_lattice.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void testCommitLimitUsesTimeAndLaterRuns() {
  const std::array<cwassistant::core::CwRunObservation, 8> observations{{
      {1, true, 60.0, 1.0F, 1'000'000U, 61'000'000U},
      {2, false, 60.0, 1.0F, 61'000'000U, 121'000'000U},
      {3, true, 60.0, 1.0F, 121'000'000U, 181'000'000U},
      {4, false, 180.0, 1.0F, 181'000'000U, 361'000'000U},
      {5, true, 60.0, 1.0F, 361'000'000U, 421'000'000U},
      {6, false, 180.0, 1.0F, 421'000'000U, 601'000'000U},
      {7, true, 60.0, 1.0F, 601'000'000U, 661'000'000U},
      {8, false, 180.0, 1.0F, 661'000'000U, 841'000'000U},
  }};
  using cwassistant::core::cwFixedLagCommitObservationId;
  expect(cwFixedLagCommitObservationId(observations, 400.0, 3U) == 5U,
         "fixed lag requires both elapsed time and later-run support");
  expect(cwFixedLagCommitObservationId(observations, 900.0, 2U) == 0U,
         "a competitive recent suffix remains provisional");

  auto malformed = observations;
  malformed[4].started_ns = malformed[3].ended_ns - 1U;
  expect(cwFixedLagCommitObservationId(malformed, 100.0, 2U) == 0U,
         "non-monotonic timing fails closed");
}

void testLaterSpacingResolvesRecentWordGap() {
  cwassistant::core::CwEventLattice lattice;
  std::uint64_t now_ns = 1U;
  const auto append = [&](const bool keyed, const double duration_ms) {
    const auto started_ns = now_ns;
    now_ns += static_cast<std::uint64_t>(duration_ms * 1'000'000.0);
    static_cast<void>(lattice.append({.keyed = keyed,
                                      .duration_ms = duration_ms,
                                      .confidence = 1.0F,
                                      .started_ns = started_ns,
                                      .ended_ns = now_ns}));
  };
  for (int character = 0; character < 2; ++character) {
    append(true, 60.0);
    append(false, 360.0);
  }
  const auto early = lattice.decode(60.0);
  expect(!early.alternatives.empty() &&
             early.alternatives.front().symbols.front().word_boundary_after,
         "a recent stretched character gap remains acoustically ambiguous");

  for (int character = 0; character < 7; ++character) {
    append(true, 60.0);
    append(false, 360.0);
  }
  const auto resolved = lattice.decode(60.0);
  expect(!resolved.alternatives.empty() &&
             !resolved.alternatives.front().symbols.front().word_boundary_after,
         "later segment spacing resolves the earlier gap without context");
  const auto safe_id = cwassistant::core::cwFixedLagCommitObservationId(
      resolved.observations, 1'000.0, 6U);
  expect(safe_id >=
             resolved.alternatives.front().symbols.front().last_observation_id,
         "the resolved prefix becomes committable only behind the fixed lag");
}

struct DecoderFixture {
  cwassistant::core::CwMultiSpeedDecoder decoder{
      {.initial_wpm = 20.0},
      {.preferred_wpm = 20.0,
       .minimum_acquisition_ms = 100.0,
       .lock_after_symbols = 1,
       .lock_score_margin = 0.0F,
       .lattice_checkpoint_ms = 100.0,
       .lattice_fixed_lag_ms = 1'000.0,
       .lattice_fixed_lag_observations = 6,
       .minimum_lattice_evidence_confidence = 0.30F}};
  std::uint64_t now_ns{0};

  cwassistant::core::CwDecoderUpdate advance(const int milliseconds,
                                              const float evidence) {
    cwassistant::core::CwDecoderUpdate result;
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
      now_ns += 10'000'000U;
      result = decoder.process(now_ns, evidence);
    }
    return result;
  }

  cwassistant::core::CwDecoderUpdate sendE() {
    static_cast<void>(advance(60, 12.0F));
    return advance(180, 0.0F);
  }
};

void testSuspensionDefersButBoundaryFinalizes() {
  DecoderFixture brief;
  static_cast<void>(brief.advance(100, 0.0F));
  static_cast<void>(brief.sendE());
  const auto suspended = brief.decoder.suspendInput(brief.now_ns);
  expect(suspended.refined_text.empty(),
         "a short association suspension cannot force-finalize a suffix");
  const auto resumed = brief.decoder.resumeInput(brief.now_ns + 500'000'000U);
  expect(resumed.refined_text.empty() && resumed.transmissions.empty(),
         "reacquisition inside the silence bound preserves the active turn");

  std::string prior;
  bool append_only = true;
  for (int character = 0; character < 8; ++character) {
    const auto update = brief.sendE();
    append_only = append_only && update.refined_text.starts_with(prior);
    prior = update.refined_text;
  }
  expect(append_only && !prior.empty(),
         "old common-prefix symbols cross the lag boundary append-only");

  DecoderFixture ended;
  static_cast<void>(ended.advance(100, 0.0F));
  static_cast<void>(ended.sendE());
  static_cast<void>(ended.decoder.suspendInput(ended.now_ns));
  const auto boundary = ended.decoder.resumeInput(
      ended.now_ns + 3'000'000'000U);
  expect(!boundary.transmissions.empty() &&
             !boundary.transmissions.back().text.empty(),
         "sustained silence finalizes the bounded acoustic turn");
  const std::string first_turn_refined = boundary.refined_text;
  ended.now_ns += 3'000'000'000U;
  static_cast<void>(ended.sendE());
  const auto second_turn = ended.decoder.flush(ended.now_ns + 1'000'000U);
  expect(second_turn.refined_text.starts_with(first_turn_refined) &&
             second_turn.refined_text.size() > first_turn_refined.size(),
         "a new segment resets only its observation watermark and appends the "
         "second turn");

  DecoderFixture flushed;
  static_cast<void>(flushed.advance(100, 0.0F));
  static_cast<void>(flushed.sendE());
  const auto final = flushed.decoder.flush(flushed.now_ns + 1'000'000U);
  expect(!final.refined_text.empty() && !final.transmissions.empty(),
         "explicit flush finalizes the remaining suffix");
  expect(flushed.decoder.stateBytes() <= 256U * 1'024U,
         "fixed-lag refinement remains inside the decoder state budget");
}

}  // namespace

int main() {
  testCommitLimitUsesTimeAndLaterRuns();
  testLaterSpacingResolvesRecentWordGap();
  testSuspensionDefersButBoundaryFinalizes();
  std::cout << "cw_fixed_lag_refinement_tests: PASS\n";
  return EXIT_SUCCESS;
}
