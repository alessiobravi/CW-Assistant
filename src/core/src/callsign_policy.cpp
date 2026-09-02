#include "cwassistant/core/callsign_policy.hpp"

#include <algorithm>
#include <cctype>

namespace cwassistant::core {
namespace {

bool isPlausibleDecodedCallsign(const std::string_view normalized) {
  const auto slash = normalized.find('/');
  if (slash != std::string_view::npos &&
      normalized.find('/', slash + 1) != std::string_view::npos) return false;
  const auto plausible_base = [](const std::string_view base) {
    if (base.size() < 3 || base.size() > 12) return false;
    const auto last_digit = base.find_last_of("0123456789");
    if (last_digit == std::string_view::npos || last_digit == 0 ||
        last_digit + 1 >= base.size() || base.size() - last_digit - 1 > 4) {
      return false;
    }
    if (!std::all_of(
            base.begin() + static_cast<std::ptrdiff_t>(last_digit + 1),
            base.end(), [](const unsigned char character) {
              return std::isalpha(character) != 0;
            })) {
      return false;
    }
    return std::any_of(
        base.begin(), base.begin() + static_cast<std::ptrdiff_t>(last_digit),
        [](const unsigned char character) {
          return std::isalpha(character) != 0;
        });
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
