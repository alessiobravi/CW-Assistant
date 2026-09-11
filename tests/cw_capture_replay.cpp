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
#include <sstream>
#include "cwassistant/core/cw_vocabulary.hpp"
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
  std::vector<cwassistant::test::TimestampedPublication> publication_points;
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

std::string joinedCallsigns(const std::vector<std::string>& callsigns) {
  std::string result;
  for (const auto& callsign : callsigns) {
    if (!result.empty()) result.push_back(',');
    result.append(callsign);
  }
  return result;
}

bool overlaps(const ObservedTrack& track,
              const cwassistant::test::ReceiverAnnotation& event) {
  return track.last_sample >= event.start_sample &&
         track.first_sample < event.end_sample;
}

double eventFrequencyDelta(
    const ObservedTrack& track,
    const cwassistant::test::ReceiverAnnotation& event,
    const cwassistant::test::SampleEpisode* episode = nullptr) {
  const std::uint64_t start_sample = episode == nullptr
      ? event.start_sample : std::max(event.start_sample,
                                      episode->start_sample);
  const std::uint64_t end_sample = episode == nullptr
      ? event.end_sample : std::min(event.end_sample, episode->end_sample);
  if (start_sample >= end_sample) return std::numeric_limits<double>::infinity();
  double result = std::numeric_limits<double>::infinity();
  double frequency_at_start = 0.0;
  bool has_frequency_at_start = false;
  for (const auto& point : track.points) {
    if (point.sample <= start_sample) {
      frequency_at_start = point.frequency_hz;
      has_frequency_at_start = true;
    }
    if (point.sample < start_sample || point.sample >= end_sample) continue;
    result = std::min(result, std::abs(point.frequency_hz - event.frequency_hz));
  }
  if (has_frequency_at_start) {
    result = std::min(result,
                      std::abs(frequency_at_start - event.frequency_hz));
  }
  return result;
}

bool episodeOverlapsCoverage(
    const cwassistant::test::SampleEpisode& episode,
    const std::vector<cwassistant::test::ReceiverAnnotationCoverage>& coverage) {
  return std::any_of(coverage.cbegin(), coverage.cend(),
                     [&episode](const auto& interval) {
    return cwassistant::test::halfOpenOverlaps(
        episode, {interval.start_sample, interval.end_sample});
  });
}

bool episodeMatchesEvent(
    const ObservedTrack& track,
    const cwassistant::test::SampleEpisode& episode,
    const cwassistant::test::ReceiverAnnotation& event,
    const double maximum_frequency_delta_hz) {
  return cwassistant::test::halfOpenOverlaps(
             episode, {event.start_sample, event.end_sample}) &&
         eventFrequencyDelta(track, event, &episode) <=
             maximum_frequency_delta_hz;
}

void evaluateAnnotations(
    const cwassistant::test::ReceiverAnnotationManifest& manifest,
    const std::unordered_map<std::uint64_t, ObservedTrack>& observations,
    const std::uint64_t total_samples) {
  using namespace cwassistant::test;
  constexpr double kMaximumFrequencyDeltaHz = 120.0;
  ErrorRate total_cer;
  ErrorRate total_wer;
  CallsignCounts total_callsigns;
  CallsignCounts total_transcript_callsigns;
  std::size_t scored_events = 0;
  std::size_t missed_events = 0;
  std::size_t revisions = 0;
  double scored_seconds = 0.0;
  double provisional_latency_sum = 0.0;
  double stable_latency_sum = 0.0;
  std::size_t provisional_latency_count = 0;
  std::size_t stable_latency_count = 0;

  std::vector<ReceiverAnnotationCoverage> reviewed_coverage =
      manifest.coverage;
  if (reviewed_coverage.empty()) {
    reviewed_coverage.push_back({0U, total_samples});
  }

  for (const auto& event : manifest.events) {
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
    if (event.uncertain) continue;
    ++scored_events;
    const double event_seconds = static_cast<double>(
        event.end_sample - event.start_sample) / manifest.sample_rate_hz;
    scored_seconds += event_seconds;
    std::string actual;
    if (selected == nullptr) {
      ++missed_events;
    } else {
      const std::string before = textAt(
          *selected, event.start_sample == 0U ? 0U : event.start_sample - 1U,
          false);
      actual = textDelta(
          before, textAt(*selected, event.end_sample - 1U, false));
      const std::string provisional_before = textAt(
          *selected, event.start_sample == 0U ? 0U : event.start_sample - 1U,
          true);
      std::string previous_provisional = provisional_before;
      bool provisional_seen = false;
      bool stable_seen = false;
      for (const auto& point : selected->points) {
        if (point.sample < event.start_sample || point.sample >= event.end_sample)
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
    std::vector<std::string> transcript_callsigns =
        cwassistant::core::CallsignPolicy::qso_participants_in_text(actual + " ");
    if (const auto call = cwassistant::core::CallsignPolicy::best_complete_in_text(
            actual + " "); call) {
      transcript_callsigns.push_back(*call);
    }
    const auto published_callsigns = selected == nullptr
        ? std::vector<std::string>{}
        : callsignsAtOrBefore(selected->publication_points,
                              event.end_sample - 1U);
    const auto calls = callsignCounts(event.callsigns, published_callsigns);
    total_callsigns.true_positives += calls.true_positives;
    total_callsigns.false_positives += calls.false_positives;
    total_callsigns.false_negatives += calls.false_negatives;
    const auto transcript_calls = callsignCounts(
        event.callsigns, transcript_callsigns);
    total_transcript_callsigns.true_positives +=
        transcript_calls.true_positives;
    total_transcript_callsigns.false_positives +=
        transcript_calls.false_positives;
    total_transcript_callsigns.false_negatives +=
        transcript_calls.false_negatives;
    std::cout << "annotation start_sample=" << event.start_sample
              << " end_sample=" << event.end_sample
              << " frequency_hz=" << event.frequency_hz
              << " matched_track=" << (selected == nullptr ? 0U : selected->id)
              << " expected=\"" << event.normalized_text
              << "\" actual=\"" << normalizedText(actual)
              << "\" cer=" << cer.rate() << " wer=" << wer.rate()
              << " published_callsigns=\""
              << joinedCallsigns(published_callsigns)
              << "\" transcript_callsigns=\""
              << joinedCallsigns(transcript_callsigns) << '"'
              << '\n';
  }

  std::size_t unmatched_publications = 0;
  std::size_t unmatched_callsigns = 0;
  for (const auto& [id, track] : observations) {
    static_cast<void>(id);
    for (const auto& episode : publicationEpisodes(
             track.publication_points, total_samples)) {
      if (!episodeOverlapsCoverage(episode, reviewed_coverage)) continue;
      const bool protected_by_event = std::any_of(
          manifest.events.cbegin(), manifest.events.cend(),
          [&](const auto& event) {
        return episodeMatchesEvent(track, episode, event,
                                   kMaximumFrequencyDeltaHz);
      });
      if (!protected_by_event) ++unmatched_publications;
    }
    for (const auto& call_episode : callsignEpisodes(
             track.publication_points, total_samples)) {
      if (!episodeOverlapsCoverage(call_episode, reviewed_coverage)) continue;
      bool matches_expected_call = false;
      bool matches_uncertain_event = false;
      bool overlaps_certain_event = false;
      for (const auto& event : manifest.events) {
        if (!episodeMatchesEvent(track, call_episode, event,
                                 kMaximumFrequencyDeltaHz)) {
          continue;
        }
        if (event.uncertain) {
          matches_uncertain_event = true;
          continue;
        }
        overlaps_certain_event = true;
        if (std::find(event.callsigns.cbegin(), event.callsigns.cend(),
                      call_episode.callsign) != event.callsigns.cend()) {
          matches_expected_call = true;
        }
      }
      const bool protected_by_event = callsignEpisodeIsProtected(
          matches_expected_call, overlaps_certain_event,
          matches_uncertain_event);
      if (!protected_by_event) ++unmatched_callsigns;
    }
  }
  const double reviewed_minutes =
      static_cast<double>(reviewedCoverageSamples(reviewed_coverage)) /
      manifest.sample_rate_hz / 60.0;
  std::cout << "annotation_summary scored_events=" << scored_events
            << " missed_events=" << missed_events
            << " reviewed_seconds=" << reviewed_minutes * 60.0
            << " cer=" << total_cer.rate()
            << " wer=" << total_wer.rate()
            << " callsign_precision=" << total_callsigns.precision()
            << " callsign_recall=" << total_callsigns.recall()
            << " transcript_callsign_precision="
            << total_transcript_callsigns.precision()
            << " transcript_callsign_recall="
            << total_transcript_callsigns.recall()
            << " mean_provisional_latency_seconds="
            << (provisional_latency_count == 0U ? -1.0 :
                provisional_latency_sum / provisional_latency_count)
            << " mean_stable_latency_seconds="
            << (stable_latency_count == 0U ? -1.0 :
                stable_latency_sum / stable_latency_count)
            << " revisions_per_annotated_minute="
            << (scored_seconds <= 0.0 ? 0.0 :
                static_cast<double>(revisions) * 60.0 / scored_seconds)
            << " false_publication_episodes=" << unmatched_publications
            << " false_callsign_episodes=" << unmatched_callsigns
            << " unmatched_publications_per_minute="
            << (reviewed_minutes <= 0.0 ? 0.0 :
                unmatched_publications / reviewed_minutes)
            << " unmatched_callsigns_per_minute="
            << (reviewed_minutes <= 0.0 ? 0.0 :
                unmatched_callsigns / reviewed_minutes) << '\n';
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
  if (annotations != nullptr) {
    if (!cwassistant::test::receiverAnnotationsFitAudio(
            *annotations, source.total_frames())) {
      std::cerr << path << ": annotation data exceeds audio duration\n";
      return 2;
    }
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
        source.position_frames() == 0U ? 0U : source.position_frames() - 1U;
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
    std::unordered_set<std::uint64_t> current_published_ids;
    for (const auto& channel : published) {
      current_published_ids.insert(channel.id);
      latest_published[channel.id] = channel;
      auto& observed = observations[channel.id];
      observed.published_callsign = channel.callsign;
      observed.published = true;
      std::vector<std::string> published_callsigns = channel.qso_participants;
      if (!channel.callsign.empty())
        published_callsigns.push_back(channel.callsign);
      std::sort(published_callsigns.begin(), published_callsigns.end());
      published_callsigns.erase(
          std::unique(published_callsigns.begin(), published_callsigns.end()),
          published_callsigns.end());
      if (observed.publication_points.empty() ||
          !observed.publication_points.back().published ||
          observed.publication_points.back().callsigns != published_callsigns) {
        observed.publication_points.push_back(
            {current_sample, true, std::move(published_callsigns)});
      }
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
    for (auto& [id, observed] : observations) {
      if (!current_published_ids.contains(id) &&
          !observed.publication_points.empty() &&
          observed.publication_points.back().published) {
        observed.publication_points.push_back(
            {current_sample, false, {}});
      }
    }
  }

  for (const auto& [id, channel] : latest_published) {
    std::cout << "final path=\"" << path << "\" id=" << id
              << " frequency_hz=" << channel.frequency_hz
              << " timing_quality=" << channel.verification_timing_quality
              << " snr_db=" << channel.snr_db
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
    for (const auto& turn : channel.transmissions) {
      std::cout << "turn path=\"" << path << "\" id=" << id
                << " sequence=" << turn.sequence
                << " sender=\"" << turn.sender_callsign << "\""
                << " wpm=" << turn.wpm
                << " cadence_confidence=" << turn.cadence_confidence;
      if (!turn.timing_fingerprint) {
        std::cout << " timing_fingerprint=unavailable\n";
        continue;
      }
      const auto& timing = *turn.timing_fingerprint;
      std::cout << " timing_first_observation_id="
                << timing.first_observation_id
                << " timing_last_observation_id="
                << timing.last_observation_id
                << " timing_started_ns=" << timing.evidence_started_ns
                << " timing_ended_ns=" << timing.evidence_ended_ns
                << " timing_mark_count=" << timing.mark_count
                << " timing_gap_count=" << timing.gap_count
                << " timing_dit_count=" << timing.dit_count
                << " timing_dah_count=" << timing.dah_count
                << " timing_element_gap_count="
                << timing.element_gap_count
                << " timing_character_gap_count="
                << timing.character_gap_count
                << " timing_word_gap_count=" << timing.word_gap_count
                << " timing_dit_median_ms=" << timing.dit_median_ms
                << " timing_dah_median_ms=" << timing.dah_median_ms
                << " timing_element_gap_median_ms="
                << timing.element_gap_median_ms
                << " timing_character_gap_median_ms="
                << timing.character_gap_median_ms
                << " timing_word_gap_median_ms="
                << timing.word_gap_median_ms
                << " timing_keying_weight=" << timing.keying_weight
                << " timing_normalized_mark_residual="
                << timing.normalized_mark_residual
                << " timing_evidence_confidence="
                << timing.evidence_confidence << '\n';
    }
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
    evaluateAnnotations(*annotations, observations, source.total_frames());
  return 0;
}

}  // namespace

namespace {

// The replay tool decodes with the same vocabulary the application loads, so a
// before/after audit measures the shipped dictionaries rather than an empty
// one. Without this the context rescorer would contribute nothing here.
void loadShippedDictionaries() {
  const auto read = [](const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  };
  const std::string directory = CWA_DICTIONARY_DIR;
  auto& vocabulary = cwassistant::core::cwSharedVocabulary();
  vocabulary.clear();
  static_cast<void>(vocabulary.importExchangeWords(
      read(directory + "/cw-abbreviations.txt")));
  static_cast<void>(vocabulary.importWordGapPrefixes(
      read(directory + "/cw-word-gap-prefixes.txt")));
  static_cast<void>(vocabulary.importDistinctiveTokens(
      read(directory + "/cw-distinctive-tokens.txt")));
}

}  // namespace

int main(const int argc, char** argv) {
  loadShippedDictionaries();
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
