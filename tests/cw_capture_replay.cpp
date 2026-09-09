#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/wav_replay_source.hpp"
#include "support/decoder_evaluation.hpp"
#include "support/file_sha256.hpp"
#include "support/receiver_annotations.hpp"

namespace {

struct TextPoint {
  std::uint64_t sample{0};
  std::string text;
  std::string provisional_text;
  double frequency_hz{0.0};
};

struct ObservedTrack {
  std::uint64_t id{0};
  double frequency_hz{0.0};
  std::uint64_t first_sample{0};
  std::uint64_t last_sample{0};
  std::vector<TextPoint> points;
  std::string published_callsign;
  bool published{false};
};

std::string textAt(const ObservedTrack& track, const std::uint64_t sample,
                   const bool provisional) {
  std::string result;
  for (const auto& point : track.points) {
    if (point.sample > sample) break;
    result = provisional ? point.provisional_text : point.text;
  }
  return result;
}

std::string textDelta(const std::string& before, const std::string& after) {
  if (!before.empty() && after.starts_with(before))
    return after.substr(before.size());
  return after;
}

bool overlaps(const ObservedTrack& track,
              const cwassistant::test::ReceiverAnnotation& event) {
  return track.last_sample >= event.start_sample &&
         track.first_sample <= event.end_sample;
}

double eventFrequencyDelta(
    const ObservedTrack& track,
    const cwassistant::test::ReceiverAnnotation& event) {
  double result = std::numeric_limits<double>::infinity();
  for (const auto& point : track.points) {
    if (point.sample < event.start_sample || point.sample > event.end_sample)
      continue;
    result = std::min(result, std::abs(point.frequency_hz - event.frequency_hz));
  }
  return result;
}

void evaluateAnnotations(
    const cwassistant::test::ReceiverAnnotationManifest& manifest,
    const std::unordered_map<std::uint64_t, ObservedTrack>& observations,
    const double duration_seconds) {
  using namespace cwassistant::test;
  constexpr double kMaximumFrequencyDeltaHz = 120.0;
  ErrorRate total_cer;
  ErrorRate total_wer;
  CallsignCounts total_callsigns;
  std::unordered_set<std::uint64_t> matched_tracks;
  std::size_t scored_events = 0;
  std::size_t missed_events = 0;
  std::size_t revisions = 0;
  double scored_seconds = 0.0;
  double provisional_latency_sum = 0.0;
  double stable_latency_sum = 0.0;
  std::size_t provisional_latency_count = 0;
  std::size_t stable_latency_count = 0;

  for (const auto& event : manifest.events) {
    if (event.uncertain) continue;
    ++scored_events;
    const ObservedTrack* selected = nullptr;
    double best_delta = std::numeric_limits<double>::infinity();
    for (const auto& [id, track] : observations) {
      static_cast<void>(id);
      if (!overlaps(track, event)) continue;
      const double delta = eventFrequencyDelta(track, event);
      if (delta <= kMaximumFrequencyDeltaHz && delta < best_delta) {
        selected = &track;
        best_delta = delta;
      }
    }
    const double event_seconds = static_cast<double>(
        event.end_sample - event.start_sample) / manifest.sample_rate_hz;
    scored_seconds += event_seconds;
    std::string actual;
    if (selected == nullptr) {
      ++missed_events;
    } else {
      matched_tracks.insert(selected->id);
      const std::string before = textAt(
          *selected, event.start_sample == 0U ? 0U : event.start_sample - 1U,
          false);
      actual = textDelta(before, textAt(*selected, event.end_sample, false));
      const std::string provisional_before = textAt(
          *selected, event.start_sample == 0U ? 0U : event.start_sample - 1U,
          true);
      std::string previous_provisional = provisional_before;
      bool provisional_seen = false;
      bool stable_seen = false;
      for (const auto& point : selected->points) {
        if (point.sample < event.start_sample || point.sample > event.end_sample)
          continue;
        const std::string stable = textDelta(before, point.text);
        const std::string provisional = textDelta(
            provisional_before, point.provisional_text);
        if (point.provisional_text != previous_provisional &&
            !point.provisional_text.starts_with(previous_provisional)) {
          ++revisions;
        }
        previous_provisional = point.provisional_text;
        if (!provisional_seen &&
            !normalizedText(provisional).empty()) {
          provisional_latency_sum += static_cast<double>(
              point.sample - event.start_sample) / manifest.sample_rate_hz;
          ++provisional_latency_count;
          provisional_seen = true;
        }
        if (!stable_seen && !normalizedText(stable).empty()) {
          stable_latency_sum += static_cast<double>(
              point.sample - event.start_sample) / manifest.sample_rate_hz;
          ++stable_latency_count;
          stable_seen = true;
        }
      }
    }

    const auto cer = characterErrorRate(event.normalized_text, actual);
    const auto wer = wordErrorRate(event.normalized_text, actual);
    total_cer.edits += cer.edits;
    total_cer.references += cer.references;
    total_wer.edits += wer.edits;
    total_wer.references += wer.references;
    std::vector<std::string> actual_callsigns =
        cwassistant::core::CallsignPolicy::qso_participants_in_text(actual + " ");
    if (const auto call = cwassistant::core::CallsignPolicy::best_complete_in_text(
            actual + " "); call) {
      actual_callsigns.push_back(*call);
    }
    const auto calls = callsignCounts(event.callsigns,
                                      std::move(actual_callsigns));
    total_callsigns.true_positives += calls.true_positives;
    total_callsigns.false_positives += calls.false_positives;
    total_callsigns.false_negatives += calls.false_negatives;
    std::cout << "annotation start_sample=" << event.start_sample
              << " end_sample=" << event.end_sample
              << " frequency_hz=" << event.frequency_hz
              << " matched_track=" << (selected == nullptr ? 0U : selected->id)
              << " expected=\"" << event.normalized_text
              << "\" actual=\"" << normalizedText(actual)
              << "\" cer=" << cer.rate() << " wer=" << wer.rate() << '\n';
  }

  std::size_t unmatched_publications = 0;
  std::size_t unmatched_callsigns = 0;
  for (const auto& [id, track] : observations) {
    if (!track.published || matched_tracks.contains(id)) continue;
    ++unmatched_publications;
    if (!track.published_callsign.empty()) ++unmatched_callsigns;
  }
  const double duration_minutes = duration_seconds / 60.0;
  std::cout << "annotation_summary scored_events=" << scored_events
            << " missed_events=" << missed_events
            << " cer=" << total_cer.rate()
            << " wer=" << total_wer.rate()
            << " callsign_precision=" << total_callsigns.precision()
            << " callsign_recall=" << total_callsigns.recall()
            << " mean_provisional_latency_seconds="
            << (provisional_latency_count == 0U ? -1.0 :
                provisional_latency_sum / provisional_latency_count)
            << " mean_stable_latency_seconds="
            << (stable_latency_count == 0U ? -1.0 :
                stable_latency_sum / stable_latency_count)
            << " revisions_per_annotated_minute="
            << (scored_seconds <= 0.0 ? 0.0 :
                static_cast<double>(revisions) * 60.0 / scored_seconds)
            << " unmatched_publications_per_minute="
            << (duration_minutes <= 0.0 ? 0.0 :
                unmatched_publications / duration_minutes)
            << " unmatched_callsigns_per_minute="
            << (duration_minutes <= 0.0 ? 0.0 :
                unmatched_callsigns / duration_minutes) << '\n';
}

int replay(
    const std::string& path,
    const cwassistant::test::ReceiverAnnotationManifest* annotations) {
  using namespace std::chrono_literals;
  using namespace cwassistant::core;

  WavReplaySource source;
  if (!source.open(path, {.kind = StreamKind::Audio})) {
    std::cerr << path << ": " << source.last_error() << '\n';
    return 1;
  }
  if (annotations != nullptr && annotations->sample_rate_hz !=
                                    static_cast<std::uint32_t>(
                                        source.stream_descriptor()
                                            .sample_rate_hz)) {
    std::cerr << path << ": annotation sample rate does not match audio\n";
    return 2;
  }
  if (annotations != nullptr && annotations->events.back().end_sample >
                                    source.total_frames()) {
    std::cerr << path << ": annotation event exceeds audio duration\n";
    return 2;
  }
  if (annotations != nullptr &&
      cwassistant::test::fileSha256(path) != annotations->audio_sha256) {
    std::cerr << path << ": annotation audio SHA-256 does not match\n";
    return 2;
  }
  source.start();
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank channels;
  std::unordered_set<std::uint64_t> published_ids;
  std::unordered_map<std::uint64_t, CwChannelSnapshot> latest_published;
  std::unordered_map<std::uint64_t, ObservedTrack> observations;
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
    const std::uint64_t current_sample =
        source.position_frames() - block.sample_count;
    for (const auto& diagnostic : diagnostics) {
      auto [entry, inserted] = observations.try_emplace(diagnostic.id);
      auto& observed = entry->second;
      if (inserted) {
        observed.id = diagnostic.id;
        observed.first_sample = current_sample;
      }
      observed.last_sample = current_sample;
      observed.frequency_hz = diagnostic.presentation_frequency_hz;
      const bool changed = observed.points.empty() ||
          observed.points.back().text != diagnostic.text ||
          observed.points.back().provisional_text != diagnostic.provisional_text ||
          std::abs(observed.points.back().frequency_hz -
                   diagnostic.presentation_frequency_hz) > 0.01;
      if (changed) {
        observed.points.push_back({current_sample, diagnostic.text,
                                   diagnostic.provisional_text,
                                   diagnostic.presentation_frequency_hz});
      }
    }
    maximum_tracks = std::max(maximum_tracks, diagnostics.size());
    maximum_published = std::max(maximum_published, published.size());
    const double elapsed_seconds =
        static_cast<double>(source.position_frames()) /
        source.stream_descriptor().sample_rate_hz;
    for (const auto& channel : published) {
      latest_published[channel.id] = channel;
      observations[channel.id].published_callsign = channel.callsign;
      observations[channel.id].published = true;
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
              << " timing_quality=" << channel.verification_timing_quality
              << " callsign=\"" << channel.callsign << "\""
              << " turns=" << channel.transmissions.size()
              << " sender=\"" << channel.current_sender_callsign << "\""
              << " sender_wpm=" << channel.current_sender_wpm
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
  if (annotations != nullptr)
    evaluateAnnotations(*annotations, observations, source.duration_seconds());
  return 0;
}

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: cwa_capture_replay [--annotations sidecar.tsv] "
                 "<audio.wav> [audio.wav ...]\n";
    return 2;
  }
  int first_audio = 1;
  cwassistant::test::ReceiverAnnotationManifest annotations;
  const cwassistant::test::ReceiverAnnotationManifest* annotation_pointer =
      nullptr;
  if (argc >= 4 && std::string_view(argv[1]) == "--annotations") {
    if (argc != 4) {
      std::cerr << "annotation mode binds one sidecar to exactly one WAV\n";
      return 2;
    }
    std::ifstream input(argv[2]);
    std::string error;
    if (!input.is_open() || !cwassistant::test::parseReceiverAnnotations(
                                input, annotations, error)) {
      std::cerr << "annotation sidecar: "
                << (error.empty() ? "cannot open file" : error) << '\n';
      return 2;
    }
    annotation_pointer = &annotations;
    first_audio = 3;
  }
  int status = 0;
  for (int index = first_audio; index < argc; ++index) {
    status = std::max(status, replay(argv[index], annotation_pointer));
  }
  return status;
}
