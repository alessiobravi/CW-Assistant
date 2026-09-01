#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"

namespace {

enum class Scenario {
  CleanCw,
  FasterCw,
  WeakFadingDriftCw,
  SteadyCarrier,
  SpeechLikeAm,
  IrregularImpulses,
  PumpingBroadbandNoise,
};

struct ScenarioResult {
  std::size_t maximum_published_tracks{0};
  double acquisition_seconds{-1.0};
  cwassistant::core::CwVerificationDiagnostics diagnostics;
};

std::vector<bool> sosKeyingSteps(const std::size_t steps_per_unit) {
  std::vector<bool> units;
  const auto append = [&units](const bool keyed, const std::size_t count) {
    units.insert(units.end(), count, keyed);
  };
  const auto letter = [&append](const std::string_view elements) {
    for (std::size_t index = 0; index < elements.size(); ++index) {
      append(true, elements[index] == '.' ? 1U : 3U);
      append(false, index + 1 == elements.size() ? 3U : 1U);
    }
  };
  letter("...");
  letter("---");
  letter("...");
  append(false, 4);  // Complete the seven-unit word gap.

  std::vector<bool> steps;
  for (const bool keyed : units)
    steps.insert(steps.end(), steps_per_unit, keyed);
  return steps;
}

ScenarioResult runScenario(const Scenario scenario,
                           const double duration_seconds) {
  constexpr double sample_rate_hz = 8'000.0;
  constexpr std::size_t samples_per_step = 80;
  constexpr double base_tone_hz = 400.0;
  const auto slow_cw_steps = sosKeyingSteps(10);  // 12 WPM.
  const auto fast_cw_steps = sosKeyingSteps(4);   // 30 WPM.
  const std::array<int, 12> irregular_durations{
      1, 2, 1, 37, 2, 1, 48, 1, 3, 26, 1, 61};

  cwassistant::core::CwChannelBank bank;
  std::vector<float> bins(1'001, -105.0F);
  std::uint32_t noise_state = 0xA53C9E17U;
  std::size_t irregular_index = 0;
  int irregular_remaining = irregular_durations.front();
  bool irregular_keyed = false;
  double phase = 0.0;
  ScenarioResult result;
  const int steps = static_cast<int>(duration_seconds * 100.0);

  for (int step = 0; step < steps; ++step) {
    const double seconds = static_cast<double>(step) * 0.01;
    bool keyed = false;
    float amplitude = 0.0F;
    double tone_hz = base_tone_hz;
    switch (scenario) {
      case Scenario::CleanCw:
        keyed = slow_cw_steps[static_cast<std::size_t>(step) %
                              slow_cw_steps.size()];
        amplitude = keyed ? 0.24F : 0.0F;
        break;
      case Scenario::FasterCw:
        keyed = fast_cw_steps[static_cast<std::size_t>(step) %
                              fast_cw_steps.size()];
        amplitude = keyed ? 0.20F : 0.0F;
        break;
      case Scenario::WeakFadingDriftCw:
        keyed = slow_cw_steps[static_cast<std::size_t>(step) %
                              slow_cw_steps.size()];
        amplitude = keyed ? 0.075F * static_cast<float>(
            0.78 + 0.22 * std::sin(2.0 * std::numbers::pi * 0.7 * seconds))
                          : 0.0F;
        tone_hz += 0.8 * seconds;
        break;
      case Scenario::SteadyCarrier:
        keyed = true;
        amplitude = 0.20F;
        break;
      case Scenario::SpeechLikeAm:
        keyed = true;
        amplitude = 0.13F + 0.055F * static_cast<float>(
            std::sin(2.0 * std::numbers::pi * 3.1 * seconds) +
            0.45 * std::sin(2.0 * std::numbers::pi * 7.3 * seconds));
        break;
      case Scenario::IrregularImpulses:
        if (--irregular_remaining <= 0) {
          irregular_index = (irregular_index + 1) % irregular_durations.size();
          irregular_remaining = irregular_durations[irregular_index];
          irregular_keyed = !irregular_keyed;
        }
        keyed = irregular_keyed;
        amplitude = keyed ? 0.28F : 0.0F;
        break;
      case Scenario::PumpingBroadbandNoise:
        break;
    }

    bins.assign(bins.size(), -105.0F);
    if (scenario == Scenario::PumpingBroadbandNoise) {
      const float pump = 7.0F * static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 0.9 * seconds));
      for (std::size_t bin = 0; bin < bins.size(); ++bin) {
        bins[bin] = -88.0F + pump +
            5.0F * static_cast<float>(std::sin(0.07 * bin + seconds)) +
            2.0F * static_cast<float>(std::sin(0.73 * bin - seconds));
      }
    } else if (keyed) {
      const auto tone_bin = static_cast<std::size_t>(std::llround(tone_hz));
      bins[tone_bin] = scenario == Scenario::WeakFadingDriftCw
          ? -78.0F : -68.0F;
    }

    const auto timestamp_ns = static_cast<std::uint64_t>(step) * 10'000'000;
    static_cast<void>(bank.updateSpectrum(timestamp_ns, 0.0, 1'000.0, bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate_hz;
    block.timestamp_ns = timestamp_ns;
    block.sequence = static_cast<std::uint64_t>(step);
    block.sample_count = samples_per_step;
    for (std::size_t sample = 0; sample < block.sample_count; ++sample) {
      noise_state = noise_state * 1'664'525U + 1'013'904'223U;
      const float white = static_cast<float>((noise_state >> 8U) & 0xFFFFU) /
                              32'767.5F - 1.0F;
      const float noise_amplitude = scenario == Scenario::PumpingBroadbandNoise
          ? 0.16F
          : scenario == Scenario::WeakFadingDriftCw ? 0.002F : 0.001F;
      block.samples[sample] = {
          amplitude * static_cast<float>(std::sin(phase)) +
              noise_amplitude * white,
          0.0F};
      phase += 2.0 * std::numbers::pi * tone_hz / sample_rate_hz;
    }
    const auto& channels = bank.processSamples(block);
    result.maximum_published_tracks = std::max(
        result.maximum_published_tracks, channels.size());
    if (result.acquisition_seconds < 0.0 && !channels.empty())
      result.acquisition_seconds = seconds;
  }
  result.diagnostics = bank.verificationDiagnostics();
  return result;
}

const char* scenarioName(const Scenario scenario) {
  switch (scenario) {
    case Scenario::CleanCw: return "clean-cw";
    case Scenario::FasterCw: return "faster-cw";
    case Scenario::WeakFadingDriftCw: return "weak-fading-drift-cw";
    case Scenario::SteadyCarrier: return "steady-carrier";
    case Scenario::SpeechLikeAm: return "speech-like-am";
    case Scenario::IrregularImpulses: return "irregular-impulses";
    case Scenario::PumpingBroadbandNoise: return "pumping-broadband-noise";
  }
  return "unknown";
}

}  // namespace

int main() {
  constexpr std::array positive_scenarios{
      Scenario::CleanCw, Scenario::FasterCw,
      Scenario::WeakFadingDriftCw};
  constexpr std::array negative_scenarios{
      Scenario::SteadyCarrier, Scenario::SpeechLikeAm,
      Scenario::IrregularImpulses, Scenario::PumpingBroadbandNoise};
  constexpr double acquisition_target_seconds = 6.0;
  constexpr double maximum_realtime_factor = 0.20;

  const auto wall_start = std::chrono::steady_clock::now();
  std::size_t acquired = 0;
  std::size_t false_published_tracks = 0;
  bool acquisition_target_met = true;
  for (const auto scenario : positive_scenarios) {
    const auto result = runScenario(scenario, 16.0);
    if (result.maximum_published_tracks == 1) ++acquired;
    if (result.acquisition_seconds < 0.0 ||
        result.acquisition_seconds > acquisition_target_seconds) {
      acquisition_target_met = false;
    }
    std::cout << "scenario=" << scenarioName(scenario)
              << " published=" << result.maximum_published_tracks
              << " acquisition_seconds=" << result.acquisition_seconds
              << " verified_transitions="
              << result.diagnostics.verified_transitions
              << " candidates=" << result.diagnostics.candidate_tracks
              << " morse_likely=" << result.diagnostics.morse_likely_tracks
              << " symbols=" << result.diagnostics.maximum_decoded_symbols
              << " transitions=" << result.diagnostics.maximum_key_transitions
              << " timing=" << result.diagnostics.best_timing_quality
              << " cadence=" << result.diagnostics.best_cadence_quality
              << " coherence="
              << result.diagnostics.best_narrowband_coherence;
    for (std::size_t reason = 0;
         reason < result.diagnostics.current_reason_counts.size(); ++reason) {
      if (result.diagnostics.current_reason_counts[reason] > 0)
        std::cout << " reason_" << reason << "="
                  << result.diagnostics.current_reason_counts[reason];
    }
    std::cout << '\n';
  }
  for (const auto scenario : negative_scenarios) {
    const auto result = runScenario(scenario, 10.0);
    false_published_tracks += result.maximum_published_tracks;
    std::cout << "scenario=" << scenarioName(scenario)
              << " published=" << result.maximum_published_tracks
              << " expired_unverified="
              << result.diagnostics.expired_unverified_tracks
              << " coherence="
              << result.diagnostics.best_narrowband_coherence << '\n';
  }
  const auto wall_end = std::chrono::steady_clock::now();
  constexpr double simulated_seconds = 88.0;
  const double wall_seconds =
      std::chrono::duration<double>(wall_end - wall_start).count();
  const double realtime_factor = wall_seconds / simulated_seconds;
  std::cout << "summary positives_acquired=" << acquired << "/"
            << positive_scenarios.size()
            << " false_published_tracks=" << false_published_tracks
            << " acquisition_target_seconds=" << acquisition_target_seconds
            << " wall_seconds=" << wall_seconds
            << " realtime_factor=" << realtime_factor
            << " maximum_realtime_factor=" << maximum_realtime_factor << '\n';

  return acquired == positive_scenarios.size() &&
      false_published_tracks == 0 && acquisition_target_met &&
      realtime_factor <= maximum_realtime_factor
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
