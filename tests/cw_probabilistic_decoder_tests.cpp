#include "cwassistant/core/cw_probabilistic_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using cwassistant::core::CwProbabilisticDecoderUpdate;
using cwassistant::core::CwProbabilisticMorseDecoder;

void expect(const bool condition, const std::string_view message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

std::string_view elementsFor(const char symbol) {
  struct Entry { char symbol; std::string_view elements; };
  static constexpr Entry table[]{
      {'A', ".-"}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
      {'F', "..-."}, {'I', ".."}, {'L', ".-.."}, {'N', "-."},
      {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
      {'W', ".--"}, {'Y', "-.--"}, {'1', ".----"},
      {'2', "..---"}, {'3', "...--"}, {'5', "....."},
      {'9', "----."},
  };
  for (const auto& entry : table) {
    if (entry.symbol == symbol) return entry.elements;
  }
  return {};
}

struct Fixture {
  CwProbabilisticMorseDecoder decoder;
  std::uint64_t now_ns{0};
  CwProbabilisticDecoderUpdate latest;
  std::string prior_stable;
  bool append_only{true};

  void advance(const double duration_ms, const float probability) {
    double remaining = duration_ms;
    while (remaining > 0.0) {
      const double step = std::min(5.0, remaining);
      now_ns += static_cast<std::uint64_t>(std::llround(step * 1'000'000.0));
      latest = decoder.process({.timestamp_ns = now_ns,
                                .key_down_probability = probability});
      if (latest.changed) {
        append_only = append_only &&
            latest.stable_text.starts_with(prior_stable);
        prior_stable = latest.stable_text;
      }
      remaining -= step;
    }
  }

  void send(const std::string_view text, const double wpm,
            const double character_gap_dots = 3.0,
            const float mark_probability = 0.96F,
            const float gap_probability = 0.04F) {
    const double dot_ms = 1'200.0 / wpm;
    for (std::size_t index = 0; index < text.size(); ++index) {
      if (text[index] == ' ') continue;
      const auto elements = elementsFor(text[index]);
      for (std::size_t element = 0; element < elements.size(); ++element) {
        advance((elements[element] == '-' ? 3.0 : 1.0) * dot_ms,
                mark_probability);
        if (element + 1U < elements.size())
          advance(dot_ms, gap_probability);
      }
      const bool word = index + 1U < text.size() && text[index + 1U] == ' ';
      advance((word ? 7.0 : character_gap_dots) * dot_ms,
              gap_probability);
    }
  }

  CwProbabilisticDecoderUpdate flush(const double tail_ms = 1'000.0) {
    latest = decoder.flush(now_ns + static_cast<std::uint64_t>(
        std::llround(tail_ms * 1'000'000.0)));
    append_only = append_only && latest.stable_text.starts_with(prior_stable);
    return latest;
  }
};

std::string trim(std::string value) {
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

void testCompressedCallsignAndStableContract() {
  Fixture fixture;
  fixture.advance(200.0, 0.03F);
  fixture.send("EA1EYL", 22.0, 2.0);
  const auto result = fixture.flush();
  if (trim(result.stable_text) != "EA1EYL") {
    std::cerr << "stable='" << result.stable_text << "' provisional='"
              << result.provisional_text << "' wpm=" << result.estimated_wpm
              << " alternatives=" << result.alternatives.size() << '\n';
    for (const auto& alternative : result.alternatives) {
      std::cerr << "  '" << alternative.text << "' elem='"
                << alternative.provisional_elements << "' wpm="
                << alternative.wpm << " cost=" << alternative.acoustic_cost
                << " evidence=" << alternative.evidence_confidence << '\n';
    }
  }
  expect(trim(result.stable_text) == "EA1EYL",
         "probability decoder recovers compressed callsign boundaries");
  expect(fixture.append_only,
         "stable probability-decoder text is append-only");
  expect(!result.stable_text.empty() && result.stable_text.back() == ' ',
         "flush records an explicit end-of-segment word boundary");
  expect(result.estimated_wpm >= 18.0 && result.estimated_wpm <= 27.0,
         "adaptive timing stays in the supplied cadence neighborhood");
}

void testSpeedRangeAndNBest() {
  for (const double wpm : {8.0, 18.0, 32.0, 55.0}) {
    Fixture fixture;
    fixture.advance(200.0, 0.02F);
    fixture.send("CQ 5NN", wpm);
    const auto result = fixture.flush();
    if (trim(result.stable_text) != "CQ 5NN") {
      std::cerr << "speed " << wpm << " stable='" << result.stable_text
                << "' provisional='" << result.provisional_text
                << "' estimated=" << result.estimated_wpm << '\n';
      for (const auto& alternative : result.alternatives) {
        std::cerr << "  '" << alternative.text << "' wpm="
                  << alternative.wpm << " cost=" << alternative.acoustic_cost
                  << " evidence=" << alternative.evidence_confidence << '\n';
      }
    }
    expect(trim(result.stable_text) == "CQ 5NN",
           "bounded timing search decodes the supported WPM range");
    expect(result.alternatives.size() >= 2U &&
               result.alternatives.size() <= 8U,
           "N-best acoustic alternatives remain explicitly bounded");
    expect(fixture.decoder.stateBytes() <= 256U * 1'024U,
           "probabilistic decoder state remains within the core budget");
  }
}

void testUncertainEvidenceAndNoise() {
  CwProbabilisticMorseDecoder ambiguous;
  std::uint64_t now = 0;
  for (int sample = 0; sample < 20; ++sample) {
    now += 5'000'000;
    static_cast<void>(ambiguous.process({now, 0.72F}));
  }
  for (int sample = 0; sample < 40; ++sample) {
    now += 5'000'000;
    static_cast<void>(ambiguous.process({now, 0.28F}));
  }
  const auto uncertain = ambiguous.flush(now + 500'000'000);
  expect(uncertain.alternatives.size() >= 2U,
         "ambiguous probability timing retains multiple acoustic paths");

  CwProbabilisticMorseDecoder noise;
  now = 0;
  CwProbabilisticDecoderUpdate latest;
  for (int sample = 0; sample < 12'000; ++sample) {
    now += 5'000'000;
    const float probability = 0.45F +
        0.10F * static_cast<float>((sample * 37) % 101) / 100.0F;
    latest = noise.process({now, probability});
  }
  latest = noise.flush(now + 500'000'000);
  expect(trim(latest.stable_text).empty() &&
             latest.provisional_text.empty(),
         "uncertain noise probabilities cannot manufacture Morse text");
}

void testTransitionDebounceAndFlushBoundary() {
  Fixture isolated_spike;
  isolated_spike.advance(100.0, 0.02F);
  isolated_spike.advance(5.0, 0.98F);
  isolated_spike.advance(100.0, 0.02F);
  const auto isolated_result = isolated_spike.flush();
  expect(trim(isolated_result.stable_text).empty() &&
             isolated_result.provisional_text.empty(),
         "one probability frame cannot manufacture a mark");

  Fixture dropout;
  dropout.advance(100.0, 0.02F);
  dropout.advance(25.0, 0.98F);
  dropout.advance(5.0, 0.02F);
  dropout.advance(30.0, 0.98F);
  dropout.advance(180.0, 0.02F);
  const auto dropout_result = dropout.flush();
  expect(trim(dropout_result.stable_text) == "E",
         "one probability dropout cannot split a valid mark");

  Fixture pending_at_flush;
  pending_at_flush.advance(100.0, 0.02F);
  pending_at_flush.advance(5.0, 0.98F);
  const auto pending_result = pending_at_flush.flush(60'000.0);
  expect(trim(pending_result.stable_text).empty() &&
             pending_result.provisional_text.empty(),
         "flush does not promote an unconfirmed transition");

  Fixture immediate_flush;
  Fixture delayed_flush;
  immediate_flush.advance(100.0, 0.02F);
  delayed_flush.advance(100.0, 0.02F);
  immediate_flush.send("E", 55.0);
  delayed_flush.send("E", 55.0);
  const auto immediate = immediate_flush.flush(0.0);
  const auto delayed = delayed_flush.flush(300'000.0);
  expect(immediate.stable_text == delayed.stable_text &&
             immediate.provisional_text == delayed.provisional_text &&
             !immediate.alternatives.empty() &&
             !delayed.alternatives.empty() &&
             immediate.alternatives.front().text ==
                 delayed.alternatives.front().text,
         "wall-clock delay before flush cannot alter acoustic evidence");

  const auto repeated = delayed_flush.flush(600'000.0);
  expect(repeated.stable_text == delayed.stable_text &&
             repeated.provisional_text == delayed.provisional_text &&
             repeated.alternatives.size() == delayed.alternatives.size() &&
             (!repeated.alternatives.empty() &&
              repeated.alternatives.front().text ==
                  delayed.alternatives.front().text),
         "repeated flush cannot duplicate the final acoustic run or text");
}

}  // namespace

int main() {
  testCompressedCallsignAndStableContract();
  testSpeedRangeAndNBest();
  testUncertainEvidenceAndNoise();
  testTransitionDebounceAndFlushBoundary();
  std::cout << "cw_probabilistic_decoder_tests: PASS\n";
}
