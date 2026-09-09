#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_decoder.hpp"

namespace {

using cwassistant::core::CwDecoderUpdate;
using cwassistant::core::CwMultiSpeedConfig;
using cwassistant::core::CwMultiSpeedDecoder;

std::string_view morse(const char symbol) {
  switch (symbol) {
    case 'A': return ".-";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'I': return "..";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'U': return "..-";
    case '0': return "-----";
    default: return {};
  }
}

struct Result {
  std::string text;
  double acoustic_wpm{0.0};
  float cadence_confidence{0.0F};
};

Result replayWeighted(const double weight, const bool paired_fit) {
  CwMultiSpeedConfig config;
  config.paired_cadence_fit = paired_fit;
  CwMultiSpeedDecoder decoder({}, config);
  constexpr std::string_view message = "CQ DE IU0LFQ";
  constexpr double wpm = 24.0;
  constexpr double dot_ms = 1'200.0 / wpm;
  constexpr double step_ms = 5.0;
  std::uint64_t now_ns = 0;
  CwDecoderUpdate latest;
  std::uint32_t jitter_state = 0x51f15eU;

  const auto advance = [&](const double requested_ms, const float evidence) {
    double remaining_ms = requested_ms;
    while (remaining_ms > 0.0) {
      const double elapsed_ms = std::min(step_ms, remaining_ms);
      now_ns += static_cast<std::uint64_t>(
          std::llround(elapsed_ms * 1'000'000.0));
      latest = decoder.process(now_ns, evidence);
      remaining_ms -= elapsed_ms;
    }
  };
  const auto jitter = [&](const double duration_ms) {
    jitter_state = jitter_state * 1'664'525U + 1'013'904'223U;
    const double centered =
        2.0 * static_cast<double>((jitter_state >> 8U) & 0xFFFFU) /
            65'535.0 -
        1.0;
    return duration_ms * (1.0 + 0.07 * centered);
  };

  advance(4.0 * dot_ms, 0.0F);
  for (std::size_t character = 0; character < message.size(); ++character) {
    if (message[character] == ' ') continue;
    const auto elements = morse(message[character]);
    for (std::size_t element = 0; element < elements.size(); ++element) {
      const double mark_units = elements[element] == '-' ? 3.0 : 1.0;
      // Weight moves the key-up edge: the mark gains weight-1 dot and the
      // following gap loses the same amount. The pair sum is invariant.
      advance(jitter((mark_units + weight - 1.0) * dot_ms), 14.0F);
      const bool character_ends = element + 1U == elements.size();
      const bool word_ends = character_ends &&
          character + 1U < message.size() && message[character + 1U] == ' ';
      const double gap_units = !character_ends ? 1.0 : (word_ends ? 7.0 : 3.0);
      advance(jitter((gap_units - (weight - 1.0)) * dot_ms), 0.0F);
    }
  }
  latest = decoder.flush(now_ns + 500'000'000ULL);
  while (!latest.text.empty() && latest.text.back() == ' ')
    latest.text.pop_back();
  return {.text = latest.text,
          .acoustic_wpm = latest.acoustic_wpm,
          .cadence_confidence = latest.acoustic_cadence_confidence};
}

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

std::size_t editDistance(const std::string_view left,
                         const std::string_view right) {
  std::vector<std::size_t> previous(right.size() + 1U);
  std::vector<std::size_t> current(right.size() + 1U);
  for (std::size_t index = 0; index <= right.size(); ++index)
    previous[index] = index;
  for (std::size_t row = 1; row <= left.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= right.size(); ++column) {
      current[column] = std::min({
          previous[column] + 1U, current[column - 1U] + 1U,
          previous[column - 1U] +
              (left[row - 1U] == right[column - 1U] ? 0U : 1U)});
    }
    previous.swap(current);
  }
  return previous.back();
}

}  // namespace

int main() {
  constexpr std::string_view expected = "CQ DE IU0LFQ";
  double paired_error = 0.0;
  double baseline_error = 0.0;
  std::size_t paired_edits = 0;
  std::size_t baseline_edits = 0;
  for (const double weight : {0.72, 0.82, 1.18, 1.28}) {
    const auto paired = replayWeighted(weight, true);
    const auto baseline = replayWeighted(weight, false);
    const double paired_case_error = std::abs(paired.acoustic_wpm - 24.0);
    const double baseline_case_error = std::abs(baseline.acoustic_wpm - 24.0);
    paired_error += paired_case_error;
    baseline_error += baseline_case_error;
    paired_edits += editDistance(expected, paired.text);
    baseline_edits += editDistance(expected, baseline.text);
    std::cout << "weight=" << weight << " paired_wpm="
              << paired.acoustic_wpm << " baseline_wpm="
              << baseline.acoustic_wpm << " paired_confidence="
              << paired.cadence_confidence << " baseline_confidence="
              << baseline.cadence_confidence << " paired_text=\""
              << paired.text << "\" baseline_text=\"" << baseline.text
              << "\"\n";
    expect(!paired.text.empty(), "weighted manual keying still decodes text");
    expect(paired.acoustic_wpm >= 18.0 && paired.acoustic_wpm <= 30.0,
           "paired cadence estimate stays in the true speed neighborhood");
  }
  std::cout << "paired_mean_absolute_wpm_error=" << paired_error / 4.0
            << " baseline_mean_absolute_wpm_error=" << baseline_error / 4.0
            << " paired_cer="
            << static_cast<double>(paired_edits) /
                   static_cast<double>(4U * expected.size())
            << " baseline_cer="
            << static_cast<double>(baseline_edits) /
                   static_cast<double>(4U * expected.size())
            << '\n';
  expect(paired_error + 1.0 < baseline_error,
         "paired cadence fit materially reduces weighted-keying WPM error");
  expect(paired_edits <= baseline_edits,
         "paired cadence fit does not worsen weighted-keying character error");
  return EXIT_SUCCESS;
}
