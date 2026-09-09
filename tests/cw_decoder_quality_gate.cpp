#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/cw_decoder.hpp"
#include "support/decoder_evaluation.hpp"

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
    {'/', "-..-."},
};

std::string_view elementsFor(const char symbol) {
  const auto found = std::find_if(
      std::begin(kMorse), std::end(kMorse),
      [symbol](const MorseEntry& entry) { return entry.symbol == symbol; });
  return found == std::end(kMorse) ? std::string_view{} : found->elements;
}

struct DecodeResult {
  std::string text;
  std::string published_callsign;
  double provisional_latency_seconds{-1.0};
  double stable_latency_seconds{-1.0};
};

DecodeResult decodeMessage(const std::string_view message, const double wpm,
                           const float snr_db,
                           const double jitter_fraction) {
  cwassistant::core::CwMultiSpeedDecoder decoder;
  std::uint64_t now_ns = 0;
  std::uint64_t signal_started_ns = 0;
  std::uint64_t provisional_ns = 0;
  std::uint64_t stable_ns = 0;
  std::uint32_t jitter_state = 0x51f15eU;
  cwassistant::core::CwDecoderUpdate latest;
  const auto advance = [&](const double requested_ms, const float evidence) {
    if (evidence > 0.0F && signal_started_ns == 0U)
      signal_started_ns = now_ns;
    double remaining_ms = requested_ms;
    while (remaining_ms > 0.0) {
      const double step_ms = std::min(5.0, remaining_ms);
      now_ns += static_cast<std::uint64_t>(std::llround(
          step_ms * 1'000'000.0));
      latest = decoder.process(now_ns, evidence);
      if (signal_started_ns != 0U && provisional_ns == 0U &&
          !cwassistant::test::normalizedText(latest.provisional_text).empty()) {
        provisional_ns = now_ns;
      }
      if (signal_started_ns != 0U && stable_ns == 0U &&
          !cwassistant::test::normalizedText(latest.text).empty()) {
        stable_ns = now_ns;
      }
      remaining_ms -= step_ms;
    }
  };
  const auto jittered = [&](const double nominal_ms) {
    jitter_state = jitter_state * 1'664'525U + 1'013'904'223U;
    const double unit = static_cast<double>((jitter_state >> 8U) & 0xffffU) /
                        65'535.0;
    return nominal_ms * (1.0 + jitter_fraction * (2.0 * unit - 1.0));
  };

  const double dot_ms = 1'200.0 / wpm;
  advance(4.0 * dot_ms, 0.0F);
  for (std::size_t index = 0; index < message.size(); ++index) {
    if (message[index] == ' ') continue;
    const auto elements = elementsFor(message[index]);
    for (std::size_t element = 0; element < elements.size(); ++element) {
      advance(jittered((elements[element] == '-' ? 3.0 : 1.0) * dot_ms),
              snr_db);
      if (element + 1U < elements.size())
        advance(jittered(dot_ms), 0.0F);
    }
    const bool word_ends = index + 1U < message.size() &&
                           message[index + 1U] == ' ';
    advance(jittered((word_ends ? 7.0 : 3.0) * dot_ms), 0.0F);
  }
  latest = decoder.flush(now_ns + static_cast<std::uint64_t>(
      10.0 * dot_ms * 1'000'000.0));

  const auto secondsAfterStart = [signal_started_ns](const std::uint64_t time) {
    return time == 0U ? -1.0 :
        static_cast<double>(time - signal_started_ns) / 1'000'000'000.0;
  };
  const auto published_callsign =
      cwassistant::core::CallsignPolicy::best_complete_in_text(latest.text);
  return {cwassistant::test::normalizedText(latest.text),
          published_callsign.value_or(std::string{}),
          secondsAfterStart(provisional_ns), secondsAfterStart(stable_ns)};
}

std::vector<bool> repeatedSos(const std::size_t steps_per_unit) {
  std::vector<bool> units;
  const auto append = [&units](const bool keyed, const std::size_t count) {
    units.insert(units.end(), count, keyed);
  };
  const auto letter = [&append](const std::string_view elements) {
    for (std::size_t index = 0; index < elements.size(); ++index) {
      append(true, elements[index] == '.' ? 1U : 3U);
      append(false, index + 1U == elements.size() ? 3U : 1U);
    }
  };
  letter("...");
  letter("---");
  letter("...");
  append(false, 4U);
  std::vector<bool> result;
  for (const bool keyed : units)
    result.insert(result.end(), steps_per_unit, keyed);
  return result;
}

struct PublicationResult {
  std::size_t maximum_published{0};
  std::size_t publication_events{0};
  std::size_t callsign_publication_events{0};
  double first_publication_seconds{-1.0};
};

PublicationResult runPublicationFixture(const bool cw,
                                        const double duration_seconds) {
  constexpr double sample_rate_hz = 8'000.0;
  constexpr std::size_t samples_per_step = 80U;
  constexpr double tone_hz = 400.0;
  const auto keying = repeatedSos(10U);
  cwassistant::core::CwChannelBank bank;
  std::vector<float> bins(1'001U, -105.0F);
  std::uint32_t noise_state = 0x1977U;
  double phase = 0.0;
  PublicationResult result;
  std::unordered_set<std::uint64_t> published_ids;
  std::unordered_set<std::uint64_t> callsign_ids;
  const auto steps = static_cast<std::size_t>(duration_seconds * 100.0);
  for (std::size_t step = 0; step < steps; ++step) {
    const bool keyed = cw && keying[step % keying.size()];
    bins.assign(bins.size(), -105.0F);
    if (keyed) bins[static_cast<std::size_t>(tone_hz)] = -68.0F;
    const auto timestamp_ns = static_cast<std::uint64_t>(step) * 10'000'000ULL;
    static_cast<void>(bank.updateSpectrum(timestamp_ns, 0.0, 1'000.0, bins));

    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate_hz;
    block.timestamp_ns = timestamp_ns;
    block.sequence = static_cast<std::uint64_t>(step);
    block.sample_count = samples_per_step;
    for (std::size_t sample = 0; sample < block.sample_count; ++sample) {
      noise_state = noise_state * 1'664'525U + 1'013'904'223U;
      const float noise = static_cast<float>((noise_state >> 8U) & 0xffffU) /
                              32'767.5F - 1.0F;
      block.samples[sample] = {
          (keyed ? 0.20F : 0.0F) * static_cast<float>(std::sin(phase)) +
              0.001F * noise,
          0.0F};
      phase += 2.0 * std::numbers::pi * tone_hz / sample_rate_hz;
    }
    const auto& channels = bank.processSamples(block);
    result.maximum_published = std::max(result.maximum_published,
                                         channels.size());
    for (const auto& channel : channels) {
      if (published_ids.insert(channel.id).second) ++result.publication_events;
      if (!channel.callsign.empty() && callsign_ids.insert(channel.id).second)
        ++result.callsign_publication_events;
    }
    if (result.first_publication_seconds < 0.0 && !channels.empty())
      result.first_publication_seconds = static_cast<double>(step) / 100.0;
  }
  return result;
}

}  // namespace

int main() {
  struct Case {
    std::string_view message;
    double wpm;
    float snr_db;
    double jitter;
    std::string_view callsign;
  };
  constexpr std::array cases{
      Case{"CQ DE W1AW W1AW", 20.0, 12.0F, 0.05, "W1AW"},
      Case{"CQ DE EA1EYL EA1EYL", 12.0, 14.0F, 0.04, "EA1EYL"},
      Case{"AD2FC/P AD2FC/P", 25.0, 10.0F, 0.08, "AD2FC/P"},
  };

  cwassistant::test::ErrorRate total_cer;
  cwassistant::test::ErrorRate total_wer;
  cwassistant::test::CallsignCounts total_callsigns;
  double maximum_provisional_latency = 0.0;
  double maximum_stable_latency = 0.0;
  bool all_latencies_observed = true;
  for (const auto& fixture : cases) {
    const auto decoded = decodeMessage(fixture.message, fixture.wpm,
                                       fixture.snr_db, fixture.jitter);
    const auto cer = cwassistant::test::characterErrorRate(
        fixture.message, decoded.text);
    const auto wer = cwassistant::test::wordErrorRate(
        fixture.message, decoded.text);
    total_cer.edits += cer.edits;
    total_cer.references += cer.references;
    total_wer.edits += wer.edits;
    total_wer.references += wer.references;

    std::vector<std::string> published;
    if (!decoded.published_callsign.empty())
      published.push_back(decoded.published_callsign);
    const auto callsigns = cwassistant::test::callsignCounts(
        {std::string(fixture.callsign)}, std::move(published));
    total_callsigns.true_positives += callsigns.true_positives;
    total_callsigns.false_positives += callsigns.false_positives;
    total_callsigns.false_negatives += callsigns.false_negatives;
    all_latencies_observed = all_latencies_observed &&
        decoded.provisional_latency_seconds >= 0.0 &&
        decoded.stable_latency_seconds >= 0.0;
    maximum_provisional_latency = std::max(
        maximum_provisional_latency, decoded.provisional_latency_seconds);
    maximum_stable_latency = std::max(
        maximum_stable_latency, decoded.stable_latency_seconds);
    std::cout << "message=\"" << fixture.message << "\" decoded=\""
              << decoded.text << "\" cer=" << cer.rate()
              << " wer=" << wer.rate()
              << " published_callsign=\"" << decoded.published_callsign
              << "\""
              << " provisional_latency_seconds="
              << decoded.provisional_latency_seconds
              << " stable_latency_seconds=" << decoded.stable_latency_seconds
              << '\n';
  }

  const auto positive = runPublicationFixture(true, 16.0);
  const auto negative = runPublicationFixture(false, 60.0);
  constexpr double maximum_provisional_latency_seconds = 2.0;
  constexpr double maximum_stable_latency_seconds = 3.25;
  constexpr double maximum_publication_latency_seconds = 6.0;
  const double false_publications_per_minute =
      static_cast<double>(negative.publication_events);

  std::cout << "summary cer=" << total_cer.rate()
            << " wer=" << total_wer.rate()
            << " callsign_precision=" << total_callsigns.precision()
            << " callsign_recall=" << total_callsigns.recall()
            << " maximum_provisional_latency_seconds="
            << maximum_provisional_latency
            << " maximum_stable_latency_seconds=" << maximum_stable_latency
            << " publication_latency_seconds="
            << positive.first_publication_seconds
            << " false_publications_per_minute="
            << false_publications_per_minute
            << " false_callsigns_per_minute="
            << negative.callsign_publication_events
            << " gates_cer=0 gates_wer=0 gate_callsign_precision=1"
            << " gate_callsign_recall=1"
            << " gate_provisional_latency_seconds="
            << maximum_provisional_latency_seconds
            << " gate_stable_latency_seconds="
            << maximum_stable_latency_seconds
            << " gate_publication_latency_seconds="
            << maximum_publication_latency_seconds
            << " gate_false_publications_per_minute=0"
            << " gate_false_callsigns_per_minute=0\n";

  return total_cer.edits == 0U && total_wer.edits == 0U &&
      total_callsigns.precision() == 1.0 &&
      total_callsigns.recall() == 1.0 && all_latencies_observed &&
      maximum_provisional_latency <= maximum_provisional_latency_seconds &&
      maximum_stable_latency <= maximum_stable_latency_seconds &&
      positive.maximum_published == 1U &&
      positive.first_publication_seconds >= 0.0 &&
      positive.first_publication_seconds <= maximum_publication_latency_seconds &&
      false_publications_per_minute == 0.0 &&
      negative.callsign_publication_events == 0U
      ? EXIT_SUCCESS : EXIT_FAILURE;
}
