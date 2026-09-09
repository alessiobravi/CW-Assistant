#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "decoder_evaluation.hpp"

namespace cwassistant::test {

struct ReceiverAnnotation {
  std::uint64_t start_sample{0};
  std::uint64_t end_sample{0};
  double frequency_hz{0.0};
  std::string literal_text;
  std::string normalized_text;
  std::vector<std::string> callsigns;
  bool uncertain{false};
};

struct ReceiverAnnotationCoverage {
  std::uint64_t start_sample{0};
  std::uint64_t end_sample{0};
};

struct ReceiverAnnotationManifest {
  std::uint32_t version{0};
  std::uint32_t sample_rate_hz{0};
  std::string audio_sha256;
  std::vector<ReceiverAnnotationCoverage> coverage;
  std::vector<ReceiverAnnotation> events;
};

[[nodiscard]] inline bool overlapsReviewedCoverage(
    const std::uint64_t start_sample, const std::uint64_t end_sample,
    const std::vector<ReceiverAnnotationCoverage>& coverage) noexcept {
  return std::any_of(coverage.cbegin(), coverage.cend(),
                     [start_sample, end_sample](const auto& interval) {
    return start_sample < interval.end_sample &&
           interval.start_sample < end_sample;
  });
}

[[nodiscard]] inline std::uint64_t reviewedCoverageSamples(
    const std::vector<ReceiverAnnotationCoverage>& coverage) noexcept {
  std::uint64_t total = 0;
  for (const auto& interval : coverage)
    total += interval.end_sample - interval.start_sample;
  return total;
}

[[nodiscard]] inline bool receiverAnnotationsFitAudio(
    const ReceiverAnnotationManifest& manifest,
    const std::uint64_t total_samples) noexcept {
  return std::all_of(manifest.events.cbegin(), manifest.events.cend(),
                     [total_samples](const auto& event) {
           return event.end_sample <= total_samples;
         }) &&
         std::all_of(manifest.coverage.cbegin(), manifest.coverage.cend(),
                     [total_samples](const auto& interval) {
           return interval.end_sample <= total_samples;
         });
}

inline bool parseUnsigned(const std::string_view text,
                          std::uint64_t& value) {
  if (text.empty()) return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                      value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

inline bool parseDouble(const std::string_view text, double& value) {
  if (text.empty()) return false;
  std::istringstream input{std::string(text)};
  input.imbue(std::locale::classic());
  input >> value;
  return input && input.peek() == std::char_traits<char>::eof() &&
         std::isfinite(value);
}

inline std::vector<std::string_view> tabFields(const std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t end = line.find('\t', start);
    fields.push_back(line.substr(start, end - start));
    if (end == std::string_view::npos) break;
    start = end + 1U;
  }
  return fields;
}

inline bool parseCallsigns(const std::string_view field,
                           std::vector<std::string>& callsigns) {
  std::size_t start = 0;
  while (start < field.size()) {
    const std::size_t end = field.find(',', start);
    const auto call = field.substr(start, end - start);
    const bool has_letter = std::any_of(call.cbegin(), call.cend(), [](char c) {
      return c >= 'A' && c <= 'Z';
    });
    const bool has_digit = std::any_of(call.cbegin(), call.cend(), [](char c) {
      return c >= '0' && c <= '9';
    });
    const bool lexical = std::all_of(call.cbegin(), call.cend(), [](char c) {
      return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/';
    });
    if (call.empty() || call.size() > 24U || !has_letter || !has_digit ||
        !lexical || call.front() == '/' || call.back() == '/' ||
        call.find("//") != std::string_view::npos) return false;
    callsigns.emplace_back(call);
    if (end == std::string_view::npos) break;
    start = end + 1U;
  }
  std::sort(callsigns.begin(), callsigns.end());
  callsigns.erase(std::unique(callsigns.begin(), callsigns.end()),
                  callsigns.end());
  return true;
}

// Bounded, dependency-free sidecar format:
//   CWA-RECEIVER-ANNOTATIONS\t<1|2>
//   sample_rate_hz\t<integer>
//   audio_sha256\t<64 lowercase hex characters>
//   coverage\t<start sample>\t<end sample>             (v2, required)
//   event\t<start sample>\t<end sample>\t<Hz>\t<literal>\t<normalized>
//         \t<comma-separated exact callsigns>\t<0|1 uncertain>
// Text fields may contain spaces but not tabs or newlines. Uncertain events are
// retained for review and excluded from quality scores. Version 1 sidecars
// retain their legacy implicit full-file coverage. Version 2 coverage declares
// exhaustively reviewed, non-overlapping intervals; an interval with no event
// is an explicit no-CW fixture.
inline bool parseReceiverAnnotations(std::istream& input,
                                     ReceiverAnnotationManifest& manifest,
                                     std::string& error) {
  constexpr std::size_t kMaximumLineBytes = 4'096U;
  constexpr std::size_t kMaximumEvents = 256U;
  manifest = {};
  error.clear();
  std::string line;
  std::size_t line_number = 0;
  bool header_seen = false;
  bool rate_seen = false;
  bool hash_seen = false;
  std::uint64_t previous_coverage_end = 0;
  bool coverage_seen = false;
  std::uint64_t previous_start = 0;
  bool event_seen = false;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() > kMaximumLineBytes) {
      error = "annotation line exceeds 4096 bytes";
      return false;
    }
    if (line.empty() || line.front() == '#') continue;
    const auto fields = tabFields(line);
    if (!header_seen) {
      std::uint64_t version = 0;
      if (fields.size() != 2U || fields[0] != "CWA-RECEIVER-ANNOTATIONS" ||
          !parseUnsigned(fields[1], version) || version < 1U || version > 2U) {
        error = "first data line must declare annotation format version 1 or 2";
        return false;
      }
      manifest.version = static_cast<std::uint32_t>(version);
      header_seen = true;
      continue;
    }
    if (fields[0] == "sample_rate_hz") {
      std::uint64_t rate = 0;
      if (rate_seen || fields.size() != 2U ||
          !parseUnsigned(fields[1], rate) || rate < 1'000U ||
          rate > 1'000'000U) {
        error = "invalid or duplicate sample_rate_hz";
        return false;
      }
      manifest.sample_rate_hz = static_cast<std::uint32_t>(rate);
      rate_seen = true;
      continue;
    }
    if (fields[0] == "audio_sha256") {
      const bool valid = fields.size() == 2U && fields[1].size() == 64U &&
          std::all_of(fields[1].cbegin(), fields[1].cend(), [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
          });
      if (hash_seen || !valid) {
        error = "invalid or duplicate audio_sha256";
        return false;
      }
      manifest.audio_sha256 = fields[1];
      hash_seen = true;
      continue;
    }
    if (fields[0] == "coverage") {
      ReceiverAnnotationCoverage interval;
      if (manifest.version != 2U || fields.size() != 3U ||
          manifest.coverage.size() >= kMaximumEvents ||
          !parseUnsigned(fields[1], interval.start_sample) ||
          !parseUnsigned(fields[2], interval.end_sample) ||
          interval.end_sample <= interval.start_sample ||
          (coverage_seen && interval.start_sample < previous_coverage_end)) {
        error = "invalid or overlapping coverage interval";
        return false;
      }
      manifest.coverage.push_back(interval);
      previous_coverage_end = interval.end_sample;
      coverage_seen = true;
      continue;
    }
    if (fields[0] != "event" || fields.size() != 8U ||
        manifest.events.size() >= kMaximumEvents) {
      error = "invalid annotation event record";
      return false;
    }
    ReceiverAnnotation event;
    if (!parseUnsigned(fields[1], event.start_sample) ||
        !parseUnsigned(fields[2], event.end_sample) ||
        event.end_sample <= event.start_sample ||
        !parseDouble(fields[3], event.frequency_hz) ||
        event.frequency_hz < 0.0 || fields[4].size() > 2'048U ||
        fields[5].empty() || fields[5].size() > 2'048U ||
        normalizedText(fields[5]) != fields[5] ||
        !parseCallsigns(fields[6], event.callsigns) ||
        (fields[7] != "0" && fields[7] != "1") ||
        (event_seen && event.start_sample < previous_start)) {
      error = "invalid annotation event fields at line " +
              std::to_string(line_number);
      return false;
    }
    event.literal_text = fields[4];
    event.normalized_text = fields[5];
    event.uncertain = fields[7] == "1";
    manifest.events.push_back(std::move(event));
    previous_start = manifest.events.back().start_sample;
    event_seen = true;
  }
  if (!header_seen || !rate_seen || !hash_seen ||
      (manifest.version == 1U && manifest.events.empty()) ||
      (manifest.version == 2U && manifest.coverage.empty())) {
    error = "annotation header, sample rate, hash, and reviewed data are required";
    return false;
  }
  for (const auto& event : manifest.events) {
    if (event.frequency_hz >
        static_cast<double>(manifest.sample_rate_hz) * 0.5) {
      error = "annotation frequency exceeds the audio Nyquist limit";
      return false;
    }
  }
  if (manifest.version == 2U) {
    for (const auto& event : manifest.events) {
      const bool contained = std::any_of(
          manifest.coverage.cbegin(), manifest.coverage.cend(),
          [&event](const auto& interval) {
        return event.start_sample >= interval.start_sample &&
               event.end_sample <= interval.end_sample;
      });
      if (!contained) {
        error = "annotation event lies outside reviewed coverage";
        return false;
      }
    }
  }
  return true;
}

}  // namespace cwassistant::test
