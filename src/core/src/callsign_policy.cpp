#include "cwassistant/core/callsign_policy.hpp"

#include <algorithm>
#include <cctype>

namespace cwassistant::core {

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
