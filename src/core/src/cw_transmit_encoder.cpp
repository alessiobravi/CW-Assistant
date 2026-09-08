#include "cwassistant/core/cw_transmit_encoder.hpp"

#include <array>
#include <limits>

namespace cwassistant::core {
namespace {

struct MorseEntry {
  char character;
  std::string_view elements;
};

constexpr std::array<MorseEntry, 54> kMorseTable{{
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."}, {'F', "..-."}, {'G', "--."}, {'H', "...."},
    {'I', ".."}, {'J', ".---"}, {'K', "-.-"}, {'L', ".-.."},
    {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'\'', ".----."},
    {'/', "-..-."}, {'!', "-.-.--"}, {'(', "-.--."}, {')', "-.--.-"},
    {'&', ".-..."}, {':', "---..."}, {';', "-.-.-."}, {'=', "-...-"},
    {'+', ".-.-."}, {'-', "-....-"}, {'"', ".-..-."}, {'$', "...-..-"},
    {'@', ".--.-."}, {' ', ""},
}};

std::string_view elementsFor(const char character) noexcept {
  for (const auto& entry : kMorseTable) {
    if (entry.character == character) return entry.elements;
  }
  return {};
}

bool appendSpan(std::vector<CwTransmitSpan>& spans, const bool key_down,
                const std::uint32_t units) {
  if (units == 0U) return false;
  if (!spans.empty() && spans.back().key_down == key_down) {
    if (spans.back().duration_units >
        std::numeric_limits<std::uint32_t>::max() - units) {
      return false;
    }
    spans.back().duration_units += units;
    return true;
  }
  spans.push_back({.key_down = key_down, .duration_units = units});
  return true;
}

}  // namespace

std::optional<CwTransmitPlan> CwTransmitEncoder::encode(
    const std::string_view normalized_text,
    const std::uint16_t words_per_minute) {
  if (normalized_text.empty() || normalized_text.size() > 512U ||
      words_per_minute < 5U || words_per_minute > 80U ||
      normalized_text.front() == ' ' || normalized_text.back() == ' ') {
    return std::nullopt;
  }

  CwTransmitPlan plan;
  plan.text = normalized_text;
  plan.dot_duration_ns =
      (1'200'000'000ULL + words_per_minute / 2U) / words_per_minute;
  plan.spans.reserve(normalized_text.size() * 8U);

  for (std::size_t character_index = 0;
       character_index < normalized_text.size(); ++character_index) {
    const char character = normalized_text[character_index];
    if (character == ' ') {
      if (character_index == 0U || normalized_text[character_index - 1U] == ' ')
        return std::nullopt;
      continue;
    }

    const std::string_view elements = elementsFor(character);
    if (elements.empty()) return std::nullopt;
    for (std::size_t element_index = 0; element_index < elements.size();
         ++element_index) {
      if (!appendSpan(plan.spans, true,
                      elements[element_index] == '-' ? 3U : 1U)) {
        return std::nullopt;
      }
      if (element_index + 1U < elements.size() &&
          !appendSpan(plan.spans, false, 1U)) {
        return std::nullopt;
      }
    }

    if (character_index + 1U < normalized_text.size()) {
      const bool word_boundary = normalized_text[character_index + 1U] == ' ';
      if (!appendSpan(plan.spans, false, word_boundary ? 7U : 3U))
        return std::nullopt;
    }
  }

  if (plan.spans.empty() || !plan.spans.front().key_down ||
      !plan.spans.back().key_down) {
    return std::nullopt;
  }
  std::uint64_t total_units = 0U;
  for (const auto& span : plan.spans) total_units += span.duration_units;
  if (total_units >
      std::numeric_limits<std::uint64_t>::max() / plan.dot_duration_ns) {
    return std::nullopt;
  }
  plan.total_duration_ns = total_units * plan.dot_duration_ns;
  return plan;
}

}  // namespace cwassistant::core
