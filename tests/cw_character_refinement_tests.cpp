#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "cwassistant/core/cw_character_decoder.hpp"
#include "cwassistant/core/cw_character_lane_frontend.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using cwassistant::core::CwCharacterFeatureWindow;
using cwassistant::core::CwCharacterLaneFrontend;
using cwassistant::core::CwCharacterTrackKey;
using cwassistant::core::CwTimedCharacter;
using cwassistant::core::RealtimeSampleBlock;

CwCharacterFeatureWindow runTone(CwCharacterLaneFrontend& frontend,
                                 const double tone_hz,
                                 const std::size_t block_size,
                                 const double seconds) {
  constexpr double sample_rate_hz = 3'200.0;
  const std::size_t total = static_cast<std::size_t>(
      std::llround(seconds * sample_rate_hz));
  double phase = 0.0;
  std::size_t offset = 0;
  CwCharacterFeatureWindow first;
  while (offset < total) {
    RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate_hz;
    block.sequence = offset / std::max<std::size_t>(1U, block_size);
    block.timestamp_ns = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(offset) * 1'000'000'000.0 / sample_rate_hz));
    block.sample_count = std::min(block_size, total - offset);
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          0.5F * static_cast<float>(std::sin(phase)), 0.0F};
      phase += 2.0 * std::numbers::pi * tone_hz / sample_rate_hz;
    }
    frontend.process(block);
    CwCharacterFeatureWindow window;
    if (frontend.takeWindow(window) && first.features.empty())
      first = std::move(window);
    offset += block.sample_count;
  }
  return first;
}

double featureEnergy(const CwCharacterFeatureWindow& window) {
  double result = 0.0;
  for (const float feature : window.features)
    result += std::expm1(static_cast<double>(feature));
  return result;
}

void testFrontendContractAndPartitioning() {
  using cwassistant::core::CwCharacterFeatureContract;
  CwCharacterLaneFrontend compatible;
  expect(compatible.valid(), "default character frontend contract is valid");
  expect(compatible.frequencyBinCount() == 65U,
         "default 3200/256 contract exposes inclusive 400-1200 Hz bins");

  auto invalid = CwCharacterFeatureContract{};
  invalid.hop_length = 0;
  expect(!compatible.configure(invalid),
         "invalid model feature contract is rejected");

  CwCharacterFeatureContract contract;
  contract.window_seconds = 5.0;
  contract.lane_filter_taps = 129;
  CwCharacterLaneFrontend whole(contract);
  CwCharacterLaneFrontend partitioned(contract);
  whole.reset({.track_id = 7, .track_generation = 2}, 800.0);
  partitioned.reset({.track_id = 7, .track_generation = 2}, 800.0);
  const auto left = runTone(whole, 800.0, 4'096, 5.5);
  const auto right = runTone(partitioned, 800.0, 317, 5.5);
  expect(!left.features.empty() && !right.features.empty(),
         "frontend emits a bounded feature window");
  expect(left.frame_count == right.frame_count &&
             left.frequency_bins == 65U,
         "block partitioning preserves feature tensor shape");
  expect(left.frame_timestamps_ns == right.frame_timestamps_ns,
         "block partitioning preserves acoustic frame timestamps");
  double maximum_difference = 0.0;
  for (std::size_t index = 0;
       index < std::min(left.features.size(), right.features.size()); ++index) {
    maximum_difference = std::max(
        maximum_difference,
        std::abs(static_cast<double>(left.features[index] -
                                    right.features[index])));
  }
  expect(maximum_difference < 1.0e-5,
         "block partitioning preserves STFT features");
  expect(left.track.track_id == 7 && left.track.track_generation == 2 &&
             left.track.frontend_generation == 0,
         "feature windows retain track and frontend generations");
}

void testFrontendNarrowLaneAndBackpressure() {
  using cwassistant::core::CwCharacterFeatureContract;
  CwCharacterFeatureContract contract;
  contract.window_seconds = 5.0;
  contract.stride_seconds = 0.5;
  contract.lane_bandwidth_hz = 30.0;
  contract.lane_filter_taps = 1'025;
  CwCharacterLaneFrontend target(contract);
  CwCharacterLaneFrontend adjacent(contract);
  target.reset({.track_id = 1}, 800.0);
  adjacent.reset({.track_id = 2}, 800.0);
  const auto target_window = runTone(target, 800.0, 1'024, 5.5);
  const auto adjacent_window = runTone(adjacent, 829.0, 1'024, 5.5);
  expect(featureEnergy(target_window) > 8.0 * featureEnergy(adjacent_window),
         "30 Hz lane suppresses a carrier 29 Hz from its fixed center");

  CwCharacterLaneFrontend slow_consumer(contract);
  slow_consumer.reset({.track_id = 3}, 800.0);
  static_cast<void>(runTone(slow_consumer, 800.0, 4'096, 7.0));
  expect(slow_consumer.droppedWindows() > 0,
         "latest feature window replaces stale pending work without blocking");
  expect(slow_consumer.stateBytes() < 1U * 1'024U * 1'024U,
         "one configured lane stays within its explicit one MiB state guard");
}

cwassistant::core::CwCharacterHypothesis hypothesis(
    const std::uint64_t sequence, const std::uint64_t shift_ns,
    const std::string& text, const float confidence = 0.9F,
    CwCharacterTrackKey track = {.track_id = 9, .track_generation = 1,
                                 .frontend_generation = 3}) {
  cwassistant::core::CwCharacterHypothesis result{
      .track = track,
      .window_sequence = sequence,
      .window_started_ns = 0,
      .window_ended_ns = 2'000'000'000ULL,
      .valid_started_ns = 50'000'000ULL,
      .valid_ended_ns = 1'950'000'000ULL,
  };
  std::uint64_t at = 200'000'000ULL + shift_ns;
  for (const char symbol : text) {
    result.characters.push_back(CwTimedCharacter{
        .symbol = symbol,
        .started_ns = at,
        .ended_ns = at + 30'000'000ULL,
        .confidence = confidence,
    });
    at += 100'000'000ULL;
  }
  return result;
}

void testTimestampConsensusAndAppendOnlyText() {
  using cwassistant::core::CwCharacterConsensusMerger;
  CwCharacterConsensusMerger merger;
  const auto first = merger.process(hypothesis(1, 0, "CQ EE"));
  expect(first.stable_text.empty() && first.provisional_text == "CQ EE",
         "one character window remains provisional");
  const auto second = merger.process(hypothesis(2, 40'000'000ULL, "CQ EE"));
  expect(second.stable_text == "CQ EE" && second.provisional_text.empty(),
         "two time-aligned windows commit repeated characters and space once");

  const auto third = merger.process(hypothesis(3, 20'000'000ULL, "CQ EEA"));
  expect(third.stable_text == "CQ EE" && third.provisional_text == "A",
         "a new one-window suffix cannot rewrite or extend stable text");
  const auto fourth = merger.process(hypothesis(4, 50'000'000ULL, "CQ EEA"));
  expect(fourth.stable_text == "CQ EEA",
         "a later aligned overlap appends the confirmed suffix");
  expect(merger.stateBytes() < 64U * 1'024U,
         "bounded consensus history stays compact");
}

void testConsensusRejectsUncertainAndStaleResults() {
  using cwassistant::core::CwCharacterConsensusMerger;
  CwCharacterConsensusMerger merger;
  static_cast<void>(merger.process(hypothesis(1, 0, "A")));
  const auto displaced = merger.process(
      hypothesis(2, 70'000'000ULL, "A"));
  expect(displaced.stable_text.empty(),
         "a character displaced beyond 60 ms remains provisional");
  const auto stale = merger.process(hypothesis(1, 0, "A"));
  expect(!stale.changed && stale.stable_text.empty(),
         "out-of-order inference result is ignored");
  CwCharacterConsensusMerger low_confidence;
  static_cast<void>(low_confidence.process(hypothesis(1, 0, "K", 0.6F)));
  const auto low = low_confidence.process(
      hypothesis(2, 20'000'000ULL, "K", 0.6F));
  expect(low.stable_text.empty(),
         "aligned low-confidence characters remain provisional");

  CwCharacterConsensusMerger unsafe_edge;
  auto edge_one = hypothesis(1, 0, "Z");
  auto edge_two = hypothesis(2, 10'000'000ULL, "Z");
  edge_one.valid_started_ns = 250'000'000ULL;
  edge_two.valid_started_ns = 250'000'000ULL;
  static_cast<void>(unsafe_edge.process(std::move(edge_one)));
  const auto edge = unsafe_edge.process(std::move(edge_two));
  expect(edge.stable_text.empty(),
         "characters inside unsafe model context never become stable");

  CwCharacterConsensusMerger bounded({.maximum_characters_per_window = 32});
  auto oversized = hypothesis(1, 0, std::string(33, 'E'));
  const auto rejected = bounded.process(std::move(oversized));
  expect(!rejected.changed && rejected.stable_text.empty(),
         "oversized backend output is rejected before alignment allocation");
}

void testRepeatedModerateConfidenceCallsignCharacters() {
  using cwassistant::core::CwCharacterConsensusMerger;
  CwCharacterConsensusMerger merger;
  auto first = hypothesis(1, 0, "EM90ZMV");
  auto second = hypothesis(2, 10'000'000ULL, "EM90ZMV");
  auto third = hypothesis(3, 20'000'000ULL, "EM90ZMV");
  for (auto* candidate : {&first, &second, &third}) {
    candidate->characters[4].confidence = 0.63F;
    candidate->characters[5].confidence = 0.58F;
  }

  static_cast<void>(merger.process(std::move(first)));
  const auto held = merger.process(std::move(second));
  expect(held.stable_text == "EM90" && held.provisional_text == "ZMV",
         "two moderate-confidence windows do not prematurely commit a call");
  const auto committed = merger.process(std::move(third));
  expect(committed.stable_text == "EM90ZMV" &&
             committed.provisional_text.empty(),
         "three time-aligned windows recover repeated moderate-confidence "
         "callsign characters without splitting the token");
  expect(committed.stable_text.find("EM9090ZMV") == std::string::npos,
         "overlap consensus does not duplicate a confirmed callsign prefix");
}

void testConsensusGenerationOrdering() {
  using cwassistant::core::CwCharacterConsensusMerger;
  const CwCharacterTrackKey original{
      .track_id = 9, .track_generation = 2, .frontend_generation = 3};
  CwCharacterConsensusMerger merger;
  static_cast<void>(merger.process(hypothesis(1, 0, "A", 0.9F, original)));
  const auto committed = merger.process(
      hypothesis(2, 10'000'000ULL, "A", 0.9F, original));
  expect(committed.stable_text == "A",
         "the original generation commits normally");

  const CwCharacterTrackKey newer_track{
      .track_id = 9, .track_generation = 3, .frontend_generation = 1};
  const auto restarted = merger.process(
      hypothesis(1, 0, "N", 0.9F, newer_track));
  expect(restarted.changed && restarted.stable_text.empty() &&
             restarted.provisional_text == "N",
         "a newer track generation starts a fresh consensus sequence");
  const auto recommitted = merger.process(
      hypothesis(2, 10'000'000ULL, "N", 0.9F, newer_track));
  expect(recommitted.stable_text == "N",
         "the newer track generation can commit independently");

  const auto delayed_old = merger.process(
      hypothesis(99, 0, "Z", 0.9F, original));
  expect(!delayed_old.changed && delayed_old.stable_text == "N",
         "a delayed older track generation cannot roll consensus backward");

  const CwCharacterTrackKey newer_frontend{
      .track_id = 9, .track_generation = 3, .frontend_generation = 2};
  const auto frontend_restart = merger.process(
      hypothesis(1, 0, "K", 0.9F, newer_frontend));
  expect(frontend_restart.changed && frontend_restart.stable_text.empty() &&
             frontend_restart.provisional_text == "K",
         "a newer frontend incarnation starts a fresh consensus sequence");
  const auto stale_frontend = merger.process(
      hypothesis(99, 0, "X", 0.9F, newer_track));
  expect(!stale_frontend.changed && stale_frontend.stable_text.empty() &&
             stale_frontend.provisional_text == "K",
         "an older frontend incarnation is rejected after reacquisition");
}

}  // namespace

int main() {
  testFrontendContractAndPartitioning();
  testFrontendNarrowLaneAndBackpressure();
  testTimestampConsensusAndAppendOnlyText();
  testConsensusRejectsUncertainAndStaleResults();
  testRepeatedModerateConfidenceCallsignCharacters();
  testConsensusGenerationOrdering();
  if (failures == 0) {
    std::cout << "CW character refinement tests passed\n";
    return 0;
  }
  std::cerr << failures << " CW character refinement test(s) failed\n";
  return 1;
}
