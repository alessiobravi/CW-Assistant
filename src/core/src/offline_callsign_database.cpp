#include "cwassistant/core/offline_callsign_database.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <utility>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

std::string_view trim(const std::string_view value) noexcept {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
    --last;
  }
  return value.substr(first, last - first);
}

bool isCommentOrDirective(const std::string_view line) noexcept {
  if (line.empty()) return true;
  return line.front() == '#' || line.front() == ';' || line.front() == '!' ||
      line.front() == '[' ||
      (line.size() >= 2U && line[0] == '/' && line[1] == '/');
}

std::string_view firstField(const std::string_view line) noexcept {
  const auto history_separator = line.find_first_of(",;");
  if (history_separator != std::string_view::npos)
    return trim(line.substr(0, history_separator));
  const auto whitespace = line.find_first_of(" \t");
  return trim(line.substr(0, whitespace));
}

std::optional<std::string> normalizePattern(const std::string_view pattern) {
  const auto value = trim(pattern);
  if (value.size() < 3U || value.size() > 16U) return std::nullopt;
  std::string normalized;
  normalized.reserve(value.size());
  bool previous_slash = false;
  for (const unsigned char character : value) {
    if (std::isalnum(character) != 0) {
      normalized.push_back(static_cast<char>(std::toupper(character)));
      previous_slash = false;
    } else if (character == '?') {
      normalized.push_back('?');
      previous_slash = false;
    } else if (character == '/' && !normalized.empty() && !previous_slash) {
      normalized.push_back('/');
      previous_slash = true;
    } else {
      return std::nullopt;
    }
  }
  if (normalized.back() == '/') return std::nullopt;
  return normalized;
}

struct Alignment {
  std::size_t distance{0};
  std::vector<CallsignCharacterDifference> differences;
};

Alignment align(const std::string_view query,
                const std::string_view callsign) {
  const std::size_t columns = callsign.size() + 1U;
  std::vector<std::uint8_t> costs((query.size() + 1U) * columns, 0U);
  for (std::size_t index = 0; index <= query.size(); ++index)
    costs[index * columns] = static_cast<std::uint8_t>(index);
  for (std::size_t index = 0; index <= callsign.size(); ++index)
    costs[index] = static_cast<std::uint8_t>(index);
  for (std::size_t left = 1; left <= query.size(); ++left) {
    for (std::size_t right = 1; right <= callsign.size(); ++right) {
      const bool same = query[left - 1U] == '?' ||
          query[left - 1U] == callsign[right - 1U];
      const auto diagonal = static_cast<std::uint8_t>(
          costs[(left - 1U) * columns + right - 1U] + (same ? 0U : 1U));
      const auto deletion = static_cast<std::uint8_t>(
          costs[(left - 1U) * columns + right] + 1U);
      const auto insertion = static_cast<std::uint8_t>(
          costs[left * columns + right - 1U] + 1U);
      costs[left * columns + right] =
          std::min({diagonal, deletion, insertion});
    }
  }

  Alignment result{.distance = costs.back(), .differences = {}};
  std::size_t left = query.size();
  std::size_t right = callsign.size();
  while (left > 0U || right > 0U) {
    const auto current = costs[left * columns + right];
    if (left > 0U && right > 0U) {
      const bool same = query[left - 1U] == '?' ||
          query[left - 1U] == callsign[right - 1U];
      const auto diagonal = static_cast<std::uint8_t>(
          costs[(left - 1U) * columns + right - 1U] + (same ? 0U : 1U));
      if (current == diagonal) {
        if (query[left - 1U] == '?') {
          result.differences.push_back({
              .kind = CallsignDifferenceKind::wildcard_match,
              .query_index = left - 1U,
              .callsign_index = right - 1U,
              .query_character = '?',
              .callsign_character = callsign[right - 1U],
          });
        } else if (!same) {
          result.differences.push_back({
              .kind = CallsignDifferenceKind::substitution,
              .query_index = left - 1U,
              .callsign_index = right - 1U,
              .query_character = query[left - 1U],
              .callsign_character = callsign[right - 1U],
          });
        }
        --left;
        --right;
        continue;
      }
    }
    if (left > 0U &&
        current == costs[(left - 1U) * columns + right] + 1U) {
      result.differences.push_back({
          .kind = CallsignDifferenceKind::deletion,
          .query_index = left - 1U,
          .callsign_index = right,
          .query_character = query[left - 1U],
          .callsign_character = '\0',
      });
      --left;
      continue;
    }
    result.differences.push_back({
        .kind = CallsignDifferenceKind::insertion,
        .query_index = left,
        .callsign_index = right - 1U,
        .query_character = '\0',
        .callsign_character = callsign[right - 1U],
    });
    --right;
  }
  std::ranges::reverse(result.differences);
  return result;
}

}  // namespace

OfflineCallsignDatabase::OfflineCallsignDatabase(
    OfflineCallsignDatabaseLimits limits)
    : limits_(limits) {
  limits_.maximum_input_bytes = std::max<std::size_t>(1U,
      std::min<std::size_t>(limits_.maximum_input_bytes,
                            256U * 1'024U * 1'024U));
  limits_.maximum_records = std::max<std::size_t>(1U,
      std::min<std::size_t>(limits_.maximum_records, 5'000'000U));
  limits_.maximum_line_bytes = std::clamp<std::size_t>(
      limits_.maximum_line_bytes, 16U, 64U * 1'024U);
  limits_.maximum_query_results = std::clamp<std::size_t>(
      limits_.maximum_query_results, 1U, 1'024U);
  limits_.maximum_edit_distance = std::clamp<std::size_t>(
      limits_.maximum_edit_distance, 0U, 16U);
  callsigns_.reserve(std::min<std::size_t>(limits_.maximum_records, 65'536U));
}

void OfflineCallsignDatabase::clear() noexcept { callsigns_.clear(); }

std::size_t OfflineCallsignDatabase::size() const noexcept {
  return callsigns_.size();
}

OfflineCallsignImportResult OfflineCallsignDatabase::importText(
    const std::string_view text) {
  OfflineCallsignImportResult result;
  if (text.size() > limits_.maximum_input_bytes) return result;
  result.accepted = true;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == std::string_view::npos ? text.size() : newline;
    const auto raw_line = text.substr(offset, end - offset);
    if (raw_line.size() > limits_.maximum_line_bytes) {
      ++result.overlong_lines;
      ++result.ignored_lines;
    } else {
      const auto line = trim(raw_line);
      if (isCommentOrDirective(line)) {
        ++result.ignored_lines;
      } else if (const auto normalized = CallsignPolicy::normalize(
                     firstField(line));
                 normalized &&
                 CallsignPolicy::latest_in_text(*normalized) == normalized) {
        if (callsigns_.contains(*normalized)) {
          ++result.duplicate_records;
        } else if (callsigns_.size() >= limits_.maximum_records) {
          result.capacity_reached = true;
        } else {
          callsigns_.insert(*normalized);
          ++result.inserted_records;
        }
      } else {
        ++result.ignored_lines;
      }
    }
    if (newline == std::string_view::npos) break;
    offset = newline + 1U;
  }
  return result;
}

bool OfflineCallsignDatabase::contains(const std::string_view callsign) const {
  const auto normalized = CallsignPolicy::normalize(callsign);
  return normalized && callsigns_.contains(*normalized);
}

std::vector<OfflineCallsignMatch> OfflineCallsignDatabase::query(
    const std::string_view pattern, const std::size_t maximum_edit_distance,
    const std::size_t limit) const {
  const auto normalized = normalizePattern(pattern);
  if (!normalized || limit == 0U) return {};
  const std::size_t bounded_distance = std::min(
      maximum_edit_distance, limits_.maximum_edit_distance);
  const std::size_t bounded_limit = std::min(
      limit, limits_.maximum_query_results);
  std::vector<OfflineCallsignMatch> matches;
  matches.reserve(bounded_limit);
  const auto ranksBefore = [](const OfflineCallsignMatch& left,
                              const OfflineCallsignMatch& right) {
    if (left.edit_distance != right.edit_distance)
      return left.edit_distance < right.edit_distance;
    return left.callsign < right.callsign;
  };
  for (const auto& callsign : callsigns_) {
    const auto longer = std::max(callsign.size(), normalized->size());
    const auto shorter = std::min(callsign.size(), normalized->size());
    if (longer - shorter > bounded_distance) continue;
    auto candidate = align(*normalized, callsign);
    if (candidate.distance > bounded_distance) continue;
    OfflineCallsignMatch match{
        .callsign = callsign,
        .edit_distance = candidate.distance,
        .differences = std::move(candidate.differences),
    };
    if (matches.size() < bounded_limit) {
      matches.push_back(std::move(match));
      continue;
    }
    const auto worst = std::max_element(matches.begin(), matches.end(),
                                        ranksBefore);
    if (ranksBefore(match, *worst)) *worst = std::move(match);
  }
  std::ranges::sort(matches, ranksBefore);
  return matches;
}

}  // namespace cwassistant::core
