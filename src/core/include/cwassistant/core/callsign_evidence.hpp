#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

struct CallsignSuggestion {
  std::string raw_span;
  std::string candidate;
  float acoustic_support{0.0F};
  float acoustic_edit_cost{0.0F};
  float provider_weight{0.0F};
  // Relative ordering score only; deliberately not named confidence.
  float ranking_score{0.0F};
  std::vector<CallsignProviderEvidence> provenance;
};

struct CallsignRankConfig {
  std::size_t maximum_hypotheses{32};
  std::size_t maximum_suggestions{8};
  std::size_t maximum_provider_records{256};
  std::size_t maximum_provenance_per_suggestion{16};
  std::size_t maximum_unknown_characters{2};
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
// letters beyond literal '?' substitution, query providers, mutate raw text,
// or treat absence as negative evidence. Provider evidence only supplies a
// capped positive weight.
[[nodiscard]] std::vector<CallsignSuggestion> rank_callsign_suggestions(
    const std::string& raw_span,
    const std::vector<CallsignRawHypothesis>& hypotheses,
    const std::vector<CallsignProviderEvidence>& provider_evidence,
    CallsignRankConfig config = {});

}  // namespace cwassistant::core
