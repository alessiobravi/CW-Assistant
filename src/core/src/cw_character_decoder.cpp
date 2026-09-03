#include "cwassistant/core/cw_character_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cwassistant::core {
namespace {

std::uint64_t midpoint(const CwTimedCharacter& character) noexcept {
  return character.started_ns +
         (character.ended_ns - character.started_ns) / 2U;
}

std::vector<int> alignCharacters(
    const std::vector<CwTimedCharacter>& reference,
    const std::vector<CwTimedCharacter>& compared,
    const std::uint64_t tolerance_ns) {
  const std::size_t rows = reference.size() + 1U;
  const std::size_t columns = compared.size() + 1U;
  std::vector<std::uint16_t> scores(rows * columns, 0U);
  for (std::size_t left = 1; left < rows; ++left) {
    for (std::size_t right = 1; right < columns; ++right) {
      const auto left_midpoint = midpoint(reference[left - 1U]);
      const auto right_midpoint = midpoint(compared[right - 1U]);
      const auto difference = left_midpoint > right_midpoint
          ? left_midpoint - right_midpoint : right_midpoint - left_midpoint;
      const bool match = reference[left - 1U].symbol ==
                             compared[right - 1U].symbol &&
                         difference <= tolerance_ns;
      auto& score = scores[left * columns + right];
      score = std::max(scores[(left - 1U) * columns + right],
                       scores[left * columns + right - 1U]);
      if (match) {
        score = std::max<std::uint16_t>(
            score, static_cast<std::uint16_t>(
                       scores[(left - 1U) * columns + right - 1U] + 1U));
      }
    }
  }

  std::vector<int> mapping(reference.size(), -1);
  std::size_t left = reference.size();
  std::size_t right = compared.size();
  while (left > 0U && right > 0U) {
    const auto left_midpoint = midpoint(reference[left - 1U]);
    const auto right_midpoint = midpoint(compared[right - 1U]);
    const auto difference = left_midpoint > right_midpoint
        ? left_midpoint - right_midpoint : right_midpoint - left_midpoint;
    const bool match = reference[left - 1U].symbol ==
                           compared[right - 1U].symbol &&
                       difference <= tolerance_ns &&
                       scores[left * columns + right] ==
                           scores[(left - 1U) * columns + right - 1U] + 1U;
    if (match) {
      mapping[left - 1U] = static_cast<int>(right - 1U);
      --left;
      --right;
    } else if (scores[(left - 1U) * columns + right] >=
               scores[left * columns + right - 1U]) {
      --left;
    } else {
      --right;
    }
  }
  return mapping;
}

bool usableCharacter(const CwTimedCharacter& character,
                     const CwCharacterHypothesis& hypothesis,
                     const float minimum_confidence) noexcept {
  return character.started_ns >= hypothesis.valid_started_ns &&
         character.ended_ns <= hypothesis.valid_ended_ns &&
         character.ended_ns >= character.started_ns &&
         std::isfinite(character.confidence) &&
         character.confidence >= minimum_confidence;
}

bool newerGeneration(const CwCharacterTrackKey& candidate,
                     const CwCharacterTrackKey& current) noexcept {
  return candidate.track_id == current.track_id &&
      (candidate.track_generation > current.track_generation ||
       (candidate.track_generation == current.track_generation &&
        candidate.frontend_generation > current.frontend_generation));
}

}  // namespace

CwCharacterConsensusMerger::CwCharacterConsensusMerger(
    CwCharacterConsensusConfig config)
    : config_(config) {
  config_.confirmation_windows = std::clamp<std::size_t>(
      config_.confirmation_windows, 2U, 4U);
  config_.retained_windows = std::clamp<std::size_t>(
      config_.retained_windows, config_.confirmation_windows, 6U);
  config_.alignment_tolerance_ms = std::clamp(
      std::isfinite(config_.alignment_tolerance_ms)
          ? config_.alignment_tolerance_ms : 60.0,
      10.0, 200.0);
  config_.minimum_character_confidence = std::clamp(
      std::isfinite(config_.minimum_character_confidence)
          ? config_.minimum_character_confidence : 0.70F,
      0.0F, 1.0F);
  config_.maximum_characters_per_window = std::clamp<std::size_t>(
      config_.maximum_characters_per_window, 32U, 1'024U);
  config_.maximum_stable_characters = std::clamp<std::size_t>(
      config_.maximum_stable_characters, 64U, 16'384U);
  windows_.reserve(config_.retained_windows);
}

void CwCharacterConsensusMerger::reset(
    const CwCharacterTrackKey track) noexcept {
  track_ = track;
  windows_.clear();
  stable_text_.clear();
  provisional_text_.clear();
  last_window_sequence_ = 0;
  last_committed_end_ns_ = 0;
  initialized_ = track.track_id != 0;
}

CwCharacterConsensusUpdate CwCharacterConsensusMerger::process(
    CwCharacterHypothesis hypothesis) {
  if (hypothesis.track.track_id == 0 ||
      hypothesis.valid_ended_ns < hypothesis.valid_started_ns ||
      hypothesis.window_ended_ns < hypothesis.window_started_ns ||
      hypothesis.characters.size() >
          config_.maximum_characters_per_window) {
    return snapshot(false);
  }
  if (!initialized_) {
    reset(hypothesis.track);
  } else if (hypothesis.track != track_) {
    if (!newerGeneration(hypothesis.track, track_)) return snapshot(false);
    reset(hypothesis.track);
  }
  if (hypothesis.window_sequence <= last_window_sequence_) {
    return snapshot(false);
  }
  last_window_sequence_ = hypothesis.window_sequence;
  hypothesis.characters.erase(
      std::remove_if(hypothesis.characters.begin(),
                     hypothesis.characters.end(),
                     [](const CwTimedCharacter& character) {
                       return character.ended_ns < character.started_ns;
                     }),
      hypothesis.characters.end());
  std::stable_sort(hypothesis.characters.begin(),
                   hypothesis.characters.end(),
                   [](const CwTimedCharacter& left,
                      const CwTimedCharacter& right) {
                     return midpoint(left) < midpoint(right);
                   });
  windows_.push_back(std::move(hypothesis));
  if (windows_.size() > config_.retained_windows)
    windows_.erase(windows_.begin());

  const auto& newest = windows_.back();
  const std::uint64_t tolerance_ns = static_cast<std::uint64_t>(
      std::llround(config_.alignment_tolerance_ms * 1'000'000.0));
  std::vector<std::vector<int>> mappings;
  mappings.reserve(windows_.size() - 1U);
  for (std::size_t index = 0; index + 1U < windows_.size(); ++index) {
    mappings.push_back(alignCharacters(newest.characters,
                                       windows_[index].characters,
                                       tolerance_ns));
  }

  bool changed = false;
  std::size_t first_uncommitted = newest.characters.size();
  for (std::size_t index = 0; index < newest.characters.size(); ++index) {
    const auto& character = newest.characters[index];
    bool already_committed = character.ended_ns <= last_committed_end_ns_;
    for (std::size_t window = 0;
         !already_committed && window < mappings.size(); ++window) {
      const int compared_index = mappings[window][index];
      already_committed = compared_index >= 0 &&
          windows_[window]
                  .characters[static_cast<std::size_t>(compared_index)]
                  .ended_ns <= last_committed_end_ns_;
    }
    if (already_committed) continue;
    first_uncommitted = index;
    if (!usableCharacter(character, newest,
                         config_.minimum_character_confidence)) {
      break;
    }
    std::size_t confirmations = 1U;
    for (std::size_t window = 0; window < mappings.size(); ++window) {
      const int compared_index = mappings[window][index];
      if (compared_index < 0) continue;
      const auto& compared =
          windows_[window].characters[static_cast<std::size_t>(compared_index)];
      if (usableCharacter(compared, windows_[window],
                          config_.minimum_character_confidence)) {
        ++confirmations;
      }
    }
    if (confirmations < config_.confirmation_windows) break;
    if (stable_text_.size() >= config_.maximum_stable_characters) break;
    stable_text_.push_back(character.symbol);
    last_committed_end_ns_ = character.ended_ns;
    first_uncommitted = index + 1U;
    changed = true;
  }

  std::string provisional;
  for (std::size_t index = first_uncommitted;
       index < newest.characters.size(); ++index) {
    const auto& character = newest.characters[index];
    bool already_committed = character.ended_ns <= last_committed_end_ns_;
    for (std::size_t window = 0;
         !already_committed && window < mappings.size(); ++window) {
      const int compared_index = mappings[window][index];
      already_committed = compared_index >= 0 &&
          windows_[window]
                  .characters[static_cast<std::size_t>(compared_index)]
                  .ended_ns <= last_committed_end_ns_;
    }
    if (already_committed ||
        character.started_ns < newest.valid_started_ns ||
        character.ended_ns > newest.valid_ended_ns) {
      continue;
    }
    provisional.push_back(character.symbol);
  }
  if (provisional != provisional_text_) {
    provisional_text_ = std::move(provisional);
    changed = true;
  }
  return snapshot(changed);
}

const std::string& CwCharacterConsensusMerger::stableText() const noexcept {
  return stable_text_;
}

const std::string& CwCharacterConsensusMerger::provisionalText() const noexcept {
  return provisional_text_;
}

std::size_t CwCharacterConsensusMerger::stateBytes() const noexcept {
  std::size_t result = sizeof(*this) + stable_text_.capacity() +
                       provisional_text_.capacity() +
                       windows_.capacity() * sizeof(CwCharacterHypothesis);
  for (const auto& window : windows_)
    result += window.characters.capacity() * sizeof(CwTimedCharacter);
  return result;
}

CwCharacterConsensusUpdate CwCharacterConsensusMerger::snapshot(
    const bool changed) const {
  return {.changed = changed,
          .stable_text = stable_text_,
          .provisional_text = provisional_text_,
          .last_committed_end_ns = last_committed_end_ns_};
}

}  // namespace cwassistant::core
