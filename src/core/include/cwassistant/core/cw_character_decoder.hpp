#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cwassistant::core {

struct CwCharacterTrackKey {
  std::uint64_t track_id{0};
  std::uint64_t track_generation{0};
  std::uint64_t frontend_generation{0};

  [[nodiscard]] bool operator==(const CwCharacterTrackKey&) const = default;
};

struct CwTimedCharacter {
  char symbol{'?'};
  std::uint64_t started_ns{0};
  std::uint64_t ended_ns{0};
  float confidence{0.0F};
};

// A backend result after CTC collapse/alignment. Runtime-specific tensors and
// session objects deliberately stay outside the dependency-free core.
struct CwCharacterHypothesis {
  CwCharacterTrackKey track{};
  std::uint64_t window_sequence{0};
  std::uint64_t window_started_ns{0};
  std::uint64_t window_ended_ns{0};
  std::uint64_t valid_started_ns{0};
  std::uint64_t valid_ended_ns{0};
  std::vector<CwTimedCharacter> characters;
};

struct CwCharacterConsensusConfig {
  std::size_t confirmation_windows{2};
  std::size_t retained_windows{3};
  double alignment_tolerance_ms{60.0};
  float minimum_character_confidence{0.70F};
  std::size_t maximum_characters_per_window{256};
  std::size_t maximum_stable_characters{4'096};
};

struct CwCharacterConsensusUpdate {
  bool changed{false};
  std::string stable_text;
  std::string provisional_text;
  std::uint64_t last_committed_end_ns{0};
};

// Merges overlapping character windows by acoustic time. Stable text is
// append-only; language, callsigns, and conversation context are not inputs.
class CwCharacterConsensusMerger {
 public:
  explicit CwCharacterConsensusMerger(
      CwCharacterConsensusConfig config = {});

  void reset(CwCharacterTrackKey track = {}) noexcept;
  [[nodiscard]] CwCharacterConsensusUpdate process(
      CwCharacterHypothesis hypothesis);
  [[nodiscard]] const std::string& stableText() const noexcept;
  [[nodiscard]] const std::string& provisionalText() const noexcept;
  [[nodiscard]] std::size_t stateBytes() const noexcept;

 private:
  [[nodiscard]] CwCharacterConsensusUpdate snapshot(bool changed) const;

  CwCharacterConsensusConfig config_{};
  CwCharacterTrackKey track_{};
  std::vector<CwCharacterHypothesis> windows_;
  std::string stable_text_;
  std::string provisional_text_;
  std::uint64_t last_window_sequence_{0};
  std::uint64_t last_committed_end_ns_{0};
  bool initialized_{false};
};

}  // namespace cwassistant::core
