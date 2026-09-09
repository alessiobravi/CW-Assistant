#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "support/decoder_evaluation.hpp"

namespace {

using namespace cwassistant::core;

const char* morse(const char symbol) {
  switch (symbol) {
    case 'A': return ".-"; case 'B': return "-..."; case 'C': return "-.-.";
    case 'D': return "-.."; case 'E': return "."; case 'F': return "..-.";
    case 'G': return "--."; case 'H': return "...."; case 'I': return "..";
    case 'J': return ".---"; case 'K': return "-.-"; case 'L': return ".-..";
    case 'M': return "--"; case 'N': return "-."; case 'O': return "---";
    case 'P': return ".--."; case 'Q': return "--.-"; case 'R': return ".-.";
    case 'S': return "..."; case 'T': return "-"; case 'U': return "..-";
    case 'V': return "...-"; case 'W': return ".--"; case 'X': return "-..-";
    case 'Y': return "-.--"; case 'Z': return "--.."; case '0': return "-----";
    case '1': return ".----"; case '2': return "..---"; case '3': return "...--";
    case '4': return "....-"; case '5': return "....."; case '6': return "-....";
    case '7': return "--..."; case '8': return "---.."; case '9': return "----.";
    case '/': return "-..-."; default: return "";
  }
}

float portableNoise(std::mt19937& generator) noexcept {
  constexpr double scale = 1.0 / 4'294'967'296.0;
  double sum = 0.0;
  for (int draw = 0; draw < 12; ++draw)
    sum += (static_cast<double>(generator()) + 0.5) * scale;
  return static_cast<float>(sum - 6.0);
}

struct Profile {
  std::string_view message;
  std::string_view callsign;
  double wpm;
  float snr_db;
  double weighting;
  double jitter;
  double drift_hz_per_second;
  double fading_hz;
  double fading_depth_db;
  unsigned seed;
};

std::vector<float> synthesize(const Profile& profile,
                              const double sample_rate) {
  struct Run { double seconds; bool keyed; };
  std::mt19937 timing_generator(profile.seed ^ 0x9e3779b9U);
  auto jittered = [&](const double duration) {
    constexpr double scale = 1.0 / 4'294'967'296.0;
    const double unit = (static_cast<double>(timing_generator()) + 0.5) * scale;
    return duration * (1.0 + profile.jitter * (2.0 * unit - 1.0));
  };
  const double dot = 1.2 / profile.wpm;
  std::vector<Run> runs;
  for (std::size_t index = 0; index < profile.message.size(); ++index) {
    const char symbol = profile.message[index];
    if (symbol == ' ') {
      if (!runs.empty()) runs.back().seconds += 4.0 * dot;
      continue;
    }
    const char* elements = morse(symbol);
    for (const char* element = elements; *element != '\0'; ++element) {
      const double nominal_units = *element == '-' ? 3.0 : 1.0;
      runs.push_back({jittered((nominal_units - 1.0 + profile.weighting) * dot),
                      true});
      if (*(element + 1) != '\0') {
        runs.push_back({jittered((2.0 - profile.weighting) * dot), false});
      }
    }
    runs.push_back({jittered((4.0 - profile.weighting) * dot), false});
  }
  double total_seconds = 1.0;
  for (const auto& run : runs) total_seconds += run.seconds;
  std::vector<float> audio(static_cast<std::size_t>(total_seconds * sample_rate),
                           0.0F);
  constexpr float noise_amplitude = 0.02F;
  const float mark_amplitude = noise_amplitude * std::sqrt(
      std::pow(10.0F, profile.snr_db / 10.0F) /
      static_cast<float>(sample_rate / 2.0 / 120.0));
  std::mt19937 noise_generator(profile.seed);
  std::size_t position = static_cast<std::size_t>(0.4 * sample_rate);
  double phase = 0.0;
  for (const auto& run : runs) {
    const auto count = static_cast<std::size_t>(run.seconds * sample_rate);
    for (std::size_t step = 0; step < count && position < audio.size();
         ++step, ++position) {
      const double seconds = static_cast<double>(position) / sample_rate;
      const double tone_hz = 650.0 + profile.drift_hz_per_second * seconds;
      phase += 2.0 * std::numbers::pi * tone_hz / sample_rate;
      double envelope = run.keyed ? 1.0 : 0.0;
      if (run.keyed && profile.fading_hz > 0.0) {
        const double loss_db = profile.fading_depth_db * 0.5 *
            (1.0 + std::sin(2.0 * std::numbers::pi * profile.fading_hz *
                            seconds));
        envelope *= std::pow(10.0, -loss_db / 20.0);
      }
      audio[position] = static_cast<float>(
          mark_amplitude * envelope * std::sin(phase));
    }
  }
  for (float& sample : audio)
    sample += noise_amplitude * portableNoise(noise_generator);
  return audio;
}

struct Result {
  std::string text;
  std::string callsign;
  std::size_t revisions{0};
  std::size_t published_updates{0};
  double first_publication_seconds{-1.0};
  std::size_t publication_events{0};
};

Result decode(const std::vector<float>& audio, const double sample_rate) {
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank bank;
  RealtimeSampleBlock block;
  block.stream.sample_rate_hz = sample_rate;
  std::unordered_set<std::uint64_t> ids;
  std::unordered_map<std::uint64_t, std::string> previous_text;
  Result result;
  std::size_t position = 0;
  std::uint64_t timestamp_ns = 0;
  while (position < audio.size()) {
    const std::size_t take = std::min<std::size_t>(1'024U,
                                                   audio.size() - position);
    block.sample_count = take;
    block.timestamp_ns = timestamp_ns;
    for (std::size_t index = 0; index < take; ++index)
      block.samples[index] = {audio[position + index], 0.0F};
    for (const auto& spectrum : analyzer.process(block)) {
      static_cast<void>(bank.updateSpectrum(
          spectrum.timestamp_ns, spectrum.lower_frequency_hz,
          spectrum.upper_frequency_hz, spectrum.instantaneous_bins_dbfs,
          false));
    }
    for (const auto& channel : bank.processSamples(block)) {
      if (ids.insert(channel.id).second) {
        ++result.publication_events;
        if (result.first_publication_seconds < 0.0)
          result.first_publication_seconds =
              static_cast<double>(position) / sample_rate;
      }
      std::string& previous = previous_text[channel.id];
      if (channel.text != previous) {
        ++result.published_updates;
        if (!previous.empty() && !channel.text.starts_with(previous))
          ++result.revisions;
        previous = channel.text;
        if (channel.text.size() >= result.text.size()) result.text = channel.text;
      }
      if (!channel.callsign.empty()) result.callsign = channel.callsign;
    }
    position += take;
    timestamp_ns += static_cast<std::uint64_t>(
        static_cast<long double>(take) * 1'000'000'000.0L / sample_rate);
  }
  result.text = cwassistant::test::normalizedText(result.text);
  return result;
}

}  // namespace

int main() {
  constexpr double sample_rate = 48'000.0;
  constexpr std::array profiles{
      Profile{"QRZ DE K2MNO K2MNO", "K2MNO", 14.0, 18.0F, 1.12, 0.10,
              0.8, 0.23, 4.0, 4'001U},
      Profile{"PSE K G4XYZ G4XYZ", "G4XYZ", 28.0, 14.0F, 0.88, 0.08,
              -1.2, 0.41, 3.0, 8'117U},
      Profile{"CQ CQ DE JA1ABC JA1ABC K", "JA1ABC", 34.0, 25.0F, 1.03,
              0.03, 0.3, 0.31, 1.5, 15'013U},
  };
  cwassistant::test::ErrorRate heldout_cer;
  cwassistant::test::ErrorRate heldout_wer;
  cwassistant::test::CallsignCounts heldout_calls;
  std::size_t heldout_wrong_calls = 0;
  double maximum_revision_rate = 0.0;
  double maximum_update_rate = 0.0;
  double maximum_publication_latency = 0.0;
  for (const auto& profile : profiles) {
    const auto audio = synthesize(profile, sample_rate);
    const auto decoded = decode(audio, sample_rate);
    const auto case_cer = cwassistant::test::characterErrorRate(
        profile.message, decoded.text);
    const auto case_wer = cwassistant::test::wordErrorRate(
        profile.message, decoded.text);
    heldout_cer.edits += case_cer.edits;
    heldout_cer.references += case_cer.references;
    heldout_wer.edits += case_wer.edits;
    heldout_wer.references += case_wer.references;
    const auto calls = cwassistant::test::callsignCounts(
        {std::string(profile.callsign)},
        decoded.callsign.empty() ? std::vector<std::string>{}
                                 : std::vector<std::string>{decoded.callsign});
    heldout_calls.true_positives += calls.true_positives;
    heldout_calls.false_positives += calls.false_positives;
    heldout_calls.false_negatives += calls.false_negatives;
    if (!decoded.callsign.empty() && decoded.callsign != profile.callsign)
      ++heldout_wrong_calls;
    const double duration_seconds =
        static_cast<double>(audio.size()) / sample_rate;
    maximum_revision_rate = std::max(
        maximum_revision_rate,
        static_cast<double>(decoded.revisions) * 60.0 / duration_seconds);
    maximum_update_rate = std::max(
        maximum_update_rate,
        static_cast<double>(decoded.published_updates) * 60.0 /
            duration_seconds);
    maximum_publication_latency = std::max(
        maximum_publication_latency, decoded.first_publication_seconds);
    std::cout << "message=\"" << profile.message << "\" decoded=\""
              << decoded.text
              << "\" cer=" << case_cer.rate()
              << " wer=" << case_wer.rate()
              << " callsign=\"" << decoded.callsign << "\" revisions="
              << decoded.revisions << " published_updates="
              << decoded.published_updates << " publication_latency_seconds="
              << decoded.first_publication_seconds << '\n';
  }

  std::vector<float> hard_negative(
      static_cast<std::size_t>(30.0 * sample_rate));
  std::mt19937 negative_generator(91'771U);
  for (float& sample : hard_negative)
    sample = 0.02F * portableNoise(negative_generator);
  const auto negative = decode(hard_negative, sample_rate);
  const double false_publications_per_minute =
      static_cast<double>(negative.publication_events) * 2.0;
  constexpr double maximum_revision_rate_per_minute = 180.0;
  constexpr double maximum_publication_latency_seconds = 8.0;
  std::cout << "summary heldout_cer=" << heldout_cer.rate()
            << " heldout_wer=" << heldout_wer.rate()
            << " callsign_precision=" << heldout_calls.precision()
            << " callsign_recall=" << heldout_calls.recall()
            << " wrong_callsigns=" << heldout_wrong_calls
            << " maximum_revisions_per_minute=" << maximum_revision_rate
            << " maximum_published_updates_per_minute=" << maximum_update_rate
            << " maximum_publication_latency_seconds="
            << maximum_publication_latency
            << " false_publications_per_minute="
            << false_publications_per_minute << '\n';

  return heldout_cer.rate() <= 0.55 && heldout_wer.rate() <= 0.95 &&
      heldout_wrong_calls == 0U && heldout_calls.precision() == 1.0 &&
      heldout_calls.recall() >= 0.33 &&
      maximum_revision_rate <= maximum_revision_rate_per_minute &&
      maximum_publication_latency >= 0.0 &&
      maximum_publication_latency <= maximum_publication_latency_seconds &&
      false_publications_per_minute == 0.0 && negative.callsign.empty()
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
