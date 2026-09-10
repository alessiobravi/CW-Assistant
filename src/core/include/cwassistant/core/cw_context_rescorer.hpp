#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "cwassistant/core/cw_vocabulary.hpp"

namespace cwassistant::core {

struct CwContextAlternative {
  std::string text;
  double acoustic_cost{0.0};
};

struct CwContextSelection {
  std::size_t index{0};
  double acoustic_cost{0.0};
  double context_bonus{0.0};
};

// Selects only among acoustically competitive paths. Context can resolve an
// uncertain word boundary, but its bounded bonus can never rescue a path that
// the acoustic lattice rejected or change any decoded character.
[[nodiscard]] CwContextSelection selectCwContextAlternative(
    std::span<const CwContextAlternative> alternatives,
    double competitive_cost_margin = 1.0,
    const CwVocabulary& vocabulary = cwSharedVocabulary());

// Reconstructs only a small set of missing boundaries around standard CW
// exchange words and an independently plausible callsign. Characters and
// punctuation are never inserted, removed, or substituted.
[[nodiscard]] std::string reconstructCwWordGaps(
    std::string_view text,
    const CwVocabulary& vocabulary = cwSharedVocabulary());

}  // namespace cwassistant::core
