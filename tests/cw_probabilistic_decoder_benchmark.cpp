#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "cwassistant/core/cw_probabilistic_decoder.hpp"

namespace {

std::string_view elementsFor(const char symbol) {
  struct Entry { char symbol; std::string_view elements; };
  static constexpr Entry table[]{
      {'C', "-.-."}, {'E', "."}, {'N', "-."}, {'Q', "--.-"},
      {'S', "..."}, {'T', "-"}, {'5', "....."},
  };
  for (const auto& entry : table) {
    if (entry.symbol == symbol) return entry.elements;
  }
  return {};
}

std::string trim(std::string value) {
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

struct Result {
  std::string text;
  bool append_only{true};
  bool quiet_samples_allocation_free{true};
  std::size_t state_bytes{0};
  double audio_seconds{0.0};
};

Result replay(const double wpm, const double jitter_fraction) {
  cwassistant::core::CwProbabilisticMorseDecoder decoder;
  constexpr std::string_view message{"CQ TEST 5NN"};
  const double dot_ms = 1'200.0 / wpm;
  std::uint64_t now_ns = 0;
  std::uint32_t random = 0x6d2b79f5U;
  std::string previous;
  bool append_only = true;
  bool quiet_samples_allocation_free = true;
  auto jitter = [&](const double nominal_ms) {
    random = random * 1'664'525U + 1'013'904'223U;
    const double centered =
        static_cast<double>((random >> 8U) & 0xffffU) / 32'767.5 - 1.0;
    return nominal_ms * (1.0 + jitter_fraction * centered);
  };
  const auto advance = [&](double duration_ms, const float probability) {
    while (duration_ms > 0.0) {
      const double step_ms = std::min(5.0, duration_ms);
      now_ns += static_cast<std::uint64_t>(std::llround(
          step_ms * 1'000'000.0));
      const auto update = decoder.process({now_ns, probability});
      if (update.changed) {
        append_only = append_only && update.stable_text.starts_with(previous);
        previous = update.stable_text;
      } else {
        quiet_samples_allocation_free = quiet_samples_allocation_free &&
            update.stable_text.empty() && update.provisional_text.empty() &&
            update.alternatives.empty();
      }
      duration_ms -= step_ms;
    }
  };

  advance(4.0 * dot_ms, 0.03F);
  for (std::size_t index = 0; index < message.size(); ++index) {
    if (message[index] == ' ') continue;
    const auto elements = elementsFor(message[index]);
    for (std::size_t element = 0; element < elements.size(); ++element) {
      advance(jitter((elements[element] == '-' ? 3.0 : 1.0) * dot_ms),
              0.94F);
      if (element + 1U < elements.size())
        advance(jitter(dot_ms), 0.06F);
    }
    const bool word = index + 1U < message.size() &&
                      message[index + 1U] == ' ';
    advance(jitter((word ? 7.0 : 3.0) * dot_ms), 0.06F);
  }
  const auto final = decoder.flush(now_ns + 2'000'000'000ULL);
  append_only = append_only && final.stable_text.starts_with(previous);
  if (trim(final.stable_text) != message) {
    std::cerr << "final stable='" << final.stable_text << "' provisional='"
              << final.provisional_text << "' elements='"
              << final.provisional_elements << "' estimated="
              << final.estimated_wpm << '\n';
    for (const auto& alternative : final.alternatives) {
      std::cerr << "  alt='" << alternative.text << "' wpm="
                << alternative.wpm << " cost=" << alternative.acoustic_cost
                << " conf=" << alternative.evidence_confidence << '\n';
    }
  }
  return {.text = trim(final.stable_text),
          .append_only = append_only,
          .quiet_samples_allocation_free = quiet_samples_allocation_free,
          .state_bytes = decoder.stateBytes(),
          .audio_seconds = static_cast<double>(now_ns) / 1'000'000'000.0};
}

}  // namespace

int main() {
  const auto started = std::chrono::steady_clock::now();
  double audio_seconds = 0.0;
  std::size_t failures = 0;
  std::size_t maximum_state = 0;
  for (const double wpm : {8.0, 18.0, 32.0, 55.0}) {
    const auto result = replay(wpm, 0.08);
    audio_seconds += result.audio_seconds;
    maximum_state = std::max(maximum_state, result.state_bytes);
    if (result.text != "CQ TEST 5NN" || !result.append_only ||
        !result.quiet_samples_allocation_free) {
      ++failures;
      std::cerr << "case wpm=" << wpm << " text='" << result.text
                << "' append_only=" << result.append_only
                << " sparse_updates="
                << result.quiet_samples_allocation_free << '\n';
    }
  }
  const double wall_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const double realtime_factor = wall_seconds / audio_seconds;
  std::cout << "probabilistic benchmark: audio=" << audio_seconds
            << "s wall=" << wall_seconds << "s rtf=" << realtime_factor
            << " state=" << maximum_state << " bytes\n";
  if (failures != 0U || maximum_state > 256U * 1'024U ||
      realtime_factor > 0.20) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
