#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cwassistant/core/cw_spot_registry.hpp"

namespace cwassistant::core {

// External data can describe activity or directory membership, but it is not
// acoustic proof. Keep the distinction explicit in every presented result.
enum class CallsignEvidenceKind {
  ActivityList,
  DirectoryListing,
  LicenseRecord,
  FrequencyTimeSpot,
};

struct CallsignRawHypothesis {
  // The decoder-owned span is retained verbatim. Ranking never rewrites it.
  std::string raw_span;
  // A complete callsign candidate produced by an acoustic/segmentation path.
  std::string candidate;
  // Unitless relative support. It is useful for ordering alternatives but is
  // not a calibrated probability or a statement of callsign certainty.
  float acoustic_support{0.0F};
  float acoustic_edit_cost{0.0F};
};

struct CallsignProviderEvidence {
  std::string candidate;
  std::string provider_id;
  std::string provider_label;
  CallsignEvidenceKind kind{CallsignEvidenceKind::ActivityList};
  // Provider weights are deliberately small and are capped again by the
  // ranker. A provider cannot turn weak audio into a confident decode.
  float requested_weight{0.0F};
  std::string dataset_version;
  std::string dataset_sha256;
  std::chrono::system_clock::time_point retrieved_at{};
  std::optional<std::chrono::system_clock::time_point> observed_at;
  std::optional<std::uint64_t> frequency_hz;
  std::optional<std::int64_t> frequency_delta_hz;
  std::optional<std::chrono::seconds> age;
  std::string mode;
  std::string spotter;
  std::string rationale;
};

// What other receivers reported near the frequency a span was decoded on, as
// the spot registry hands it over. Supplying nothing is the ordinary case:
// most real contacts are never spotted, so an empty list says nothing at all
// about any candidate and never counts against one.
struct CallsignCorroborationInput {
  std::vector<CwSpotMatch> spots;
  // The receive frequency the span was decoded on. A non-positive or
  // non-finite value disables corroboration entirely, because "near the same
  // frequency" cannot be checked without knowing where the decode happened.
  double decode_frequency_hz{0.0};
  // A clock reading on the same scale as CwSpotMatch::newest_observation_ns,
  // used to age each observation.
  std::uint64_t now_ns{0};
};

// How an external observation supported one ranked candidate. It is reported
// apart from the acoustic fields and apart from provider provenance so that a
// caller can always tell the operator which part of a suggestion was heard on
// the air and which part is only somebody else's receiver agreeing.
//
// Corroboration can lower how much of its own evidence a decoded callsign
// needs before it is offered. It can never supply a callsign: a candidate that
// no acoustic hypothesis produced is never ranked, whatever is spotted.
struct CallsignCorroboration {
  bool corroborated{false};
  bool reverse_beacon{false};
  bool cluster{false};
  std::size_t observations{0};
  std::chrono::seconds age{0};
  // Signed offset of the observation from the decoded frequency, for display.
  std::int64_t frequency_delta_hz{0};
  // The support actually added to the ranking score, after every cap. It
  // spends the same bounded external budget as provider weight, so it is zero
  // once providers have used that budget up.
  float applied_weight{0.0F};
  // One operator-facing sentence saying why the support was granted.
  std::string rationale;
};

struct CallsignSuggestion {
  std::string raw_span;
  std::string candidate;
  float acoustic_support{0.0F};
  float acoustic_edit_cost{0.0F};
  float provider_weight{0.0F};
  // Relative ordering score only; deliberately not named confidence.
  float ranking_score{0.0F};
  std::vector<CallsignProviderEvidence> provenance;
  // External agreement, kept out of provider_weight and out of provenance so
  // acoustic evidence and outside opinion stay separable at the call site.
  CallsignCorroboration corroboration;
};

struct CallsignRankConfig {
  std::size_t maximum_hypotheses{32};
  std::size_t maximum_suggestions{8};
  std::size_t maximum_provider_records{256};
  std::size_t maximum_provenance_per_suggestion{16};
  std::size_t maximum_unknown_characters{2};
  // Wildcards are free. Any known-character substitution, insertion, or
  // deletion consumes one edit. Exact matching remains the default so each
  // presentation/provider boundary must opt in deliberately.
  std::size_t maximum_span_edit_distance{0};
  float minimum_acoustic_support{0.20F};
  float maximum_acoustic_edit_cost{2.0F};
  float acoustic_edit_cost_weight{0.10F};
  float maximum_weight_per_provider{0.06F};
  float maximum_total_provider_weight{0.12F};
  std::chrono::seconds maximum_spot_age{120};
  std::uint64_t maximum_spot_frequency_delta_hz{250};
};

// Accepts a bounded, callsign-shaped decoder span containing zero or more '?'
// characters. This is deliberately not a registry-validity check.
[[nodiscard]] bool is_callsign_like_span(const std::string& span,
                                         std::size_t maximum_unknowns = 2);

// Ranks already-generated acoustic alternatives. It does not generate missing
// letters without an acoustic candidate, query providers, mutate raw text, or
// treat absence as negative evidence. A caller may explicitly permit a bounded
// wildcard-aware edit distance between the raw span and an acoustic candidate.
// Provider evidence only supplies a capped positive weight.
[[nodiscard]] std::vector<CallsignSuggestion> rank_callsign_suggestions(
    const std::string& raw_span,
    const std::vector<CallsignRawHypothesis>& hypotheses,
    const std::vector<CallsignProviderEvidence>& provider_evidence,
    CallsignRankConfig config = {});

// The same ranking, additionally allowing an already-ranked candidate to be
// corroborated by an external observation of the same callsign near the same
// frequency. Corroboration only adjusts how much of its own evidence a decoded
// callsign needs before it is offered. It never introduces a candidate, never
// rewrites a character, and cannot lift a candidate that has no acoustic
// support, because only acoustic hypotheses are ever ranked in the first
// place. Its influence spends the same maximum_total_provider_weight budget as
// provider evidence, so no external channel gains influence merely by arriving
// through a different door, and a candidate absent from every spot is never
// penalised.
//
// The corroboration argument follows the configuration rather than preceding
// it so that an existing four-argument call with a braced configuration stays
// unambiguous.
[[nodiscard]] std::vector<CallsignSuggestion> rank_callsign_suggestions(
    const std::string& raw_span,
    const std::vector<CallsignRawHypothesis>& hypotheses,
    const std::vector<CallsignProviderEvidence>& provider_evidence,
    CallsignRankConfig config,
    const CallsignCorroborationInput& corroboration);

}  // namespace cwassistant::core
