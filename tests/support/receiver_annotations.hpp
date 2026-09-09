#pragma once

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <istream>
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

struct ReceiverAnnotationManifest {
  std::uint32_t sample_rate_hz{0};
  std::string audio_sha256;
  std::vector<ReceiverAnnotation> events;
};

inline bool parseUnsigned(const std::string_view text,
                          std::uint64_t& value) {
  if (text.empty()) return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                      value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

inline bool parseDouble(const std::string_view text, double& value) {
  if (text.empty()) return false;
  const std::string owned(text);
  char* end = nullptr;
  errno = 0;
  value = std::strtod(owned.c_str(), &end);
  return errno == 0 && end == owned.c_str() + owned.size() &&
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
//   CWA-RECEIVER-ANNOTATIONS\t1
//   sample_rate_hz\t<integer>
//   audio_sha256\t<64 lowercase hex characters>
//   event\t<start sample>\t<end sample>\t<Hz>\t<literal>\t<normalized>
//         \t<comma-separated exact callsigns>\t<0|1 uncertain>
// Text fields may contain spaces but not tabs or newlines. Uncertain events are
// retained for review and excluded from quality scores.
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
      if (fields.size() != 2U || fields[0] != "CWA-RECEIVER-ANNOTATIONS" ||
          fields[1] != "1") {
        error = "first data line must declare annotation format version 1";
        return false;
      }
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
  if (!header_seen || !rate_seen || !hash_seen || manifest.events.empty()) {
    error = "annotation header, sample rate, hash, and events are required";
    return false;
  }
  for (const auto& event : manifest.events) {
    if (event.frequency_hz >
        static_cast<double>(manifest.sample_rate_hz) * 0.5) {
      error = "annotation frequency exceeds the audio Nyquist limit";
      return false;
    }
  }
  return true;
}

}  // namespace cwassistant::test
