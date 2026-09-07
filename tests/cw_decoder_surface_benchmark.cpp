// Character error over a speed and noise surface, reported with the
// measurement's own repeatability.
//
// The figure this produces moves by a few hundredths with the noise draw alone
// while being deterministic per seed. A single absolute number therefore says
// very little: differences smaller than the spread printed at the end are not
// results. Two builds must be compared on the same seed sets, row by row, and
// judged on the paired difference -- that comparison cancels the draw almost
// entirely and resolves changes an unpaired comparison cannot see.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"

namespace {

using namespace cwassistant::core;

const char* morse(const char symbol) {
  switch (symbol) {
    case 'A': return ".-";    case 'B': return "-...";  case 'C': return "-.-.";
    case 'D': return "-..";   case 'E': return ".";     case 'F': return "..-.";
    case 'G': return "--.";   case 'H': return "....";  case 'I': return "..";
    case 'J': return ".---";  case 'K': return "-.-";   case 'L': return ".-..";
    case 'M': return "--";    case 'N': return "-.";    case 'O': return "---";
    case 'P': return ".--.";  case 'Q': return "--.-";  case 'R': return ".-.";
    case 'S': return "...";   case 'T': return "-";     case 'U': return "..-";
    case 'V': return "...-";  case 'W': return ".--";   case 'X': return "-..-";
    case 'Y': return "-.--";  case 'Z': return "--..";  case '0': return "-----";
    case '1': return ".----"; case '2': return "..---"; case '3': return "...--";
    case '4': return "....-"; case '5': return "....."; case '6': return "-....";
    case '7': return "--..."; case '8': return "---.."; case '9': return "----.";
    default: return "";
  }
}

std::size_t editDistance(const std::string& left, const std::string& right) {
  std::vector<std::size_t> previous(right.size() + 1);
  std::vector<std::size_t> current(right.size() + 1);
  for (std::size_t column = 0; column <= right.size(); ++column)
    previous[column] = column;
  for (std::size_t row = 1; row <= left.size(); ++row) {
    current[0] = row;
    for (std::size_t column = 1; column <= right.size(); ++column) {
      current[column] = std::min({previous[column] + 1, current[column - 1] + 1,
                                  previous[column - 1] +
                                      (left[row - 1] == right[column - 1] ? 0U
                                                                          : 1U)});
    }
    previous.swap(current);
  }
  return previous.back();
}

std::string squeeze(std::string value) {
  std::string result;
  for (const char symbol : value)
    if (symbol != ' ' || (!result.empty() && result.back() != ' '))
      result += symbol;
  while (!result.empty() && result.back() == ' ') result.pop_back();
  const auto begin = result.find_first_not_of(' ');
  return begin == std::string::npos ? std::string{} : result.substr(begin);
}

std::vector<float> synthesize(const std::string& message, const double wpm,
                              const float snr_db, const double sample_rate,
                              const double tone_hz, const unsigned seed) {
  const double dot = 1.2 / wpm;
  const double rise = 0.005;
  std::vector<std::pair<double, bool>> runs;
  for (const char symbol : message) {
    if (symbol == ' ') {
      if (!runs.empty()) runs.back().first += 4.0 * dot;
      continue;
    }
    const char* elements = morse(symbol);
    for (const char* element = elements; *element != '\0'; ++element) {
      runs.push_back({*element == '-' ? 3.0 * dot : dot, true});
      if (*(element + 1) != '\0') runs.push_back({dot, false});
    }
    runs.push_back({3.0 * dot, false});
  }
  double total = 0.0;
  for (const auto& run : runs) total += run.first;
  const auto count = static_cast<std::size_t>((total + 1.0) * sample_rate);
  std::vector<float> audio(count, 0.0F);
  const float noise = 0.02F;
  const float amplitude = noise * std::sqrt(
      std::pow(10.0F, snr_db / 10.0F) /
      static_cast<float>(sample_rate / 2.0 / 120.0));
  std::mt19937 generator(seed);
  std::normal_distribution<float> gaussian(0.0F, 1.0F);
  double phase = 0.0;
  std::size_t index = static_cast<std::size_t>(0.4 * sample_rate);
  for (const auto& run : runs) {
    const auto length = static_cast<std::size_t>(run.first * sample_rate);
    for (std::size_t step = 0; step < length && index < count; ++step, ++index) {
      double envelope = 0.0;
      if (run.second) {
        const double into = step / sample_rate;
        const double from_end = (length / sample_rate) - into;
        const double attack = into < rise
            ? 0.5 - 0.5 * std::cos(std::numbers::pi * into / rise) : 1.0;
        const double decay = from_end < rise
            ? 0.5 - 0.5 * std::cos(std::numbers::pi * from_end / rise) : 1.0;
        envelope = std::min(attack, decay);
      }
      phase += 2.0 * std::numbers::pi * tone_hz / sample_rate;
      audio[index] += static_cast<float>(amplitude * envelope * std::sin(phase));
    }
  }
  for (std::size_t step = 0; step < count; ++step)
    audio[step] += noise * gaussian(generator);
  return audio;
}

struct Decoded {
  std::string text;
  std::string callsign;
};

Decoded decodeChannel(const std::vector<float>& audio,
                      const double sample_rate) {
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank bank;
  RealtimeSampleBlock block;
  block.stream.sample_rate_hz = sample_rate;
  Decoded best;
  std::size_t position = 0;
  std::uint64_t now = 0;
  while (position < audio.size()) {
    const std::size_t take = std::min<std::size_t>(1'024,
                                                   audio.size() - position);
    block.sample_count = take;
    block.timestamp_ns = now;
    for (std::size_t index = 0; index < take; ++index)
      block.samples[index] = {audio[position + index], 0.0F};
    for (const auto& snapshot : analyzer.process(block)) {
      static_cast<void>(bank.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.instantaneous_bins_dbfs));
    }
    for (const auto& channel : bank.processSamples(block)) {
      if (channel.text.size() > best.text.size()) best.text = channel.text;
      if (!channel.callsign.empty()) best.callsign = channel.callsign;
    }
    position += take;
    now += static_cast<std::uint64_t>(
        static_cast<long double>(take) * 1'000'000'000.0L / sample_rate);
  }
  best.text = squeeze(best.text);
  return best;
}

std::string decode(const std::vector<float>& audio, const double sample_rate) {
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank bank;
  RealtimeSampleBlock block;
  block.stream.sample_rate_hz = sample_rate;
  std::string best;
  std::size_t position = 0;
  std::uint64_t now = 0;
  while (position < audio.size()) {
    const std::size_t take = std::min<std::size_t>(1'024,
                                                   audio.size() - position);
    block.sample_count = take;
    block.timestamp_ns = now;
    for (std::size_t index = 0; index < take; ++index)
      block.samples[index] = {audio[position + index], 0.0F};
    for (const auto& snapshot : analyzer.process(block)) {
      static_cast<void>(bank.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.instantaneous_bins_dbfs));
    }
    for (const auto& channel : bank.processSamples(block))
      if (channel.text.size() > best.size()) best = channel.text;
    position += take;
    now += static_cast<std::uint64_t>(
        static_cast<long double>(take) * 1'000'000'000.0L / sample_rate);
  }
  return squeeze(best);
}

}  // namespace

int main(int argc, char** argv) {
  const bool full = argc > 1 && std::strcmp(argv[1], "--full") == 0;
  const std::string message = "CQ CQ DE IU0LFQ IU0LFQ K";
  const double sample_rate = 48'000.0;
  const std::vector<std::vector<unsigned>> seed_sets{
      {11U, 222U, 3333U},
      {7U, 101U, 2027U},
      {97U, 1543U, 20161U}};
  const std::vector<double> speeds = full
      ? std::vector<double>{12.0, 16.0, 20.0, 25.0, 30.0, 40.0, 50.0}
      : std::vector<double>{16.0, 25.0, 40.0};
  const std::vector<float> ratios = full
      ? std::vector<float>{30.0F, 20.0F, 15.0F, 12.0F}
      : std::vector<float>{20.0F, 12.0F};

  std::printf("decoder surface: %zu speeds x %zu signal-to-noise ratios,"
              " %zu independent seed sets\n",
              speeds.size(), ratios.size(), seed_sets.size());
  const std::string expected_call = "IU0LFQ";
  std::size_t wrong_callsigns = 0;
  std::size_t right_callsigns = 0;
  std::vector<double> per_set;
  per_set.reserve(seed_sets.size());
  for (std::size_t set = 0; set < seed_sets.size(); ++set) {
    double total = 0.0;
    std::size_t cells = 0;
    for (const double wpm : speeds) {
      for (const float snr_db : ratios) {
        double accumulated = 0.0;
        for (const unsigned seed : seed_sets[set]) {
          const auto audio = synthesize(message, wpm, snr_db, sample_rate,
                                        700.0, seed);
          const auto decoded = decodeChannel(audio, sample_rate);
          accumulated += std::min<double>(
              1.0, static_cast<double>(editDistance(message, decoded.text)) /
                       message.size());
          // Naming the wrong station is worse than naming none: an operator
          // logs what the application asserts. A mis-decoded token sitting in
          // a position where a callsign belongs scores exactly as a correct
          // one does, so this cannot be scored away and is measured instead.
          if (!decoded.callsign.empty() && decoded.callsign != expected_call)
            ++wrong_callsigns;
          if (decoded.callsign == expected_call) ++right_callsigns;
        }
        total += accumulated / static_cast<double>(seed_sets[set].size());
        ++cells;
      }
    }
    const double mean = total / static_cast<double>(cells);
    per_set.push_back(mean);
    std::printf("  seed set %zu   mean character error %.4f\n", set, mean);
  }
  const auto bounds = std::minmax_element(per_set.begin(), per_set.end());
  const double spread = *bounds.second - *bounds.first;
  double sum = 0.0;
  for (const double value : per_set) sum += value;
  std::printf("  overall %.4f, spread across seed sets %.4f\n",
              sum / static_cast<double>(per_set.size()), spread);
  std::printf("  callsign asserted correctly %zu, wrongly %zu\n",
              right_callsigns, wrong_callsigns);
  std::printf("  a difference smaller than %.4f is not a result: compare two"
              " builds on these same seed sets and read the paired\n"
              "  difference per set, never one absolute figure against"
              " another run\n", spread);
  // Reports a measurement; it does not gate. Failing on an absolute threshold
  // here would fail on the noise draw as readily as on a regression.
  return EXIT_SUCCESS;
}
