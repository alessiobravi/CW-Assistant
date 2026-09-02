#include "cwassistant/core/callsign_evidence.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include <tuple>
#include <utility>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

constexpr std::size_t kAbsoluteHypothesisLimit = 256;
constexpr std::size_t kAbsoluteSuggestionLimit = 64;
constexpr std::size_t kAbsoluteProviderRecordLimit = 1'024;
constexpr std::size_t kAbsoluteProvenanceLimit = 64;

std::string trim(const std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string uppercase(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<char>(std::toupper(character)));
  }
  return result;
}

std::string canonicalProviderId(const std::string_view value) {
  std::string result = trim(value);
  if (result.empty() || result.size() > 64) return {};
  for (char& raw_character : result) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (std::isupper(character) != 0) {
      raw_character = static_cast<char>(std::tolower(character));
    } else if (std::islower(character) == 0 &&
               std::isdigit(character) == 0 && character != '-' &&
               character != '_' && character != '.') {
      return {};
    }
  }
  return result;
}

bool validSha256(const std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(),
                     [](const unsigned char character) {
                       return std::isxdigit(character) != 0;
                     });
}

bool hasTimestamp(const std::chrono::system_clock::time_point value) {
  return value.time_since_epoch() !=
         std::chrono::system_clock::duration::zero();
}

bool plausibleBase(const std::string_view base) {
  if (base.size() < 3 || base.size() > 12) return false;
  const auto first_digit = base.find_first_of("0123456789");
  const auto last_digit = base.find_last_of("0123456789");
  if (first_digit == std::string_view::npos || last_digit + 1 >= base.size() ||
      base.size() - last_digit - 1 > 4) {
    return false;
  }
  if (!std::all_of(base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
                   base.end(), [](const unsigned char character) {
                     return std::isalpha(character) != 0;
                   })) {
    return false;
  }

  // Letter-leading special-event calls may contain one contiguous numeric
  // block. Numeric-leading international prefixes need another digit after
  // their initial prefix letters (for example 3DA0RU).
  if (first_digit > 0) {
    if (!std::all_of(base.begin(),
                     base.begin() + static_cast<std::ptrdiff_t>(first_digit),
                     [](const unsigned char character) {
                       return std::isalpha(character) != 0;
                     })) {
      return false;
    }
    if (!std::all_of(base.begin() + static_cast<std::ptrdiff_t>(first_digit),
                     base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
                     [](const unsigned char character) {
                       return std::isdigit(character) != 0;
                     })) {
      return false;
    }
  } else {
    const auto second_digit = base.find_first_of("0123456789", 1);
    if (second_digit == std::string_view::npos || second_digit <= 1 ||
        !std::all_of(base.begin() + 1,
                     base.begin() + static_cast<std::ptrdiff_t>(second_digit),
                     [](const unsigned char character) {
                       return std::isalpha(character) != 0;
                     }) ||
        !std::all_of(base.begin() + static_cast<std::ptrdiff_t>(second_digit),
                     base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
                     [](const unsigned char character) {
                       return std::isdigit(character) != 0;
                     })) {
      return false;
    }
  }

  return true;
}

bool plausibleModifier(const std::string_view modifier) {
  return !modifier.empty() && modifier.size() <= 4 &&
         std::all_of(modifier.begin(), modifier.end(),
                     [](const unsigned char character) {
                       return std::isalnum(character) != 0;
                     });
}

bool plausibleCompleteCallsign(const std::string_view value) {
  const auto normalized = CallsignPolicy::normalize(value);
  if (!normalized || *normalized != uppercase(value)) return false;

  const auto slash = normalized->find('/');
  if (slash == std::string::npos) return plausibleBase(*normalized);
  const auto left = std::string_view(*normalized).substr(0, slash);
  const auto right = std::string_view(*normalized).substr(slash + 1);
  return (plausibleBase(left) && plausibleModifier(right)) ||
         (plausibleModifier(left) && plausibleBase(right));
}

bool callsignLikeBase(const std::string_view base) {
  if (base.size() < 3 || base.size() > 12) return false;
  bool has_letter = false;
  bool has_digit_or_unknown = false;
  for (const unsigned char character : base) {
    has_letter = has_letter || std::isalpha(character) != 0;
    has_digit_or_unknown = has_digit_or_unknown ||
                           std::isdigit(character) != 0 || character == '?';
  }
  return has_letter && has_digit_or_unknown &&
         (std::isalpha(static_cast<unsigned char>(base.back())) != 0 ||
          base.back() == '?');
}

bool callsignLikeModifier(const std::string_view modifier) {
  return !modifier.empty() && modifier.size() <= 4 &&
         std::all_of(modifier.begin(), modifier.end(),
                     [](const unsigned char character) {
                       return std::isalnum(character) != 0 || character == '?';
                     });
}

bool sameRawSpan(const std::string_view expected,
                 const std::string_view hypothesis) {
  return uppercase(trim(expected)) == uppercase(trim(hypothesis));
}

bool candidateMatchesRawSpan(const std::string_view raw_span,
                             const std::string_view candidate) {
  const std::string raw = uppercase(trim(raw_span));
  const std::string normalized_candidate = uppercase(trim(candidate));
  if (raw.size() != normalized_candidate.size()) return false;
  for (std::size_t index = 0; index < raw.size(); ++index) {
    if (raw[index] == '?') {
      const auto replacement =
          static_cast<unsigned char>(normalized_candidate[index]);
      if (std::isalnum(replacement) == 0) return false;
    } else if (raw[index] != normalized_candidate[index]) {
      return false;
    }
  }
  return true;
}

bool validProviderEvidence(
    const CallsignProviderEvidence& evidence,
    const CallsignRankConfig& config) {
  if (canonicalProviderId(evidence.provider_id).empty() ||
      trim(evidence.provider_label).empty() ||
      evidence.provider_label.size() > 96 ||
      trim(evidence.rationale).empty() || evidence.rationale.size() > 256 ||
      evidence.dataset_version.size() > 96 ||
      evidence.dataset_sha256.size() > 64 ||
      (!evidence.dataset_sha256.empty() &&
       !validSha256(evidence.dataset_sha256)) ||
      evidence.mode.size() > 16 || evidence.spotter.size() > 32 ||
      !std::isfinite(evidence.requested_weight) ||
      evidence.requested_weight <= 0.0F || !hasTimestamp(evidence.retrieved_at)) {
    return false;
  }

  switch (evidence.kind) {
    case CallsignEvidenceKind::ActivityList:
      return !trim(evidence.dataset_version).empty() &&
             evidence.dataset_version.size() <= 96 &&
             validSha256(evidence.dataset_sha256);
    case CallsignEvidenceKind::DirectoryListing:
      return true;
    case CallsignEvidenceKind::LicenseRecord:
      return !trim(evidence.dataset_version).empty() &&
             evidence.dataset_version.size() <= 96 &&
             validSha256(evidence.dataset_sha256);
    case CallsignEvidenceKind::FrequencyTimeSpot: {
      if (!evidence.observed_at || !hasTimestamp(*evidence.observed_at) ||
          !evidence.frequency_hz || *evidence.frequency_hz == 0 ||
          !evidence.frequency_delta_hz || !evidence.age ||
          evidence.age->count() < 0 || *evidence.age > config.maximum_spot_age ||
          uppercase(trim(evidence.mode)) != "CW" ||
          trim(evidence.spotter).empty() || evidence.spotter.size() > 32) {
        return false;
      }
      const auto maximum_delta =
          config.maximum_spot_frequency_delta_hz >
                  static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())
              ? std::numeric_limits<std::int64_t>::max()
              : static_cast<std::int64_t>(
                    config.maximum_spot_frequency_delta_hz);
      return *evidence.frequency_delta_hz >= -maximum_delta &&
             *evidence.frequency_delta_hz <= maximum_delta;
    }
  }
  return false;
}

}  // namespace

bool is_callsign_like_span(const std::string& span,
                           const std::size_t maximum_unknowns) {
  const std::string value = uppercase(trim(span));
  if (value.size() < 3 || value.size() > 16) return false;

  std::size_t slash_count = 0;
  std::size_t unknown_count = 0;
  bool has_letter = false;
  bool has_digit_or_unknown = false;
  for (const unsigned char character : value) {
    if (std::isalpha(character) != 0) {
      has_letter = true;
    } else if (std::isdigit(character) != 0) {
      has_digit_or_unknown = true;
    } else if (character == '?') {
      ++unknown_count;
      has_digit_or_unknown = true;
    } else if (character == '/') {
      ++slash_count;
    } else {
      return false;
    }
  }
  if (!has_letter || !has_digit_or_unknown || slash_count > 1 ||
      unknown_count > maximum_unknowns || value.front() == '/' ||
      value.back() == '/') {
    return false;
  }
  const auto slash = value.find('/');
  if (slash == std::string::npos) return callsignLikeBase(value);
  const auto left = std::string_view(value).substr(0, slash);
  const auto right = std::string_view(value).substr(slash + 1);
  return (callsignLikeBase(left) && callsignLikeModifier(right)) ||
         (callsignLikeModifier(left) && callsignLikeBase(right));
}

std::vector<CallsignSuggestion> rank_callsign_suggestions(
    const std::string& raw_span,
    const std::vector<CallsignRawHypothesis>& hypotheses,
    const std::vector<CallsignProviderEvidence>& provider_evidence,
    CallsignRankConfig config) {
  config.maximum_hypotheses =
      std::clamp<std::size_t>(config.maximum_hypotheses, 1,
                              kAbsoluteHypothesisLimit);
  config.maximum_suggestions =
      std::clamp<std::size_t>(config.maximum_suggestions, 1,
                              kAbsoluteSuggestionLimit);
  config.maximum_provider_records =
      std::clamp<std::size_t>(config.maximum_provider_records, 1,
                              kAbsoluteProviderRecordLimit);
  config.maximum_provenance_per_suggestion =
      std::clamp<std::size_t>(config.maximum_provenance_per_suggestion, 1,
                              kAbsoluteProvenanceLimit);
  config.maximum_unknown_characters =
      std::clamp<std::size_t>(config.maximum_unknown_characters, 0, 4);
  config.minimum_acoustic_support =
      std::clamp(config.minimum_acoustic_support, 0.0F, 1.0F);
  config.maximum_acoustic_edit_cost =
      std::clamp(config.maximum_acoustic_edit_cost, 0.0F, 8.0F);
  config.acoustic_edit_cost_weight =
      std::clamp(config.acoustic_edit_cost_weight, 0.0F, 1.0F);
  config.maximum_weight_per_provider =
      std::clamp(config.maximum_weight_per_provider, 0.0F, 0.25F);
  config.maximum_total_provider_weight =
      std::clamp(config.maximum_total_provider_weight, 0.0F, 0.25F);
  config.maximum_spot_age = std::clamp(
      config.maximum_spot_age, std::chrono::seconds::zero(),
      std::chrono::seconds{3'600});
  config.maximum_spot_frequency_delta_hz = std::min<std::uint64_t>(
      config.maximum_spot_frequency_delta_hz, 10'000);
  if (!is_callsign_like_span(raw_span, config.maximum_unknown_characters)) {
    return {};
  }

  // Deduplicate acoustic candidates before applying bounds. Keeping the best
  // path prevents repeated hypotheses from manufacturing extra support.
  std::map<std::string, CallsignRawHypothesis> unique;
  const auto hypothesis_count =
      std::min(hypotheses.size(), config.maximum_hypotheses);
  for (std::size_t index = 0; index < hypothesis_count; ++index) {
    const auto& hypothesis = hypotheses[index];
    if (!sameRawSpan(raw_span, hypothesis.raw_span) ||
        !candidateMatchesRawSpan(raw_span, hypothesis.candidate) ||
        !std::isfinite(hypothesis.acoustic_support) ||
        !std::isfinite(hypothesis.acoustic_edit_cost) ||
        hypothesis.acoustic_edit_cost < 0.0F ||
        hypothesis.acoustic_edit_cost > config.maximum_acoustic_edit_cost) {
      continue;
    }
    const std::string candidate = uppercase(trim(hypothesis.candidate));
    if (!plausibleCompleteCallsign(candidate)) continue;
    const float support =
        std::clamp(hypothesis.acoustic_support, 0.0F, 1.0F);
    if (support < config.minimum_acoustic_support) continue;
    CallsignRawHypothesis normalized{
        .raw_span = raw_span,
        .candidate = candidate,
        .acoustic_support = support,
        .acoustic_edit_cost = hypothesis.acoustic_edit_cost,
    };
    const auto found = unique.find(candidate);
    if (found == unique.end() ||
        support > found->second.acoustic_support ||
        (support == found->second.acoustic_support &&
         normalized.acoustic_edit_cost < found->second.acoustic_edit_cost)) {
      unique.insert_or_assign(candidate, std::move(normalized));
    }
  }

  std::vector<CallsignRawHypothesis> bounded;
  bounded.reserve(unique.size());
  for (auto& [candidate, hypothesis] : unique) {
    static_cast<void>(candidate);
    bounded.push_back(std::move(hypothesis));
  }
  std::ranges::sort(bounded, [](const auto& left, const auto& right) {
    if (left.acoustic_support != right.acoustic_support)
      return left.acoustic_support > right.acoustic_support;
    if (left.acoustic_edit_cost != right.acoustic_edit_cost)
      return left.acoustic_edit_cost < right.acoustic_edit_cost;
    return left.candidate < right.candidate;
  });
  std::vector<CallsignSuggestion> suggestions;
  suggestions.reserve(bounded.size());
  for (const auto& hypothesis : bounded) {
    CallsignSuggestion suggestion{
        .raw_span = raw_span,
        .candidate = hypothesis.candidate,
        .acoustic_support = hypothesis.acoustic_support,
        .acoustic_edit_cost = hypothesis.acoustic_edit_cost,
        .provider_weight = 0.0F,
        .ranking_score = 0.0F,
        .provenance = {},
    };

    // Score at most one contribution from each provider. Multiple records
    // remain visible as provenance but cannot amplify a provider's weight.
    std::map<std::string, float> provider_weights;
    const auto provider_record_count =
        std::min(provider_evidence.size(), config.maximum_provider_records);
    for (std::size_t index = 0; index < provider_record_count; ++index) {
      const auto& evidence = provider_evidence[index];
      if (!validProviderEvidence(evidence, config) ||
          uppercase(trim(evidence.candidate)) != hypothesis.candidate ||
          !candidateMatchesRawSpan(raw_span, evidence.candidate)) {
        continue;
      }
      CallsignProviderEvidence canonical = evidence;
      canonical.candidate = hypothesis.candidate;
      canonical.provider_id = canonicalProviderId(evidence.provider_id);
      canonical.provider_label = trim(evidence.provider_label);
      canonical.mode = uppercase(trim(evidence.mode));
      canonical.spotter = uppercase(trim(evidence.spotter));
      canonical.rationale = trim(evidence.rationale);
      suggestion.provenance.push_back(std::move(canonical));
      const float weight = std::min(evidence.requested_weight,
                                    config.maximum_weight_per_provider);
      auto& provider_weight =
          provider_weights[canonicalProviderId(evidence.provider_id)];
      provider_weight = std::max(provider_weight, weight);
    }
    std::ranges::sort(suggestion.provenance, [](const auto& left,
                                                const auto& right) {
      return std::tie(left.provider_id, left.kind, left.dataset_version,
                      left.spotter, left.rationale) <
             std::tie(right.provider_id, right.kind, right.dataset_version,
                      right.spotter, right.rationale);
    });
    if (suggestion.provenance.size() >
        config.maximum_provenance_per_suggestion) {
      suggestion.provenance.resize(
          config.maximum_provenance_per_suggestion);
    }
    float provider_weight = 0.0F;
    for (const auto& [provider, weight] : provider_weights) {
      static_cast<void>(provider);
      provider_weight += weight;
    }
    suggestion.provider_weight =
        std::min(provider_weight, config.maximum_total_provider_weight);
    suggestion.ranking_score =
        hypothesis.acoustic_support -
        config.acoustic_edit_cost_weight * hypothesis.acoustic_edit_cost +
        suggestion.provider_weight;
    suggestions.push_back(std::move(suggestion));
  }

  std::ranges::sort(suggestions, [](const auto& left, const auto& right) {
    if (left.ranking_score != right.ranking_score)
      return left.ranking_score > right.ranking_score;
    if (left.acoustic_support != right.acoustic_support)
      return left.acoustic_support > right.acoustic_support;
    if (left.acoustic_edit_cost != right.acoustic_edit_cost)
      return left.acoustic_edit_cost < right.acoustic_edit_cost;
    return left.candidate < right.candidate;
  });
  if (suggestions.size() > config.maximum_suggestions) {
    suggestions.resize(config.maximum_suggestions);
  }
  return suggestions;
}

}  // namespace cwassistant::core
