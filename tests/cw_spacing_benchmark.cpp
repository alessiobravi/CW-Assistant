// Measures where the decoder puts word boundaries, which is the one thing the
// context vocabulary can change and the one thing no other benchmark can see.
//
// The synthetic surface never exercises the context rescorer, and the capture
// corpus scores callsign recovery rather than spacing, so a vocabulary change
// was previously unmeasurable in either direction. Guenther measured on real
// hand-sent Morse that character-gap and word-gap durations overlap heavily --
// unlike dot against dash, which separate cleanly -- so spacing is exactly
// where the remaining error lives and exactly what a threshold cannot settle.
//
// Method: generate contacts whose gaps are jittered until character and word
// gaps genuinely overlap, decode each twice in one process from the same
// samples -- once with the shipped vocabulary loaded, once with it cleared --
// and score only those decodes whose non-whitespace characters already match
// the message. Scoring the rest would confuse a lettering error with a spacing
// one; the rescorer cannot change a letter, so a decode that lost characters
// says nothing about the vocabulary.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/cw_vocabulary.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"

using cwassistant::core::CwChannelBank;
using cwassistant::core::CwChannelBankConfig;
using cwassistant::core::CwVocabulary;
using cwassistant::core::RealtimeSampleBlock;
using cwassistant::core::SpectrumAnalyzer;

namespace {

const char* morse(const char symbol) {
  switch (symbol) {
    case 'A': return ".-";      case 'B': return "-...";
    case 'C': return "-.-.";    case 'D': return "-..";
    case 'E': return ".";       case 'F': return "..-.";
    case 'G': return "--.";     case 'H': return "....";
    case 'I': return "..";      case 'J': return ".---";
    case 'K': return "-.-";     case 'L': return ".-..";
    case 'M': return "--";      case 'N': return "-.";
    case 'O': return "---";     case 'P': return ".--.";
    case 'Q': return "--.-";    case 'R': return ".-.";
    case 'S': return "...";     case 'T': return "-";
    case 'U': return "..-";     case 'V': return "...-";
    case 'W': return ".--";     case 'X': return "-..-";
    case 'Y': return "-.--";    case 'Z': return "--..";
    case '0': return "-----";   case '1': return ".----";
    case '2': return "..---";   case '3': return "...--";
    case '4': return "....-";   case '5': return ".....";
    case '6': return "-....";   case '7': return "--...";
    case '8': return "---..";   case '9': return "----.";
    case '/': return "-..-.";
    default: return "";
  }
}

float portableGaussianLike(std::mt19937& generator) noexcept {
  constexpr double scale = 1.0 / 4'294'967'296.0;
  double sum = 0.0;
  for (int draw = 0; draw < 12; ++draw)
    sum += (static_cast<double>(generator()) + 0.5) * scale;
  return static_cast<float>(sum - 6.0);
}

// Jitters gaps toward each other rather than uniformly: word gaps are
// compressed and character gaps stretched, which is the measured shape of a
// real fist and the case a duration threshold cannot resolve. `overlap` at 0
// is machine timing; at 1 the two distributions meet.
std::vector<float> synthesize(const std::string& message, const double wpm,
                              const float snr_db, const double sample_rate,
                              const double tone_hz, const double overlap,
                              const unsigned seed) {
  const double dot = 1.2 / wpm;
  const double rise = 0.005;
  std::mt19937 timing(seed ^ 0x5bd1u);
  const auto jitter = [&timing](const double spread) {
    return 1.0 + spread * ((static_cast<double>(timing() % 2001u) / 1000.0) - 1.0);
  };
  std::vector<std::pair<double, bool>> runs;
  const auto push_gap = [&runs](const double length) {
    if (runs.empty()) return;
    if (runs.back().second) runs.push_back({length, false});
    else runs.back().first += length;
  };
  // A turn becomes semantic only after a silence far longer than a word gap,
  // and only once the decoder sees the next transmission begin. One message
  // followed by a dead carrier never closes a turn, so the scene carries a
  // second short transmission after a long gap; the first turn is the one
  // scored.
  const std::string scene = message + std::string("\x01") + "K";
  for (std::size_t index = 0; index < scene.size(); ++index) {
    const char symbol = scene[index];
    if (symbol == '\x01') {
      push_gap(12.0 * dot * 8.0);
      continue;
    }
    if (symbol == ' ') {
      // A word gap is nominally 7 dots. Pull it toward 3 and jitter it.
      const double nominal = 7.0 - 3.0 * overlap;
      push_gap((nominal - 3.0) * dot * jitter(0.25 * overlap));
      continue;
    }
    const char* elements = morse(symbol);
    if (*elements == '\0') continue;
    for (const char* element = elements; *element != '\0'; ++element) {
      runs.push_back({(*element == '-' ? 3.0 : 1.0) * dot * jitter(0.10 * overlap),
                      true});
      if (*(element + 1) != '\0')
        runs.push_back({dot * jitter(0.10 * overlap), false});
    }
    // A character gap is nominally 3 dots; push it up toward the word gap.
    push_gap((3.0 + 1.2 * overlap) * dot * jitter(0.20 * overlap));
  }

  double total = 0.0;
  for (const auto& run : runs) total += run.first;
  // The tail must carry the same noise floor as the message. Digital silence
  // drops the track instead of ending its turn, and the context rescorer only
  // runs when a turn completes.
  const auto count = static_cast<std::size_t>((total + 6.0) * sample_rate);
  std::vector<float> audio(count, 0.0F);
  const float noise = 0.02F;
  const float amplitude = noise * std::sqrt(
      std::pow(10.0F, snr_db / 10.0F) /
      static_cast<float>(sample_rate / 2.0 / 120.0));
  std::mt19937 generator(seed);
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
    audio[step] += noise * portableGaussianLike(generator);
  return audio;
}

std::string decodeChannel(const std::vector<float>& audio,
                          const double sample_rate) {
  SpectrumAnalyzer analyzer({.audio_upper_frequency_hz = 3'000.0});
  CwChannelBank bank{CwChannelBankConfig{}};
  RealtimeSampleBlock block;
  block.stream.sample_rate_hz = sample_rate;
  std::string best;
  std::set<std::pair<std::uint32_t, std::uint64_t>> seen;
  std::size_t position = 0;
  std::uint64_t now = 0;
  while (position < audio.size()) {
    const std::size_t take = std::min<std::size_t>(1'024,
                                                   audio.size() - position);
    block.sample_count = take;
    block.timestamp_ns = now;
    for (std::size_t i = 0; i < take; ++i)
      block.samples[i] = {audio[position + i], 0.0F};
    for (const auto& snapshot : analyzer.process(block)) {
      static_cast<void>(bank.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.instantaneous_bins_dbfs,
          false));
    }
    // The context rescorer runs in completeTransmission, so its result reaches
    // a completed turn and never the per-block text. Scoring `text` measures
    // the acoustic lattice alone and is blind to the vocabulary by
    // construction; the benchmark must let the turn finish and read that.
    for (const auto& channel : bank.processSamples(block)) {
      // Only the first completed turn corresponds to the message; the short
      // second transmission exists solely to close it.
      for (const auto& turn : channel.transmissions) {
        const auto key = std::pair{channel.id, turn.sequence};
        if (!seen.insert(key).second) continue;
        if (turn.text.empty()) continue;
        if (best.empty() || turn.text.size() > best.size()) best = turn.text;
        break;
      }
    }
    position += take;
    now += static_cast<std::uint64_t>(
        static_cast<long double>(take) * 1'000'000'000.0L / sample_rate);
  }
  return best;
}

// The boundary positions of a text, expressed as counts of non-whitespace
// characters seen before each gap. Comparing these rather than the strings
// makes the score independent of how many spaces were emitted.
std::vector<std::size_t> boundaries(const std::string_view text) {
  std::vector<std::size_t> result;
  std::size_t characters = 0;
  bool pending = false;
  for (const unsigned char symbol : text) {
    if (std::isspace(symbol) != 0) {
      if (characters > 0) pending = true;
      continue;
    }
    if (pending) {
      result.push_back(characters);
      pending = false;
    }
    ++characters;
  }
  return result;
}

std::string withoutWhitespace(const std::string_view text) {
  std::string result;
  for (const unsigned char symbol : text)
    if (std::isspace(symbol) == 0)
      result.push_back(static_cast<char>(std::toupper(symbol)));
  return result;
}

struct Score {
  std::size_t scorable{0};
  std::size_t unscorable{0};
  std::size_t correct{0};
  std::size_t missing{0};
  std::size_t spurious{0};
};

void accumulate(Score& score, const std::string& truth,
                const std::string& decoded) {
  if (withoutWhitespace(truth) != withoutWhitespace(decoded)) {
    ++score.unscorable;
    return;
  }
  ++score.scorable;
  const auto expected = boundaries(truth);
  const auto actual = boundaries(decoded);
  for (const auto position : expected) {
    if (std::ranges::find(actual, position) != actual.end()) ++score.correct;
    else ++score.missing;
  }
  for (const auto position : actual)
    if (std::ranges::find(expected, position) == expected.end())
      ++score.spurious;
}

void report(const char* label, const Score& score) {
  const auto expected = score.correct + score.missing;
  const double recall = expected == 0 ? 0.0
      : static_cast<double>(score.correct) / static_cast<double>(expected);
  const auto asserted = score.correct + score.spurious;
  const double precision = asserted == 0 ? 0.0
      : static_cast<double>(score.correct) / static_cast<double>(asserted);
  const double f1 = (precision + recall) <= 0.0 ? 0.0
      : 2.0 * precision * recall / (precision + recall);
  std::printf(
      "  %-22s boundaries %3zu correct, %3zu missing, %3zu spurious"
      "   precision %.3f  recall %.3f  F1 %.3f   (scored %zu of %zu)\n",
      label, score.correct, score.missing, score.spurious, precision, recall,
      f1, score.scorable, score.scorable + score.unscorable);
}

bool loadShippedVocabulary(CwVocabulary& vocabulary) {
  const auto read = [](const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  };
  const std::string directory = CWA_DICTIONARY_DIR;
  vocabulary.clear();
  const auto words = vocabulary.importExchangeWords(
      read(directory + "/cw-abbreviations.txt"));
  static_cast<void>(vocabulary.importWordGapPrefixes(
      read(directory + "/cw-word-gap-prefixes.txt")));
  return words.inserted_tokens > 0;
}

}  // namespace

int main() {
  // Contacts built from the vocabulary an operator actually sends, because a
  // message with no recognisable token cannot distinguish the two arms.
  const std::vector<std::string> messages{
      "CQ CQ DE IU0LFQ IU0LFQ K",
      "IU0LFQ DE DL1NKB QRZ K",
      "R TU 599 599 QSL DE IU0LFQ",
      "QTH ROMA ES NAME ALESSIO HW CPY",
      "QRS PSE AGN QRM ES QSB HR",
      "TU 5NN NR 123 QSL VY 73 SK",
  };
  constexpr double kSampleRate = 48'000.0;
  constexpr double kToneHz = 700.0;

  if (!loadShippedVocabulary(cwassistant::core::cwSharedVocabulary())) {
    std::printf("FAIL: the shipped CW dictionaries did not load\n");
    return 2;
  }
  const CwVocabulary loaded = cwassistant::core::cwSharedVocabulary();

  std::printf("word-boundary placement under overlapping character and word gaps\n");
  int status = 0;
  for (const double overlap : {0.6, 0.85}) {
    Score with;
    Score without;
    for (unsigned seed = 1; seed <= 2U; ++seed) {
      for (const auto& message : messages) {
        for (const double wpm : {18.0, 26.0}) {
          const auto audio = synthesize(message, wpm, 26.0F, kSampleRate,
                                        kToneHz, overlap, seed);
          cwassistant::core::cwSharedVocabulary() = loaded;
          accumulate(with, message, decodeChannel(audio, kSampleRate));
          cwassistant::core::cwSharedVocabulary().clear();
          accumulate(without, message, decodeChannel(audio, kSampleRate));
        }
      }
    }
    std::printf("gap overlap %.2f\n", overlap);
    report("with vocabulary", with);
    report("without vocabulary", without);
    // The vocabulary may only ever help placement. It cannot change a
    // character, so a decode it makes unscorable would be a real defect.
    if (with.scorable < without.scorable) {
      std::printf("FAIL: the vocabulary changed decoded characters\n");
      status = 1;
    }
    // It must not invent boundaries either: a spacing the timing does not
    // support is exactly the failure a vocabulary this size could introduce.
    if (with.spurious > without.spurious) {
      std::printf("FAIL: the vocabulary asserted %zu spurious boundaries "
                  "against %zu without it\n", with.spurious, without.spurious);
      status = 1;
    }
    // And it must earn its place. Without this the vocabulary could rot to
    // nothing -- a file emptied, a loader silently failing -- and every other
    // gate in the suite would still pass.
    if (with.correct <= without.correct) {
      std::printf("FAIL: the vocabulary recovered no boundaries (%zu against "
                  "%zu without it)\n", with.correct, without.correct);
      status = 1;
    }
  }
  cwassistant::core::cwSharedVocabulary() = loaded;
  return status;
}
