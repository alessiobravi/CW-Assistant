#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
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

// A clock reading far enough from zero that an observation can be aged
// backwards from it without underflowing the unsigned nanosecond stamp.
constexpr std::uint64_t kNowNs = 3'600ULL * 1'000'000'000ULL;
constexpr double kDecodeFrequencyHz = 14'023'400.0;
// Comfortably inside the default 250 Hz corroboration window.
constexpr double kSpotOffsetHz = 40.0;

cwassistant::core::CwSpotMatch testSpot(std::string callsign,
                                        const bool reverse_beacon,
                                        const bool cluster,
                                        const int age_seconds) {
  using namespace cwassistant::core;
  CwSpotMatch match;
  match.callsign = std::move(callsign);
  match.frequency_hz = kDecodeFrequencyHz + kSpotOffsetHz;
  match.reverse_beacon = reverse_beacon;
  match.cluster = cluster;
  match.newest_observation_ns =
      kNowNs - static_cast<std::uint64_t>(age_seconds) * 1'000'000'000ULL;
  match.observations = 1;
  return match;
}

cwassistant::core::CallsignCorroborationInput testCorroboration(
    std::vector<cwassistant::core::CwSpotMatch> spots) {
  using namespace cwassistant::core;
  CallsignCorroborationInput input;
  input.spots = std::move(spots);
  input.decode_frequency_hz = kDecodeFrequencyHz;
  input.now_ns = kNowNs;
  return input;
}

const cwassistant::core::CallsignSuggestion* findSuggestion(
    const std::vector<cwassistant::core::CallsignSuggestion>& ranked,
    const std::string& candidate) {
  const auto found = std::find_if(
      ranked.cbegin(), ranked.cend(), [&candidate](const auto& suggestion) {
        return suggestion.candidate == candidate;
      });
  return found == ranked.cend() ? nullptr : &*found;
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

  CallsignRankConfig fuzzy_config;
  fuzzy_config.maximum_span_edit_distance = 2;
  const auto fuzzy_ranked =
      rank_callsign_suggestions(raw, hypotheses, {}, fuzzy_config);
  expect(fuzzy_ranked.size() == 4 &&
             std::any_of(fuzzy_ranked.cbegin(), fuzzy_ranked.cend(),
                         [](const auto& suggestion) {
               return suggestion.candidate == "EB1EYL";
             }) &&
             std::any_of(fuzzy_ranked.cbegin(), fuzzy_ranked.cend(),
                         [](const auto& suggestion) {
               return suggestion.candidate == "EA11EYL";
             }) &&
             std::any_of(fuzzy_ranked.cbegin(), fuzzy_ranked.cend(),
                         [](const auto& suggestion) {
               return suggestion.candidate == "EA1EY";
             }),
         "explicit bounded alignment admits acoustic substitutions, "
         "insertions, and deletions without changing the exact default");

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


void test_corroboration_lifts_a_match_within_the_cap() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  // A margin of 0.02 is narrower than the external budget, which is exactly
  // the situation corroboration is allowed to decide.
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.0F},
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.0F},
  };
  const CallsignRankConfig config;
  const auto uncorroborated =
      rank_callsign_suggestions(raw, hypotheses, {}, config, {});
  expect(uncorroborated.size() == 2 &&
             uncorroborated.front().candidate == "EA1EYL",
         "acoustic support alone decides the order without any observation");

  const auto ranked = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 0)}));
  expect(ranked.size() == 2 && ranked.front().candidate == "EA7EYL",
         "a corroborated candidate can overturn a margin smaller than the "
         "external budget");

  const auto* const lifted = findSuggestion(ranked, "EA7EYL");
  expect(lifted != nullptr && lifted->corroboration.corroborated,
         "the corroborated candidate reports that it was corroborated");
  expect(lifted != nullptr &&
             lifted->corroboration.applied_weight <=
                 config.maximum_weight_per_provider + 0.0001F,
         "a single external channel cannot exceed the per-provider cap");
  expect(lifted != nullptr &&
             lifted->corroboration.applied_weight +
                     lifted->provider_weight <=
                 config.maximum_total_provider_weight + 0.0001F,
         "acoustic and external influence together respect the total cap");
  expect(lifted != nullptr && lifted->corroboration.reverse_beacon &&
             lifted->corroboration.cluster &&
             lifted->corroboration.observations == 1 &&
             lifted->corroboration.frequency_delta_hz == 40 &&
             !lifted->corroboration.rationale.empty(),
         "corroboration is reported in enough detail to show the operator why");
  expect(lifted != nullptr && lifted->provenance.empty(),
         "external agreement is reported apart from provider provenance so "
         "acoustic and outside evidence stay distinguishable");
  expect(lifted != nullptr &&
             std::abs(lifted->ranking_score - lifted->acoustic_support -
                      lifted->corroboration.applied_weight) < 0.0001F,
         "the ranking score moves by exactly the reported external support");
}

void test_agreement_from_both_sources_outweighs_one() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.0F},
  };
  const CallsignRankConfig config;
  const auto both = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 0)}));
  const auto beacon_only = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, false, 0)}));
  const auto cluster_only = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", false, true, 0)}));
  expect(both.size() == 1 && beacon_only.size() == 1 &&
             cluster_only.size() == 1,
         "corroboration never changes how many acoustic candidates exist");
  expect(both.front().corroboration.applied_weight >
                 beacon_only.front().corroboration.applied_weight &&
             both.front().corroboration.applied_weight >
                 cluster_only.front().corroboration.applied_weight,
         "two sources that fail differently are worth more than either alone");
  expect(std::abs(beacon_only.front().corroboration.applied_weight -
                  cluster_only.front().corroboration.applied_weight) < 0.0001F,
         "neither single source is privileged over the other");
  expect(both.front().corroboration.applied_weight <=
             config.maximum_weight_per_provider + 0.0001F,
         "agreement between both sources still stays inside the existing cap");
  expect(beacon_only.front().corroboration.applied_weight > 0.0F,
         "one source still supplies some support");
}

void test_corroboration_decays_with_observation_age() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.0F},
  };
  const CallsignRankConfig config;
  const auto fresh = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 0)}));
  const auto middling = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 60)}));
  const auto expired = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 121)}));
  expect(fresh.front().corroboration.applied_weight >
             middling.front().corroboration.applied_weight,
         "support falls as the observation ages");
  expect(middling.front().corroboration.applied_weight > 0.0F,
         "an observation inside the retention window still counts");
  expect(middling.front().corroboration.age == std::chrono::seconds{60},
         "the reported age is the age of the newest observation");
  expect(!expired.front().corroboration.corroborated &&
             expired.front().corroboration.applied_weight == 0.0F,
         "an observation past the spot age limit supplies nothing");
  expect(std::abs(expired.front().ranking_score -
                  fresh.front().acoustic_support) < 0.0001F,
         "an expired observation leaves the acoustic score exactly as it was");
}

void test_an_unspotted_candidate_is_never_penalised() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.2F},
  };
  const CallsignRankConfig config;
  const auto without_spots =
      rank_callsign_suggestions(raw, hypotheses, {}, config, {});
  // Somebody else is spotted on this frequency. That says nothing whatever
  // about this candidate: most real contacts are never spotted at all.
  const auto with_other_spots = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 0)}));
  expect(without_spots.size() == 1 && with_other_spots.size() == 1,
         "an unmatched observation neither adds nor removes a candidate");
  expect(std::abs(without_spots.front().ranking_score -
                  with_other_spots.front().ranking_score) < 0.0001F,
         "absence from the spot registry is not evidence of absence");
  expect(!with_other_spots.front().corroboration.corroborated &&
             with_other_spots.front().corroboration.applied_weight == 0.0F &&
             with_other_spots.front().corroboration.rationale.empty(),
         "an uncorroborated candidate reports no external support at all");
}

void test_a_spot_can_never_supply_a_callsign() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const CallsignRankConfig config;
  const auto corroboration =
      testCorroboration({testSpot("EA7EYL", true, true, 0)});

  // A station everybody else hears, that this receiver did not decode, must
  // not appear at all. A confidently wrong callsign is worse than none.
  const std::vector<CallsignRawHypothesis> only_other{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.0F},
  };
  const auto unheard = rank_callsign_suggestions(raw, only_other, {}, config,
                                                 corroboration);
  expect(unheard.size() == 1 && unheard.front().candidate == "EA1EYL" &&
             findSuggestion(unheard, "EA7EYL") == nullptr,
         "a spotted callsign that no acoustic hypothesis produced is never "
         "ranked");

  // Acoustic support below the floor is filtered before corroboration runs,
  // so a spot cannot rescue a candidate the audio does not support.
  const std::vector<CallsignRawHypothesis> barely_heard{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.70F,
       .acoustic_edit_cost = 0.0F},
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.10F,
       .acoustic_edit_cost = 0.0F},
  };
  const auto floored = rank_callsign_suggestions(raw, barely_heard, {}, config,
                                                 corroboration);
  expect(floored.size() == 1 && floored.front().candidate == "EA1EYL",
         "a spot cannot lift a candidate below the minimum acoustic support");

  // A decisive acoustic margin is wider than the whole external budget, so
  // corroboration adjusts how much evidence is needed and never replaces it.
  const std::vector<CallsignRawHypothesis> decisive{
      {.raw_span = raw, .candidate = "EA1EYL", .acoustic_support = 0.90F,
       .acoustic_edit_cost = 0.0F},
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.60F,
       .acoustic_edit_cost = 0.0F},
  };
  const auto ranked = rank_callsign_suggestions(raw, decisive, {}, config,
                                                corroboration);
  expect(ranked.size() == 2 && ranked.front().candidate == "EA1EYL",
         "a spot cannot overturn an acoustic margin wider than the cap");
  expect(ranked.front().candidate == "EA1EYL" &&
             !ranked.front().corroboration.corroborated,
         "the acoustic winner is not credited with somebody else's spot");

  // The spotted characters are never written into a decoded span.
  const auto rewritten = rank_callsign_suggestions(
      raw, only_other, {}, config,
      testCorroboration({testSpot("EB1EYL", true, true, 0)}));
  expect(rewritten.size() == 1 && rewritten.front().candidate == "EA1EYL" &&
             rewritten.front().raw_span == raw &&
             !rewritten.front().corroboration.corroborated,
         "a near-miss spot never rewrites a decoded character");
}

void test_corroboration_shares_the_bounded_external_budget() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.0F},
  };
  const CallsignRankConfig config;
  const auto directory = activityEvidence("EA7EYL", "directory");
  const auto beacon_list = activityEvidence("EA7EYL", "beacon-list");
  const auto ranked = rank_callsign_suggestions(
      raw, hypotheses, {directory, beacon_list}, config,
      testCorroboration({testSpot("EA7EYL", true, true, 0)}));
  expect(ranked.size() == 1, "the single acoustic candidate is retained");
  expect(std::abs(ranked.front().provider_weight - 0.12F) < 0.0001F,
         "two providers alone already spend the whole external budget");
  expect(ranked.front().corroboration.corroborated &&
             ranked.front().corroboration.applied_weight == 0.0F,
         "corroboration is still reported honestly once the budget is spent, "
         "but it opens no second budget of its own");
  expect(ranked.front().ranking_score <=
             ranked.front().acoustic_support +
                 config.maximum_total_provider_weight + 0.0001F,
         "every external channel together stays inside the total cap");
}

void test_corroboration_requires_a_usable_observation() {
  using namespace cwassistant::core;
  const std::string raw = "EA?EYL";
  const std::vector<CallsignRawHypothesis> hypotheses{
      {.raw_span = raw, .candidate = "EA7EYL", .acoustic_support = 0.68F,
       .acoustic_edit_cost = 0.0F},
  };
  const CallsignRankConfig config;

  auto distant = testSpot("EA7EYL", true, true, 0);
  distant.frequency_hz = kDecodeFrequencyHz + 400.0;
  const auto sourceless = testSpot("EA7EYL", false, false, 0);
  auto unobserved = testSpot("EA7EYL", true, true, 0);
  unobserved.observations = 0;
  auto from_the_future = testSpot("EA7EYL", true, true, 0);
  from_the_future.newest_observation_ns = kNowNs + 1'000'000'000ULL;
  const auto ranked = rank_callsign_suggestions(
      raw, hypotheses, {}, config,
      testCorroboration({distant, sourceless, unobserved, from_the_future}));
  expect(ranked.size() == 1 && !ranked.front().corroboration.corroborated &&
             ranked.front().corroboration.applied_weight == 0.0F,
         "an off-frequency, sourceless, empty, or future-stamped observation "
         "supplies no support");

  auto unknown_frequency =
      testCorroboration({testSpot("EA7EYL", true, true, 0)});
  unknown_frequency.decode_frequency_hz = 0.0;
  const auto without_frequency = rank_callsign_suggestions(
      raw, hypotheses, {}, config, unknown_frequency);
  expect(!without_frequency.front().corroboration.corroborated,
         "proximity cannot be checked without knowing the decode frequency, "
         "so nothing is credited");
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
  test_corroboration_lifts_a_match_within_the_cap();
  test_agreement_from_both_sources_outweighs_one();
  test_corroboration_decays_with_observation_age();
  test_an_unspotted_candidate_is_never_penalised();
  test_a_spot_can_never_supply_a_callsign();
  test_corroboration_shares_the_bounded_external_budget();
  test_corroboration_requires_a_usable_observation();
  if (failures != 0) {
    std::cerr << failures << " callsign evidence test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Callsign evidence tests passed\n";
  return EXIT_SUCCESS;
}
