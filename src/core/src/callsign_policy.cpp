#include "cwassistant/core/callsign_policy.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace cwassistant::core {
namespace {

bool isPlausibleDecodedCallsign(const std::string_view normalized) {
  const auto slash = normalized.find('/');
  if (slash != std::string_view::npos &&
      normalized.find('/', slash + 1) != std::string_view::npos) return false;
  const auto plausible_base = [](const std::string_view base) {
    if (base.size() < 3 || base.size() > 12) return false;
    const auto first_digit = base.find_first_of("0123456789");
    const auto last_digit = base.find_last_of("0123456789");
    if (first_digit == std::string_view::npos ||
        last_digit + 1 >= base.size() || base.size() - last_digit - 1 > 4) {
      return false;
    }
    const bool standard_prefix = first_digit > 0 && std::all_of(
        base.begin(), base.begin() + static_cast<std::ptrdiff_t>(first_digit),
        [](const unsigned char character) {
          return std::isalpha(character) != 0;
        });
    const auto second_digit = first_digit == 0
        ? base.find_first_of("0123456789", 1) : std::string_view::npos;
    const bool numeric_leading_prefix = second_digit != std::string_view::npos &&
        second_digit > 1 && std::all_of(
            base.begin() + 1,
            base.begin() + static_cast<std::ptrdiff_t>(second_digit),
            [](const unsigned char character) {
              return std::isalpha(character) != 0;
            }) && std::all_of(
            base.begin() + static_cast<std::ptrdiff_t>(second_digit),
            base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
            [](const unsigned char character) {
              return std::isdigit(character) != 0;
            });
    if ((!standard_prefix && !numeric_leading_prefix) || !std::all_of(
            base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
            base.end(), [](const unsigned char character) {
              return std::isalpha(character) != 0;
            })) {
      return false;
    }
    // A contiguous multi-digit block is valid for special-event calls. A
    // letter-leading token with separated digit runs is far more likely to be
    // merged report/noise (for example EA7G2NX) than one decoded callsign.
    if (standard_prefix) {
      return std::all_of(
          base.begin() + static_cast<std::ptrdiff_t>(first_digit),
          base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
          [](const unsigned char character) {
            return std::isdigit(character) != 0;
          });
    }
    return true;
  };
  if (slash == std::string_view::npos) return plausible_base(normalized);
  const std::string_view left = normalized.substr(0, slash);
  const std::string_view right = normalized.substr(slash + 1);
  const auto plausible_modifier = [](const std::string_view value) {
    return !value.empty() && value.size() <= 4 &&
        std::all_of(value.begin(), value.end(),
                    [](const unsigned char character) {
                      return std::isalnum(character) != 0;
                    });
  };
  return (plausible_base(left) && plausible_modifier(right)) ||
         (plausible_modifier(left) && plausible_base(right));
}

}  // namespace

std::optional<std::string> CallsignPolicy::normalize(
    const std::string_view callsign) {
  const auto first = callsign.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  const auto last = callsign.find_last_not_of(" \t\r\n");
  const auto trimmed = callsign.substr(first, last - first + 1);
  if (trimmed.size() < 3 || trimmed.size() > 16) {
    return std::nullopt;
  }

  std::string normalized;
  normalized.reserve(trimmed.size());
  bool has_letter = false;
  bool has_digit = false;
  bool previous_was_slash = false;
  for (const unsigned char character : trimmed) {
    if (std::isalpha(character) != 0) {
      normalized.push_back(static_cast<char>(std::toupper(character)));
      has_letter = true;
      previous_was_slash = false;
    } else if (std::isdigit(character) != 0) {
      normalized.push_back(static_cast<char>(character));
      has_digit = true;
      previous_was_slash = false;
    } else if (character == '/' && !normalized.empty() && !previous_was_slash) {
      normalized.push_back('/');
      previous_was_slash = true;
    } else {
      return std::nullopt;
    }
  }

  if (!has_letter || !has_digit || normalized.back() == '/') {
    return std::nullopt;
  }
  return normalized;
}

std::optional<std::string> CallsignPolicy::latest_in_text(
    const std::string_view decoded_text) {
  std::string token;
  std::optional<std::string> result;
  const auto consider = [&result](const std::string& candidate) {
    if (const auto normalized = normalize(candidate);
        normalized && isPlausibleDecodedCallsign(*normalized)) {
      result = *normalized;
    }
  };
  for (const unsigned char character : decoded_text) {
    if (std::isalnum(character) != 0 || character == '/') {
      token.push_back(static_cast<char>(character));
    } else if (!token.empty()) {
      consider(token);
      token.clear();
    }
  }
  if (!token.empty()) consider(token);
  return result;
}

std::optional<std::string> CallsignPolicy::latest_complete_in_text(
    const std::string_view stable_text) {
  const auto completed_end = stable_text.find_last_of(" \t\r\n");
  if (completed_end == std::string_view::npos) return std::nullopt;
  return latest_in_text(stable_text.substr(0, completed_end + 1));
}

std::optional<std::string> CallsignPolicy::best_complete_in_text(
    const std::string_view stable_text) {
  const auto completed_end = stable_text.find_last_of(" \t\r\n");
  if (completed_end == std::string_view::npos) return std::nullopt;
  std::vector<std::string> words;
  std::string token;
  for (const unsigned char character :
       stable_text.substr(0, completed_end + 1)) {
    if (std::isalnum(character) != 0 || character == '/') {
      token.push_back(static_cast<char>(std::toupper(character)));
    } else if (!token.empty()) {
      words.push_back(std::move(token));
      token.clear();
    }
  }

  struct CandidateEvidence {
    int score{0};
    std::size_t last_index{0};
    int occurrences{0};
  };
  std::unordered_map<std::string, CandidateEvidence> evidence;
  for (std::size_t index = 0; index < words.size(); ++index) {
    const auto normalized = normalize(words[index]);
    if (!normalized || !isPlausibleDecodedCallsign(*normalized)) continue;
    CandidateEvidence& candidate = evidence[*normalized];
    ++candidate.occurrences;
    candidate.last_index = index;
    // A stream label needs more than callsign-shaped spelling. Contest and
    // ordinary CW exchanges provide useful role evidence: a station normally
    // identifies itself after DE, CQ/TEST, or TU, and a split runner commonly
    // places UP immediately after its call. Exact repetition is weaker but
    // still useful for a caller sending only its own call. These weights rank
    // decoded alternatives; they never create or correct decoded characters.
    if (index > 0 && words[index - 1] == "DE") candidate.score += 8;
    if (index > 0 && words[index - 1] == "TU") candidate.score += 6;
    if (index > 0 && words[index - 1] == "CQ") candidate.score += 5;
    if (index > 1 && words[index - 2] == "CQ") candidate.score += 4;
    if (index > 2 && words[index - 3] == "CQ") candidate.score += 3;
    if (index + 1 < words.size() && words[index + 1] == "UP")
      candidate.score += 5;
    // Ordinary hand-sent QSOs often close an identification with PSE K, K,
    // KN, AR, or SK rather than repeating CQ/DE. These are role/boundary
    // clues only: the token must already be a complete acoustically decoded
    // callsign before any of these weights apply.
    if (index + 2 < words.size() && words[index + 1] == "PSE" &&
        words[index + 2] == "K") {
      candidate.score += 6;
    } else if (index + 1 < words.size() &&
               (words[index + 1] == "K" || words[index + 1] == "KN" ||
                words[index + 1] == "AR" || words[index + 1] == "SK")) {
      candidate.score += 4;
    }
    if (candidate.occurrences > 1) candidate.score += 4;
  }

  std::optional<std::string> result;
  int best_score = 3;
  std::size_t latest_index = 0;
  for (const auto& [candidate, value] : evidence) {
    if (value.score > best_score ||
        (value.score == best_score && value.last_index >= latest_index)) {
      result = candidate;
      best_score = value.score;
      latest_index = value.last_index;
    }
  }
  return result;
}

bool CallsignPolicy::add_ignored(const std::string_view callsign) {
  const auto normalized = normalize(callsign);
  return normalized.has_value() && ignored_.insert(*normalized).second;
}

bool CallsignPolicy::remove_ignored(const std::string_view callsign) {
  const auto normalized = normalize(callsign);
  return normalized.has_value() && ignored_.erase(*normalized) == 1;
}

bool CallsignPolicy::is_ignored(const std::string_view callsign) const {
  const auto normalized = normalize(callsign);
  return normalized.has_value() && ignored_.contains(*normalized);
}

std::vector<std::string> CallsignPolicy::ignored_callsigns() const {
  std::vector<std::string> result(ignored_.begin(), ignored_.end());
  std::ranges::sort(result);
  return result;
}

}  // namespace cwassistant::core
