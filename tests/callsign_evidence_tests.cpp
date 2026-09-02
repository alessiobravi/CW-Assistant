#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cwassistant/core/callsign_evidence.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::chrono::system_clock::time_point testTime(const int seconds) {
  return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

cwassistant::core::CallsignProviderEvidence activityEvidence(
    std::string candidate, std::string provider_id = "activity-list") {
  using namespace cwassistant::core;
  CallsignProviderEvidence evidence;
  evidence.candidate = std::move(candidate);
  evidence.provider_id = std::move(provider_id);
  evidence.provider_label = "Activity list";
  evidence.kind = CallsignEvidenceKind::ActivityList;
  evidence.requested_weight = 0.06F;
  evidence.dataset_version = "2026-09-02";
  evidence.dataset_sha256 = std::string(64, 'a');
  evidence.retrieved_at = testTime(1'000);
  evidence.rationale = "masked callsign match";
  return evidence;
}

cwassistant::core::CallsignProviderEvidence spotEvidence(
    std::string candidate) {
  using namespace cwassistant::core;
  CallsignProviderEvidence evidence;
  evidence.candidate = std::move(candidate);
  evidence.provider_id = "rbn";
  evidence.provider_label = "Beacon reports";
  evidence.kind = CallsignEvidenceKind::FrequencyTimeSpot;
  evidence.requested_weight = 0.06F;
  evidence.retrieved_at = testTime(1'000);
  evidence.observed_at = testTime(990);
  evidence.frequency_hz = 14'023'400;
  evidence.frequency_delta_hz = 42;
  evidence.age = std::chrono::seconds{10};
  evidence.mode = "cw";
  evidence.spotter = "ol7m";
  evidence.rationale = "recent CW report near checked RF";
  return evidence;
}

void test_span_validation() {
  using cwassistant::core::is_callsign_like_span;
  expect(is_callsign_like_span(" EA?EYL "),
         "one unknown digit position remains callsign-like");
  expect(is_callsign_like_span("3DA?RU/P"),
         "numeric international prefixes and modifiers remain eligible");
  expect(is_callsign_like_span("EA8/W?AW"),
         "operating prefixes before a compound callsign remain eligible");
  expect(!is_callsign_like_span("HELLO"),
         "ordinary words are not callsign suggestion spans");
  expect(!is_callsign_like_span("EA???YL"), "unknown count is bounded");
  expect(!is_callsign_like_span("EA?EYL/"),
         "unfinished modifiers are not suggestion spans");
}

void test_only_unknown_substitution_is_allowed() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.80F,
       .acoustic_edit_cost = 0.2F},
      {.raw_span = raw, .candidate = "EB1EYL", .acoustic_support = 0.99F,
       .acoustic_edit_cost = 0.1F},
      {.raw_span = raw, .candidate = "EA11EYL", .acoustic_support = 0.99F,
       .acoustic_edit_cost = 0.1F},
      {.raw_span = raw, .candidate = "EA1EY", .acoustic_support = 0.99F,
       .acoustic_edit_cost = 0.1F},
      {.raw_span = raw, .candidate = "EA1/EYL", .acoustic_support = 0.99F,
       .acoustic_edit_cost = 0.1F},
  };
  const auto ranked = rank_callsign_suggestions(raw, hypotheses, {});
  expect(ranked.size() == 1 && ranked.front().candidate == "EA1EYL",
         "known characters, length, and slash positions cannot be rewritten");

  const std::string compound_raw = "EA8/W?AW";
  const auto compound = rank_callsign_suggestions(
      compound_raw,
      {{.raw_span = compound_raw, .candidate = "EA8/W1AW",
        .acoustic_support = 0.8F, .acoustic_edit_cost = 0.2F}},
      {});
  expect(compound.size() == 1 && compound.front().candidate == "EA8/W1AW",
         "literal substitution retains valid compound callsigns");
}

void test_provider_weights_are_bounded_and_canonicalized() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.86F,
       .acoustic_edit_cost = 0.2F},
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.4F},
  };
  auto first = activityEvidence("EA7EYL", " Directory ");
  first.requested_weight = 1.0F;
  auto duplicate = activityEvidence("EA7EYL", "DIRECTORY");
  duplicate.requested_weight = 1.0F;
  auto spot = spotEvidence("EA7EYL");
  spot.requested_weight = 1.0F;
  const auto ranked =
      rank_callsign_suggestions(raw, hypotheses, {first, duplicate, spot});
  expect(ranked.size() == 2, "both bounded acoustic alternatives are retained");
  expect(ranked.front().candidate == "EA1EYL",
         "providers cannot overturn a decisive acoustic margin");
  expect(std::abs(ranked[1].provider_weight - 0.12F) < 0.0001F,
         "total provider influence is capped");
  expect(ranked[1].provenance.size() == 3,
         "valid source records remain visible as provenance");
  expect(ranked[1].provenance.front().provider_id == "directory",
         "provider identifiers are canonicalized for scoring and display");
}

void test_edit_cost_affects_rank_and_is_bounded() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.75F,
       .acoustic_edit_cost = 0.2F},
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.75F,
       .acoustic_edit_cost = 1.2F},
      {.raw_span = raw, .candidate = "EA9EYL", .acoustic_support = 1.0F,
       .acoustic_edit_cost = 99.0F},
  };
  const auto ranked = rank_callsign_suggestions(raw, hypotheses, {});
  expect(ranked.size() == 2 && ranked.front().candidate == "EA1EYL",
         "lower acoustic edit cost materially improves relative rank");
  expect(ranked[0].ranking_score > ranked[1].ranking_score,
         "ranking score includes the acoustic edit penalty");
}

void test_invalid_spots_supply_no_weight() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.3F},
  };
  auto stale = spotEvidence("EA1EYL");
  stale.age = std::chrono::seconds{121};
  auto far = spotEvidence("EA1EYL");
  far.frequency_delta_hz = 251;
  auto wrong_mode = spotEvidence("EA1EYL");
  wrong_mode.mode = "RTTY";
  auto incomplete = spotEvidence("EA1EYL");
  incomplete.spotter.clear();
  const auto ranked = rank_callsign_suggestions(
      raw, hypotheses, {stale, far, wrong_mode, incomplete});
  expect(ranked.size() == 1 && ranked.front().provider_weight == 0.0F,
         "stale, far, wrong-mode, and incomplete spots add no weight");
  expect(ranked.front().provenance.empty(),
         "non-contributing spot records are not presented as evidence");
}

void test_provider_metadata_is_required() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.3F},
  };
  auto invalid_hash = activityEvidence("EA1EYL");
  invalid_hash.dataset_sha256 = "not-a-sha256";
  auto missing_time = activityEvidence("EA1EYL");
  missing_time.retrieved_at = {};
  auto invalid_id = activityEvidence("EA1EYL", "provider id with spaces");
  auto oversized = activityEvidence("EA1EYL");
  oversized.rationale = std::string(257, 'x');
  const auto ranked = rank_callsign_suggestions(
      raw, hypotheses, {invalid_hash, missing_time, invalid_id, oversized});
  expect(ranked.front().provider_weight == 0.0F &&
             ranked.front().provenance.empty(),
         "unversioned or malformed provider records cannot affect ranking");
}

void test_processing_and_output_are_hard_bounded() {
  using namespace cwassistant::core;
  const std::string raw = "W?AW";
  std::vector<CallsignRawHypothesis> hypotheses;
  for (int digit = 0; digit <= 9; ++digit) {
    hypotheses.push_back({.raw_span = raw,
                          .candidate = "W" + std::to_string(digit) + "AW",
                          .acoustic_support = 0.5F,
                          .acoustic_edit_cost = 0.5F});
  }
  std::vector<CallsignProviderEvidence> evidence;
  for (int index = 0; index < 10; ++index) {
    evidence.push_back(activityEvidence(
        "W0AW", "provider-" + std::to_string(index)));
  }
  CallsignRankConfig config;
  config.maximum_hypotheses = 5;
  config.maximum_suggestions = 3;
  config.maximum_provider_records = 4;
  config.maximum_provenance_per_suggestion = 2;
  const auto ranked =
      rank_callsign_suggestions(raw, hypotheses, evidence, config);
  expect(ranked.size() == 3, "suggestion output obeys its hard bound");
  expect(ranked[0].candidate == "W0AW" && ranked[1].candidate == "W1AW" &&
             ranked[2].candidate == "W2AW",
         "only the bounded acoustic input prefix is examined deterministically");
  expect(ranked[0].provenance.size() == 2,
         "retained provenance obeys its independent hard bound");
  expect(std::abs(ranked[0].provider_weight - 0.12F) < 0.0001F,
         "bounded provider processing still observes the global weight cap");
}

void test_non_callsign_input_and_database_absence() {
  using namespace cwassistant::core;
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = "REPORT", .candidate = "REPORT", .acoustic_support = 0.9F,
       .acoustic_edit_cost = 0.0F},
  };
  expect(rank_callsign_suggestions("REPORT", hypotheses, {}).empty(),
         "provider matching never runs over non-callsign words");

  const std::string raw = "EA?EYL";
  const auto without_provider = rank_callsign_suggestions(
      raw,
      {{.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.7F,
        .acoustic_edit_cost = 0.2F}},
      {});
  expect(without_provider.size() == 1 &&
             without_provider.front().provider_weight == 0.0F,
         "absence from provider data is not negative evidence");
}

}  // namespace

int main() {
  test_span_validation();
  test_only_unknown_substitution_is_allowed();
  test_provider_weights_are_bounded_and_canonicalized();
  test_edit_cost_affects_rank_and_is_bounded();
  test_invalid_spots_supply_no_weight();
  test_provider_metadata_is_required();
  test_processing_and_output_are_hard_bounded();
  test_non_callsign_input_and_database_absence();
  if (failures != 0) {
    std::cerr << failures << " callsign evidence test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Callsign evidence tests passed\n";
  return EXIT_SUCCESS;
}
