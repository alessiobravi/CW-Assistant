#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/wav_replay_source.hpp"

namespace {

int replay(const std::string& path) {
  using namespace std::chrono_literals;
  using namespace cwassistant::core;

  WavReplaySource source;
  if (!source.open(path, {.kind = StreamKind::Audio})) {
    std::cerr << path << ": " << source.last_error() << '\n';
    return 1;
  }
  source.start();
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank channels;
  std::unordered_set<std::uint64_t> published_ids;
  std::unordered_map<std::uint64_t, CwChannelSnapshot> latest_published;
  std::size_t maximum_tracks = 0;
  std::size_t maximum_published = 0;

  RealtimeSampleBlock block;
  while (source.read(block, 0ms)) {
    for (const auto& spectrum : analyzer.process(block)) {
      static_cast<void>(channels.updateSpectrum(
          spectrum.timestamp_ns, spectrum.lower_frequency_hz,
          spectrum.upper_frequency_hz, spectrum.bins_dbfs));
    }
    const auto& published = channels.processSamples(block);
    const auto diagnostics = channels.allTrackDiagnostics();
    maximum_tracks = std::max(maximum_tracks, diagnostics.size());
    maximum_published = std::max(maximum_published, published.size());
    const double elapsed_seconds =
        static_cast<double>(source.position_frames()) /
        source.stream_descriptor().sample_rate_hz;
    for (const auto& channel : published) {
      latest_published[channel.id] = channel;
      if (!published_ids.insert(channel.id).second) continue;
      std::cout << "published path=\"" << path << "\" time_s="
                << elapsed_seconds << " id=" << channel.id
                << " color=" << static_cast<unsigned>(channel.color_index)
                << " frequency_hz=" << channel.frequency_hz
                << " presentation_frequency_hz="
                << channel.presentation_frequency_hz
                << " wpm=" << channel.wpm
                << " acoustic_wpm=" << channel.acoustic_wpm
                << " cadence_fit="
                << channel.acoustic_cadence_confidence
                << " confidence=" << channel.verification_confidence
                << " text=\"" << channel.text << "\""
                << " refined_text=\"" << channel.refined_text << "\"\n";
    }
  }

  for (const auto& [id, channel] : latest_published) {
    std::cout << "final path=\"" << path << "\" id=" << id
              << " frequency_hz=" << channel.frequency_hz
              << " text=\"" << channel.text << "\""
              << " refined_text=\"" << channel.refined_text << "\""
              << " alternatives=" << channel.acoustic_alternatives.size();
    if (!channel.acoustic_alternatives.empty()) {
      const auto& best = channel.acoustic_alternatives.front();
      std::cout << " best_alternative=\"" << best.text << "\""
                << " best_cost=" << best.acoustic_cost
                << " best_confidence=" << best.evidence_confidence;
    }
    std::cout << '\n';
  }

  const auto verification = channels.verificationDiagnostics();
  std::cout << "summary path=\"" << path << "\" duration_s="
            << source.duration_seconds() << " maximum_tracks="
            << maximum_tracks << " maximum_published=" << maximum_published
            << " verified_transitions=" << verification.verified_transitions
            << " decoder_reacquisitions="
            << verification.decoder_reacquisitions
            << " expired_unverified="
            << verification.expired_unverified_tracks << '\n';
  return 0;
}

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: cwa_capture_replay <audio.wav> [audio.wav ...]\n";
    return 2;
  }
  int status = 0;
  for (int index = 1; index < argc; ++index) {
    status = std::max(status, replay(argv[index]));
  }
  return status;
}
