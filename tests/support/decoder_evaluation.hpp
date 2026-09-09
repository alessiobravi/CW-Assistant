#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace cwassistant::test {

// CER and WER use case-insensitive text with runs of whitespace canonicalized
// to one ASCII space. Insertions still count even when the reference is empty.
struct ErrorRate {
  std::size_t edits{0};
  std::size_t references{0};

  [[nodiscard]] double rate() const noexcept {
    return references == 0U
        ? (edits == 0U ? 0.0 : std::numeric_limits<double>::infinity())
        : static_cast<double>(edits) / static_cast<double>(references);
  }
};

template <typename Value>
[[nodiscard]] std::size_t editDistance(const std::vector<Value>& expected,
                                       const std::vector<Value>& actual) {
  std::vector<std::size_t> previous(actual.size() + 1U);
  std::vector<std::size_t> current(actual.size() + 1U);
  for (std::size_t column = 0; column <= actual.size(); ++column)
    previous[column] = column;
  for (std::size_t row = 1; row <= expected.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= actual.size(); ++column) {
      const std::size_t substitution =
          previous[column - 1U] +
          (expected[row - 1U] == actual[column - 1U] ? 0U : 1U);
      current[column] = std::min({previous[column] + 1U,
                                  current[column - 1U] + 1U,
                                  substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

[[nodiscard]] inline std::string normalizedText(const std::string_view text) {
  std::string result;
  bool pending_space = false;
  for (const unsigned char character : text) {
    if (std::isspace(character) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) result.push_back(' ');
    pending_space = false;
    result.push_back(static_cast<char>(std::toupper(character)));
  }
  return result;
}

[[nodiscard]] inline std::vector<std::string> words(
    const std::string_view text) {
  const std::string normalized = normalizedText(text);
  std::vector<std::string> result;
  std::size_t start = 0;
  while (start < normalized.size()) {
    const std::size_t end = normalized.find(' ', start);
    result.emplace_back(normalized.substr(start, end - start));
    if (end == std::string::npos) break;
    start = end + 1U;
  }
  return result;
}

[[nodiscard]] inline ErrorRate characterErrorRate(
    const std::string_view expected, const std::string_view actual) {
  const std::string expected_normalized = normalizedText(expected);
  const std::string actual_normalized = normalizedText(actual);
  const std::vector<char> expected_characters(expected_normalized.cbegin(),
                                               expected_normalized.cend());
  const std::vector<char> actual_characters(actual_normalized.cbegin(),
                                             actual_normalized.cend());
  return {editDistance(expected_characters, actual_characters),
          expected_characters.size()};
}

[[nodiscard]] inline ErrorRate wordErrorRate(const std::string_view expected,
                                             const std::string_view actual) {
  const auto expected_words = words(expected);
  const auto actual_words = words(actual);
  return {editDistance(expected_words, actual_words), expected_words.size()};
}

// Exact set matching: duplicate observations of one station do not inflate
// either precision or recall, and near misses remain errors.
struct CallsignCounts {
  std::size_t true_positives{0};
  std::size_t false_positives{0};
  std::size_t false_negatives{0};

  [[nodiscard]] double precision() const noexcept {
    const std::size_t published = true_positives + false_positives;
    return published == 0U ? 1.0
                           : static_cast<double>(true_positives) /
                                 static_cast<double>(published);
  }

  [[nodiscard]] double recall() const noexcept {
    const std::size_t expected = true_positives + false_negatives;
    return expected == 0U ? 1.0
                          : static_cast<double>(true_positives) /
                                static_cast<double>(expected);
  }
};

// Callsign publication is a time-varying decoder result. Receiver annotation
// scoring must inspect the callsign set visible by the end of an event rather
// than re-running callsign extraction over that event's transcript. The latter
// remains useful as a separate diagnostic, but it is not publication recall.
struct TimestampedPublication {
  std::uint64_t sample{0};
  bool published{false};
  std::vector<std::string> callsigns;
};

[[nodiscard]] inline std::vector<std::string> callsignsAtOrBefore(
    const std::vector<TimestampedPublication>& points,
    const std::uint64_t sample) {
  std::vector<std::string> result;
  for (const auto& point : points) {
    if (point.sample > sample) break;
    result = point.published ? point.callsigns : std::vector<std::string>{};
  }
  return result;
}

struct SampleEpisode {
  std::uint64_t start_sample{0};
  std::uint64_t end_sample{0};
};

[[nodiscard]] inline bool halfOpenOverlaps(
    const SampleEpisode& left, const SampleEpisode& right) noexcept {
  return left.start_sample < right.end_sample &&
         right.start_sample < left.end_sample;
}

// A known-good event takes precedence over an overlapping uncertain interval:
// uncertainty protects a label only when no reviewed event contradicts it.
[[nodiscard]] inline bool callsignEpisodeIsProtected(
    const bool matches_expected_callsign,
    const bool overlaps_certain_event,
    const bool overlaps_uncertain_event) noexcept {
  return matches_expected_callsign ||
         (!overlaps_certain_event && overlaps_uncertain_event);
}

struct CallsignEpisode : SampleEpisode {
  std::string callsign;
};

[[nodiscard]] inline std::vector<SampleEpisode> publicationEpisodes(
    const std::vector<TimestampedPublication>& points,
    const std::uint64_t end_sample) {
  std::vector<SampleEpisode> result;
  bool published = false;
  std::uint64_t started = 0;
  for (const auto& point : points) {
    if (!published && point.published) {
      started = point.sample;
      published = true;
    } else if (published && !point.published) {
      if (point.sample > started) result.push_back({started, point.sample});
      published = false;
    }
  }
  if (published && end_sample > started)
    result.push_back({started, end_sample});
  return result;
}

[[nodiscard]] inline std::vector<CallsignEpisode> callsignEpisodes(
    const std::vector<TimestampedPublication>& points,
    const std::uint64_t end_sample) {
  struct ActiveCallsign {
    std::string callsign;
    std::uint64_t started{0};
  };
  std::vector<ActiveCallsign> active;
  std::vector<CallsignEpisode> result;
  for (const auto& point : points) {
    std::vector<std::string> next = point.published
        ? point.callsigns : std::vector<std::string>{};
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    for (auto iterator = active.begin(); iterator != active.end();) {
      if (std::binary_search(next.cbegin(), next.cend(), iterator->callsign)) {
        ++iterator;
        continue;
      }
      if (point.sample > iterator->started) {
        result.push_back(
            {{iterator->started, point.sample}, iterator->callsign});
      }
      iterator = active.erase(iterator);
    }
    for (const auto& callsign : next) {
      const bool retained = std::any_of(
          active.cbegin(), active.cend(), [&](const auto& current) {
        return current.callsign == callsign;
      });
      if (!retained) active.push_back({callsign, point.sample});
    }
  }
  for (const auto& current : active) {
    if (end_sample > current.started)
      result.push_back({{current.started, end_sample}, current.callsign});
  }
  return result;
}

[[nodiscard]] inline CallsignCounts callsignCounts(
    std::vector<std::string> expected, std::vector<std::string> published) {
  const auto normalize = [](std::vector<std::string>& values) {
    for (std::string& value : values) value = normalizedText(value);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
  };
  normalize(expected);
  normalize(published);

  CallsignCounts result;
  std::size_t expected_index = 0;
  std::size_t published_index = 0;
  while (expected_index < expected.size() &&
         published_index < published.size()) {
    if (expected[expected_index] == published[published_index]) {
      ++result.true_positives;
      ++expected_index;
      ++published_index;
    } else if (expected[expected_index] < published[published_index]) {
      ++result.false_negatives;
      ++expected_index;
    } else {
      ++result.false_positives;
      ++published_index;
    }
  }
  result.false_negatives += expected.size() - expected_index;
  result.false_positives += published.size() - published_index;
  return result;
}

}  // namespace cwassistant::test
