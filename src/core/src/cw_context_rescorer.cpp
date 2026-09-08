#include "cwassistant/core/cw_context_rescorer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

double contextBonus(const std::string_view text) {
  static constexpr std::array<std::string_view, 13> kExchangeWords{
      "CQ", "DE", "QRZ", "PSE", "K", "KN", "AR", "SK", "TU",
      "UP", "RST", "599", "5NN"};
  std::vector<std::string> words;
  std::string word;
  std::size_t unknowns = 0;
  std::size_t oversized = 0;
  for (const unsigned char character : text) {
    if (std::isalnum(character) != 0 || character == '/') {
      word.push_back(static_cast<char>(std::toupper(character)));
    } else {
      if (character == '?') ++unknowns;
      if (!word.empty()) {
        if (word.size() > 16U) ++oversized;
        words.push_back(std::move(word));
        word.clear();
      }
    }
  }
  if (!word.empty()) {
    if (word.size() > 16U) ++oversized;
    words.push_back(std::move(word));
  }

  double bonus = 0.0;
  for (const auto& token : words) {
    if (std::ranges::find(kExchangeWords, token) != kExchangeWords.end()) {
      bonus += 1.0;
    }
  }
  std::string completed(text);
  if (completed.empty() || completed.back() != ' ') completed.push_back(' ');
  if (CallsignPolicy::best_complete_in_text(completed)) bonus += 2.0;
  if (!CallsignPolicy::qso_participants_in_text(completed).empty()) {
    bonus += 2.0;
  }
  bonus -= 0.30 * static_cast<double>(unknowns);
  bonus -= 1.0 * static_cast<double>(oversized);
  return bonus;
}

bool plausibleCall(const std::string_view token) {
  std::string evidence = "CQ ";
  evidence.append(token);
  evidence.push_back(' ');
  const auto call = CallsignPolicy::best_complete_in_text(evidence);
  return call && *call == token;
}

std::vector<std::string> splitLeadingExchangeWords(
    const std::string_view token) {
  static constexpr std::array<std::string_view, 4> kPrefixes{
      "QRZ", "CQ", "DE", "TU"};
  std::vector<std::string> best;
  const auto search = [&](auto&& self, const std::string_view remaining,
                          std::vector<std::string> prefix) -> void {
    if (plausibleCall(remaining) && prefix.size() + 1U > best.size()) {
      prefix.emplace_back(remaining);
      best = std::move(prefix);
      return;
    }
    for (const auto known : kPrefixes) {
      if (remaining.size() <= known.size() ||
          !remaining.starts_with(known)) {
        continue;
      }
      auto next = prefix;
      next.emplace_back(known);
      self(self, remaining.substr(known.size()), std::move(next));
    }
  };
  search(search, token, {});
  return best;
}

std::string charactersWithoutWhitespace(const std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const unsigned char character : text) {
    if (std::isspace(character) == 0)
      result.push_back(static_cast<char>(character));
  }
  return result;
}

}  // namespace

CwContextSelection selectCwContextAlternative(
    const std::span<const CwContextAlternative> alternatives,
    const double competitive_cost_margin) {
  if (alternatives.empty()) return {};
  const double margin = std::clamp(
      std::isfinite(competitive_cost_margin) ? competitive_cost_margin : 1.0,
      0.0, 5.0);
  const auto acoustic_best = std::min_element(
      alternatives.begin(), alternatives.end(),
      [](const CwContextAlternative& left,
         const CwContextAlternative& right) {
        const double left_cost = std::isfinite(left.acoustic_cost)
            ? left.acoustic_cost : std::numeric_limits<double>::infinity();
        const double right_cost = std::isfinite(right.acoustic_cost)
            ? right.acoustic_cost : std::numeric_limits<double>::infinity();
        return left_cost < right_cost;
      });
  if (acoustic_best == alternatives.end() ||
      !std::isfinite(acoustic_best->acoustic_cost)) return {};
  const std::size_t acoustic_best_index = static_cast<std::size_t>(
      std::distance(alternatives.begin(), acoustic_best));
  const double best_acoustic = acoustic_best->acoustic_cost;
  CwContextSelection result{.index = acoustic_best_index,
                            .acoustic_cost = best_acoustic,
                            .context_bonus = contextBonus(
                                acoustic_best->text)};
  // At most three quarters of one acoustic cost unit may be recovered through
  // context. That is enough to choose a credible missing word gap inside the
  // lattice's competitive set, but never enough to conceal materially worse
  // timing evidence.
  auto adjusted_cost = [](const CwContextAlternative& candidate,
                          const double bonus) {
    return candidate.acoustic_cost - std::clamp(0.10 * bonus, -0.25, 0.75);
  };
  double best_adjusted = adjusted_cost(*acoustic_best,
                                       result.context_bonus);
  const std::string acoustic_characters = charactersWithoutWhitespace(
      acoustic_best->text);
  for (std::size_t index = 0; index < alternatives.size(); ++index) {
    if (index == acoustic_best_index) continue;
    const auto& candidate = alternatives[index];
    if (!std::isfinite(candidate.acoustic_cost) ||
        candidate.acoustic_cost > best_acoustic + margin ||
        charactersWithoutWhitespace(candidate.text) != acoustic_characters) {
      continue;
    }
    const double bonus = contextBonus(candidate.text);
    const double adjusted = adjusted_cost(candidate, bonus);
    if (adjusted + 1e-9 < best_adjusted) {
      result = {.index = index,
                .acoustic_cost = candidate.acoustic_cost,
                .context_bonus = bonus};
      best_adjusted = adjusted;
    }
  }
  return result;
}

std::string reconstructCwWordGaps(const std::string_view text) {
  std::string output;
  std::string token;
  const auto append_word = [&output](const std::string_view word) {
    if (!output.empty() && output.back() != ' ') output.push_back(' ');
    output.append(word);
  };
  const auto emit = [&] {
    if (token.empty()) return;
    std::string upper;
    upper.reserve(token.size());
    for (const unsigned char character : token)
      upper.push_back(static_cast<char>(std::toupper(character)));
    if (upper == "PSEK") {
      append_word("PSE");
      append_word("K");
    } else if (const auto split = splitLeadingExchangeWords(upper);
               split.size() > 1U) {
      for (const auto& word : split) append_word(word);
    } else {
      append_word(token);
    }
    token.clear();
  };
  for (const unsigned char character : text) {
    if (std::isalnum(character) != 0 || character == '/') {
      token.push_back(static_cast<char>(character));
    } else if (std::isspace(character) != 0) {
      emit();
    } else {
      emit();
      if (!output.empty() && output.back() != ' ') output.push_back(' ');
      output.push_back(static_cast<char>(character));
    }
  }
  emit();
  return output;
}

}  // namespace cwassistant::core
