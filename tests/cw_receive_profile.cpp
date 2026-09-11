// Where the live receive path spends its time, as a function of how many
// signals are being tracked.
//
// PERF-002 asks for per-stage wall time and accepts an optimisation only after
// a before-and-after measurement. The specific claim this exists to test is
// that the per-track down-conversion dominates and grows with the number of
// tracked signals: each track mixes the input to baseband with three
// oscillators and runs five three-stage complex filter cascades per sample,
// and for every track except the monitored one the complex result is consumed
// only by std::norm. If that is where the time goes, replacing it with power
// taken from the transform the spectrum path already computes would be close
// to free and flat in track count. If it is not, that work is not worth doing.
//
// Reports a real-time factor: seconds of processor time per second of audio.
// Absolute figures depend on the machine, so what matters is the shape against
// track count and the difference between two builds on the same machine.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"

using cwassistant::core::CwChannelBank;
using cwassistant::core::CwChannelBankConfig;
using cwassistant::core::RealtimeSampleBlock;
using cwassistant::core::SpectrumAnalyzer;

namespace {

constexpr double kSampleRate = 48'000.0;
constexpr double kSeconds = 20.0;

// Keyed tones at distinct frequencies, so the bank acquires and then tracks
// one signal per tone rather than measuring an idle pipeline.
std::vector<float> scene(const std::size_t tone_count) {
  const auto total = static_cast<std::size_t>(kSeconds * kSampleRate);
  std::vector<float> audio(total, 0.0F);
  const double dot_seconds = 1.2 / 22.0;
  for (std::size_t tone = 0; tone < tone_count; ++tone) {
    const double frequency = 500.0 + 70.0 * static_cast<double>(tone);
    // A different keying phase per tone keeps them from switching together,
    // which would make the whole scene unrealistically correlated.
    const double offset = 0.37 * static_cast<double>(tone);
    double phase = 0.0;
    for (std::size_t index = 0; index < total; ++index) {
      const double seconds = static_cast<double>(index) / kSampleRate + offset;
      const auto element = static_cast<long long>(seconds / dot_seconds);
      const bool keyed = (element % 3) != 2;
      phase += 2.0 * std::numbers::pi * frequency / kSampleRate;
      if (keyed) audio[index] += 0.08F * static_cast<float>(std::sin(phase));
    }
  }
  return audio;
}

struct Timing {
  double spectrum_seconds{0.0};
  double samples_seconds{0.0};
  std::size_t tracks{0};
};

Timing measure(const std::size_t tone_count) {
  const auto audio = scene(tone_count);
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank bank{CwChannelBankConfig{}};
  RealtimeSampleBlock block;
  block.stream.sample_rate_hz = kSampleRate;
  Timing timing;
  std::size_t position = 0;
  std::uint64_t now = 0;
  while (position < audio.size()) {
    const std::size_t take = std::min<std::size_t>(1'024,
                                                   audio.size() - position);
    block.sample_count = take;
    block.timestamp_ns = now;
    for (std::size_t index = 0; index < take; ++index)
      block.samples[index] = {audio[position + index], 0.0F};

    const auto spectrum_start = std::chrono::steady_clock::now();
    for (const auto& snapshot : analyzer.process(block)) {
      static_cast<void>(bank.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.instantaneous_bins_dbfs,
          false));
    }
    const auto samples_start = std::chrono::steady_clock::now();
    static_cast<void>(bank.processSamples(block));
    const auto done = std::chrono::steady_clock::now();

    timing.spectrum_seconds +=
        std::chrono::duration<double>(samples_start - spectrum_start).count();
    timing.samples_seconds +=
        std::chrono::duration<double>(done - samples_start).count();
    // Every tracked signal runs the per-track chain whether or not it has
    // been verified and published, so the count that explains the time is
    // the tracked one, not the published one.
    timing.tracks = std::max(timing.tracks,
                             bank.allTrackDiagnostics().size());
    position += take;
    now += static_cast<std::uint64_t>(
        static_cast<long double>(take) * 1'000'000'000.0L / kSampleRate);
  }
  return timing;
}

}  // namespace

int main() {
  std::printf("live receive path, %.0f s of audio per case\n", kSeconds);
  std::printf("  %-6s %-8s %12s %12s %12s\n", "tones", "tracked",
              "spectrum", "samples", "real-time");
  double first_per_track = 0.0;
  for (const std::size_t tones : {1U, 4U, 8U, 16U, 24U}) {
    const auto timing = measure(tones);
    const double total = timing.spectrum_seconds + timing.samples_seconds;
    std::printf("  %-6zu %-8zu %10.3f s %10.3f s %11.4f\n", tones,
                timing.tracks, timing.spectrum_seconds, timing.samples_seconds,
                total / kSeconds);
    if (tones == 1U) first_per_track = timing.samples_seconds;
  }
  // A per-track cost that grows with the number of signals shows up as the
  // sample stage rising while the spectrum stage stays flat; a fixed cost
  // shows the opposite. The numbers above say which.
  static_cast<void>(first_per_track);
  return 0;
}
