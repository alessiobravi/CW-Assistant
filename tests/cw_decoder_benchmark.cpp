#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_decoder.hpp"

namespace {

struct MorseEntry {
  char symbol;
  std::string_view elements;
};

constexpr MorseEntry kMorse[]{
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
    {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
    {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."}, {'0', "-----"}, {'1', ".----"},
    {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'/', "-..-."}, {'?', "..--.."}, {'+', ".-.-."}, {'=', "-...-"},
};

std::string_view elementsFor(const char symbol) {
  const auto found = std::find_if(
      std::begin(kMorse), std::end(kMorse),
      [symbol](const MorseEntry& entry) { return entry.symbol == symbol; });
  return found == std::end(kMorse) ? std::string_view{} : found->elements;
}

std::size_t editDistance(const std::string_view expected,
                         const std::string_view actual) {
  std::vector<std::size_t> previous(actual.size() + 1);
  std::vector<std::size_t> current(actual.size() + 1);
  for (std::size_t column = 0; column <= actual.size(); ++column)
    previous[column] = column;
  for (std::size_t row = 1; row <= expected.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= actual.size(); ++column) {
      const std::size_t substitution =
          previous[column - 1] +
          (expected[row - 1] == actual[column - 1] ? 0U : 1U);
      current[column] = std::min({previous[column] + 1,
                                  current[column - 1] + 1,
                                  substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

std::string trimSpaces(std::string value) {
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

struct ReplayResult {
  std::string text;
  double wpm{0.0};
  std::uint64_t updates{0};
  double simulated_seconds{0.0};
};

ReplayResult replayMessage(const std::string_view message, const double wpm,
                           const float mark_snr_db,
                           const double jitter_fraction) {
  cwassistant::core::CwMultiSpeedDecoder decoder;
  const double dot_ms = 1'200.0 / wpm;
  std::uint64_t now_ns = 0;
  std::uint64_t updates = 0;
  std::uint32_t jitter_state = 0x9e3779b9U;
  cwassistant::core::CwDecoderUpdate latest;
  const auto advance = [&](const double requested_ms, const float snr_db) {
    double remaining_ms = requested_ms;
    while (remaining_ms > 0.0) {
      const double step_ms = std::min(5.0, remaining_ms);
      now_ns += static_cast<std::uint64_t>(std::llround(step_ms * 1'000'000.0));
      latest = decoder.process(now_ns, snr_db);
      ++updates;
      remaining_ms -= step_ms;
    }
  };
  const auto jittered = [&](const double nominal_ms) {
    jitter_state = jitter_state * 1'664'525U + 1'013'904'223U;
    const double unit = static_cast<double>((jitter_state >> 8U) & 0xFFFFU) /
                        65'535.0;
    return nominal_ms * (1.0 + jitter_fraction * (2.0 * unit - 1.0));
  };

  advance(4.0 * dot_ms, 0.0F);
  for (std::size_t index = 0; index < message.size(); ++index) {
    if (message[index] == ' ') continue;
    const std::string_view elements = elementsFor(message[index]);
    for (std::size_t element = 0; element < elements.size(); ++element) {
      const double marks = elements[element] == '-' ? 3.0 : 1.0;
      advance(jittered(marks * dot_ms), mark_snr_db);
      if (element + 1 < elements.size()) advance(jittered(dot_ms), 0.0F);
    }
    const bool word_ends = index + 1 < message.size() &&
                           message[index + 1] == ' ';
    advance(jittered((word_ends ? 7.0 : 3.0) * dot_ms), 0.0F);
  }
  latest = decoder.flush(now_ns +
                         static_cast<std::uint64_t>(10.0 * dot_ms * 1'000'000.0));
  return {.text = trimSpaces(latest.text),
          .wpm = latest.wpm,
          .updates = updates,
          .simulated_seconds = static_cast<double>(now_ns) / 1'000'000'000.0};
}

ReplayResult replaySpeedChange() {
  cwassistant::core::CwMultiSpeedDecoder decoder;
  std::uint64_t now_ns = 0;
  std::uint64_t updates = 0;
  cwassistant::core::CwDecoderUpdate latest;
  const auto advance = [&](const double requested_ms, const float snr_db) {
    double remaining_ms = requested_ms;
    while (remaining_ms > 0.0) {
      const double step_ms = std::min(5.0, remaining_ms);
      now_ns += static_cast<std::uint64_t>(std::llround(step_ms * 1'000'000.0));
      latest = decoder.process(now_ns, snr_db);
      ++updates;
      remaining_ms -= step_ms;
    }
  };
  const auto send = [&](const std::string_view message, const double wpm) {
    const double dot_ms = 1'200.0 / wpm;
    for (std::size_t index = 0; index < message.size(); ++index) {
      const std::string_view elements = elementsFor(message[index]);
      for (std::size_t element = 0; element < elements.size(); ++element) {
        advance((elements[element] == '-' ? 3.0 : 1.0) * dot_ms, 14.0F);
        if (element + 1 < elements.size()) advance(dot_ms, 0.0F);
      }
      advance(3.0 * dot_ms, 0.0F);
    }
  };

  advance(300.0, 0.0F);
  send("CQ", 12.0);
  advance(3'000.0, 0.0F);
  send("TEST", 40.0);
  latest = decoder.flush(now_ns + 1'000'000'000);
  return {.text = trimSpaces(latest.text), .wpm = latest.wpm,
          .updates = updates,
          .simulated_seconds = static_cast<double>(now_ns) / 1'000'000'000.0};
}

}  // namespace

int main() {
  struct BenchmarkCase {
    const char* name;
    const char* message;
    double wpm;
    float snr_db;
    double jitter;
  };
  constexpr BenchmarkCase cases[]{
      {"very-slow-acquisition", "SOS", 8.0, 14.0F, 0.03},
      {"slow-clean", "CQ TEST 123", 12.0, 14.0F, 0.04},
      {"nominal-weak", "W1AW 599", 20.0, 7.2F, 0.08},
      {"fast-jitter", "AD2FC/P", 25.0, 9.0F, 0.12},
      {"high-speed", "CQ DX", 40.0, 11.0F, 0.06},
      {"very-high-speed", "TEST 5NN", 55.0, 13.0F, 0.04},
  };

  const auto wall_start = std::chrono::steady_clock::now();
  std::size_t expected_characters = 0;
  std::size_t edits = 0;
  std::size_t speed_failures = 0;
  std::uint64_t updates = 0;
  double simulated_seconds = 0.0;
  for (const auto& benchmark : cases) {
    const ReplayResult result = replayMessage(
        benchmark.message, benchmark.wpm, benchmark.snr_db,
        benchmark.jitter);
    const std::size_t case_edits =
        editDistance(benchmark.message, result.text);
    const double speed_error =
        std::abs(result.wpm - benchmark.wpm) / benchmark.wpm;
    if (speed_error > 0.25) ++speed_failures;
    expected_characters += std::string_view(benchmark.message).size();
    edits += case_edits;
    updates += result.updates;
    simulated_seconds += result.simulated_seconds;
    std::cout << "case=" << benchmark.name
              << " wpm=" << benchmark.wpm
              << " snr_db=" << benchmark.snr_db
              << " acquired_wpm=" << result.wpm
              << " speed_error=" << speed_error
              << " edits=" << case_edits
              << " expected=\"" << benchmark.message
              << "\" actual=\"" << result.text << "\"\n";
  }

  const ReplayResult speed_change = replaySpeedChange();
  const std::size_t speed_change_edits =
      editDistance("CQ TEST", speed_change.text);
  edits += speed_change_edits;
  expected_characters += std::string_view("CQ TEST").size();
  updates += speed_change.updates;
  simulated_seconds += speed_change.simulated_seconds;
  if (std::abs(speed_change.wpm - 40.0) / 40.0 > 0.25) ++speed_failures;
  std::cout << "case=midstream-speed-change wpm=12_to_40"
            << " acquired_wpm=" << speed_change.wpm
            << " edits=" << speed_change_edits
            << " expected=\"CQ TEST\" actual=\""
            << speed_change.text << "\"\n";

  cwassistant::core::CwMultiSpeedDecoder silence_decoder;
  std::uint64_t silence_time_ns = 0;
  cwassistant::core::CwDecoderUpdate silence;
  std::uint32_t noise_state = 0x12345678U;
  constexpr std::uint64_t silence_updates = 6'000;
  for (std::uint64_t sample = 0; sample < silence_updates; ++sample) {
    noise_state = noise_state * 1'103'515'245U + 12'345U;
    const float noise_snr =
        0.5F + 2.8F * static_cast<float>((noise_state >> 16U) & 0x7FFFU) /
                   32'767.0F;
    silence_time_ns += 10'000'000;
    silence = silence_decoder.process(silence_time_ns, noise_snr);
  }
  silence = silence_decoder.flush(silence_time_ns + 500'000'000);
  const std::size_t false_characters = trimSpaces(silence.text).size();

  const auto wall_end = std::chrono::steady_clock::now();
  const double wall_seconds =
      std::chrono::duration<double>(wall_end - wall_start).count();
  const double character_error_rate = expected_characters == 0
      ? 0.0
      : static_cast<double>(edits) /
            static_cast<double>(expected_characters);
  const double total_simulated_seconds = simulated_seconds + 60.0;
  const double real_time_factor = total_simulated_seconds > 0.0
      ? wall_seconds / total_simulated_seconds
      : 0.0;
  std::cout << "summary expected_characters=" << expected_characters
            << " edits=" << edits
            << " speed_failures=" << speed_failures
            << " cer=" << character_error_rate
            << " false_characters_per_noise_minute=" << false_characters
            << " updates=" << (updates + silence_updates)
            << " simulated_seconds=" << total_simulated_seconds
            << " wall_seconds=" << wall_seconds
            << " real_time_factor=" << real_time_factor
            << " timing_hypotheses=" << silence_decoder.hypothesisCount()
            << " decoder_state_bytes=" << silence_decoder.stateBytes()
            << '\n';

  return edits == 0 && speed_failures == 0 && false_characters == 0
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
