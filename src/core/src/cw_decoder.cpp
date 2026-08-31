#include "cwassistant/core/cw_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace cwassistant::core {
namespace {

char decode_elements(const std::string_view value) {
  struct Entry { std::string_view code; char symbol; };
  static constexpr Entry table[]{
      {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},
      {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'},
      {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'},
      {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'},
      {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
      {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'},
      {"-.--", 'Y'}, {"--..", 'Z'}, {"-----", '0'}, {".----", '1'},
      {"..---", '2'}, {"...--", '3'}, {"....-", '4'}, {".....", '5'},
      {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
  };
  for (const auto& entry : table) if (entry.code == value) return entry.symbol;
  return '?';
}

double milliseconds(const std::uint64_t value) {
  return static_cast<double>(value) / 1'000'000.0;
}

}  // namespace

CwTimingDecoder::CwTimingDecoder(CwDecoderConfig config) : config_(config) {
  config_.initial_wpm = std::clamp(config_.initial_wpm, 5.0, 80.0);
  config_.key_off_snr_db = std::min(config_.key_off_snr_db,
                                    config_.key_on_snr_db);
  reset();
}

void CwTimingDecoder::reset() noexcept {
  text_.clear(); elements_.clear();
  dot_ms_ = 1'200.0 / config_.initial_wpm;
  state_started_ns_ = 0; last_snr_db_ = 0.0F; confidence_ = 0.0F;
  initialized_ = false; key_down_ = false; character_finished_ = false;
  word_space_emitted_ = false;
}

CwDecoderUpdate CwTimingDecoder::process(const std::uint64_t timestamp_ns,
                                         const float snr_db) {
  last_snr_db_ = snr_db;
  const bool observed_down = key_down_ ? snr_db >= config_.key_off_snr_db
                                       : snr_db >= config_.key_on_snr_db;
  if (!initialized_) {
    initialized_ = true; key_down_ = observed_down;
    state_started_ns_ = timestamp_ns;
    return snapshot(false);
  }
  if (timestamp_ns < state_started_ns_) {
    reset();
    initialized_ = true;
    key_down_ = observed_down;
    state_started_ns_ = timestamp_ns;
    return snapshot(true);
  }
  bool changed = false;
  if (observed_down != key_down_) {
    const double duration_ms = milliseconds(timestamp_ns - state_started_ns_);
    if (key_down_) {
      finishElement(duration_ms); changed = true;
    } else {
      if (!character_finished_ && duration_ms >= 2.2 * dot_ms_) {
        finishCharacter(); changed = true;
      }
      if (duration_ms >= 6.0 * dot_ms_ && !word_space_emitted_ && !text_.empty()) {
        if (text_.back() != ' ') text_.push_back(' ');
        word_space_emitted_ = true; changed = true;
      }
    }
    key_down_ = observed_down; state_started_ns_ = timestamp_ns;
    if (key_down_) { character_finished_ = false; word_space_emitted_ = false; }
  } else if (!key_down_) {
    const double duration_ms = milliseconds(timestamp_ns - state_started_ns_);
    if (!character_finished_ && !elements_.empty() && duration_ms >= 2.2 * dot_ms_) {
      finishCharacter(); changed = true;
    }
    if (character_finished_ && !word_space_emitted_ && !text_.empty() &&
        duration_ms >= 6.0 * dot_ms_) {
      if (text_.back() != ' ') text_.push_back(' ');
      word_space_emitted_ = true; changed = true;
    }
  }
  if (observed_down) {
    confidence_ = std::clamp(
        (snr_db - config_.key_off_snr_db) / 12.0F, 0.0F, 1.0F);
  } else {
    confidence_ *= 0.995F;
  }
  return snapshot(changed);
}

CwDecoderUpdate CwTimingDecoder::flush(const std::uint64_t timestamp_ns) {
  auto result = process(timestamp_ns, -100.0F);
  if (!elements_.empty()) { finishCharacter(); result = snapshot(true); }
  return result;
}

void CwTimingDecoder::finishElement(const double duration_ms) {
  const bool dash = duration_ms > 2.0 * dot_ms_;
  elements_.push_back(dash ? '-' : '.');
  const double estimate = dash ? duration_ms / 3.0 : duration_ms;
  if (estimate >= 15.0 && estimate <= 240.0)
    dot_ms_ += 0.18 * (estimate - dot_ms_);
  character_finished_ = false;
}

void CwTimingDecoder::finishCharacter() {
  if (elements_.empty()) return;
  text_.push_back(decode_elements(elements_));
  if (text_.size() > 4'096) text_.erase(0, text_.size() - 4'096);
  elements_.clear(); character_finished_ = true;
}

CwDecoderUpdate CwTimingDecoder::snapshot(const bool changed) const {
  return {.changed = changed, .key_down = key_down_,
          .wpm = 1'200.0 / dot_ms_, .confidence = confidence_,
          .text = text_, .pending_elements = elements_};
}

}  // namespace cwassistant::core
