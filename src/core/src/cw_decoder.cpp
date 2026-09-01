#include "cwassistant/core/cw_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace cwassistant::core {
namespace {

std::string_view decode_elements(const std::string_view value) {
  struct Entry { std::string_view code; std::string_view symbol; };
  static constexpr Entry table[]{
      {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"},
      {".", "E"}, {"..-.", "F"}, {"--.", "G"}, {"....", "H"},
      {"..", "I"}, {".---", "J"}, {"-.-", "K"}, {".-..", "L"},
      {"--", "M"}, {"-.", "N"}, {"---", "O"}, {".--.", "P"},
      {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"},
      {"..-", "U"}, {"...-", "V"}, {".--", "W"}, {"-..-", "X"},
      {"-.--", "Y"}, {"--..", "Z"}, {"-----", "0"}, {".----", "1"},
      {"..---", "2"}, {"...--", "3"}, {"....-", "4"}, {".....", "5"},
      {"-....", "6"}, {"--...", "7"}, {"---..", "8"}, {"----.", "9"},
      {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"},
      {".----.", "'"}, {"-.-.--", "!"}, {"-..-.", "/"},
      {"-.--.", "("}, {"-.--.-", ")"}, {".-...", "&"},
      {"---...", ":"}, {"-.-.-.", ";"}, {"-...-", "="},
      {".-.-.", "+"}, {"-....-", "-"}, {"..--.-", "_"},
      {".-..-.", "\""}, {"...-..-", "$"}, {".--.-.", "@"},
      {"...-.-", "<SK>"}, {"...---...", "<SOS>"},
  };
  for (const auto& entry : table) if (entry.code == value) return entry.symbol;
  return "?";
}

double milliseconds(const std::uint64_t value) {
  return static_cast<double>(value) / 1'000'000.0;
}

}  // namespace

CwTimingDecoder::CwTimingDecoder(CwDecoderConfig config) : config_(config) {
  config_.initial_wpm = std::clamp(config_.initial_wpm, 5.0, 80.0);
  config_.key_off_snr_db = std::min(config_.key_off_snr_db,
                                    config_.key_on_snr_db);
  config_.key_on_probability =
      std::clamp(config_.key_on_probability, 0.51F, 0.95F);
  config_.key_off_probability = std::clamp(
      config_.key_off_probability, 0.05F, config_.key_on_probability - 0.05F);
  config_.evidence_time_constant_ms =
      std::clamp(config_.evidence_time_constant_ms, 1.0, 100.0);
  config_.character_gap_dots =
      std::clamp(config_.character_gap_dots, 1.6, 2.8);
  config_.stable_gap_dots = std::clamp(
      config_.stable_gap_dots, config_.character_gap_dots + 0.2, 4.5);
  config_.word_gap_dots =
      std::clamp(config_.word_gap_dots, config_.stable_gap_dots + 0.5, 9.0);
  reset();
}

void CwTimingDecoder::reset() noexcept {
  stable_text_.clear(); provisional_text_.clear(); elements_.clear();
  dot_ms_ = 1'200.0 / config_.initial_wpm;
  state_started_ns_ = 0; last_timestamp_ns_ = 0;
  last_snr_db_ = 0.0F; key_down_probability_ = 0.0F;
  confidence_ = 0.0F; element_confidence_sum_ = 0.0F;
  mark_probability_sum_ = 0.0F; mark_probability_duration_ms_ = 0.0;
  element_count_ = 0;
  initialized_ = false; key_down_ = false; character_finished_ = false;
  word_space_emitted_ = false;
}

CwDecoderUpdate CwTimingDecoder::process(const std::uint64_t timestamp_ns,
                                         const float snr_db) {
  last_snr_db_ = snr_db;
  const float instantaneous_probability = probabilityForSnr(snr_db);
  if (!initialized_) {
    initialized_ = true;
    key_down_probability_ = instantaneous_probability;
    key_down_ = key_down_probability_ >= config_.key_on_probability;
    state_started_ns_ = timestamp_ns;
    last_timestamp_ns_ = timestamp_ns;
    return snapshot(false);
  }
  if (timestamp_ns < state_started_ns_ || timestamp_ns < last_timestamp_ns_) {
    reset();
    initialized_ = true;
    key_down_probability_ = instantaneous_probability;
    key_down_ = key_down_probability_ >= config_.key_on_probability;
    state_started_ns_ = timestamp_ns;
    last_timestamp_ns_ = timestamp_ns;
    return snapshot(true);
  }

  const double elapsed_ms = milliseconds(timestamp_ns - last_timestamp_ns_);
  const float smoothing = static_cast<float>(std::clamp(
      elapsed_ms / config_.evidence_time_constant_ms, 0.0, 1.0));
  key_down_probability_ +=
      smoothing * (instantaneous_probability - key_down_probability_);
  if (key_down_ && elapsed_ms > 0.0) {
    mark_probability_sum_ +=
        key_down_probability_ * static_cast<float>(elapsed_ms);
    mark_probability_duration_ms_ += elapsed_ms;
  }
  last_timestamp_ns_ = timestamp_ns;

  const bool observed_down =
      key_down_ ? key_down_probability_ >= config_.key_off_probability
                : key_down_probability_ >= config_.key_on_probability;
  bool changed = false;
  if (observed_down != key_down_) {
    const double duration_ms = milliseconds(timestamp_ns - state_started_ns_);
    if (key_down_) {
      finishElement(duration_ms); changed = true;
    } else {
      if (!character_finished_ &&
          duration_ms >= config_.character_gap_dots * dot_ms_) {
        finishCharacter(); changed = true;
      }
      if (!provisional_text_.empty()) {
        promoteProvisional();
        changed = true;
      }
      if (duration_ms >= config_.word_gap_dots * dot_ms_ &&
          !word_space_emitted_ && !stable_text_.empty()) {
        if (stable_text_.back() != ' ') stable_text_.push_back(' ');
        word_space_emitted_ = true; changed = true;
      }
    }
    key_down_ = observed_down; state_started_ns_ = timestamp_ns;
    if (key_down_) {
      mark_probability_sum_ = 0.0F;
      mark_probability_duration_ms_ = 0.0;
      character_finished_ = false;
      word_space_emitted_ = false;
    }
  } else if (!key_down_) {
    const double duration_ms = milliseconds(timestamp_ns - state_started_ns_);
    if (!character_finished_ && !elements_.empty() &&
        duration_ms >= config_.character_gap_dots * dot_ms_) {
      finishCharacter(); changed = true;
    }
    if (character_finished_ && !provisional_text_.empty() &&
        duration_ms >= config_.stable_gap_dots * dot_ms_) {
      promoteProvisional(); changed = true;
    }
    if (character_finished_ && !word_space_emitted_ && !stable_text_.empty() &&
        duration_ms >= config_.word_gap_dots * dot_ms_) {
      if (stable_text_.back() != ' ') stable_text_.push_back(' ');
      word_space_emitted_ = true; changed = true;
    }
  }
  return snapshot(changed);
}

CwDecoderUpdate CwTimingDecoder::flush(const std::uint64_t timestamp_ns) {
  auto result = process(timestamp_ns, -100.0F);
  if (!elements_.empty()) { finishCharacter(); result = snapshot(true); }
  if (!provisional_text_.empty()) {
    promoteProvisional();
    result = snapshot(true);
  }
  return result;
}

void CwTimingDecoder::finishElement(const double duration_ms) {
  const double ratio = duration_ms / dot_ms_;
  const double dot_distance = std::abs(ratio - 1.0);
  const double dash_distance = std::abs(ratio - 3.0) / 1.5;
  const bool dash = dash_distance < dot_distance;
  elements_.push_back(dash ? '-' : '.');
  if (elements_.size() > 9) elements_.erase(0, elements_.size() - 9);

  const float mark_confidence = mark_probability_duration_ms_ > 0.0
      ? std::clamp(mark_probability_sum_ /
                       static_cast<float>(mark_probability_duration_ms_),
                   0.0F, 1.0F)
      : key_down_probability_;
  const float timing_confidence = static_cast<float>(std::exp(
      -1.25 * std::min(dot_distance, dash_distance)));
  element_confidence_sum_ += mark_confidence * timing_confidence;
  if (element_count_ < 255) ++element_count_;

  const double estimate = dash ? duration_ms / 3.0 : duration_ms;
  const float element_confidence = mark_confidence * timing_confidence;
  if (estimate >= 15.0 && estimate <= 240.0 && element_confidence >= 0.35F) {
    const double adaptation = 0.08 + 0.14 * element_confidence;
    dot_ms_ += adaptation * (estimate - dot_ms_);
  }
  character_finished_ = false;
}

void CwTimingDecoder::finishCharacter() {
  if (elements_.empty()) return;
  provisional_text_ = std::string(decode_elements(elements_));
  confidence_ = element_count_ == 0
      ? 0.0F
      : std::clamp(element_confidence_sum_ /
                       static_cast<float>(element_count_),
                   0.0F, 1.0F);
  if (provisional_text_ == "?") confidence_ *= 0.35F;
  elements_.clear(); character_finished_ = true;
  element_confidence_sum_ = 0.0F;
  element_count_ = 0;
}

void CwTimingDecoder::promoteProvisional() {
  if (provisional_text_.empty()) return;
  stable_text_ += provisional_text_;
  if (stable_text_.size() > 4'096)
    stable_text_.erase(0, stable_text_.size() - 4'096);
  provisional_text_.clear();
}

float CwTimingDecoder::probabilityForSnr(const float snr_db) const noexcept {
  const float midpoint =
      0.5F * (config_.key_on_snr_db + config_.key_off_snr_db);
  const float scale = std::max(
      0.75F, 0.5F * (config_.key_on_snr_db - config_.key_off_snr_db));
  const float normalized = std::clamp((snr_db - midpoint) / scale,
                                      -20.0F, 20.0F);
  return 1.0F / (1.0F + std::exp(-normalized));
}

CwDecoderUpdate CwTimingDecoder::snapshot(const bool changed) const {
  return {.changed = changed, .key_down = key_down_,
          .key_down_probability = key_down_probability_,
          .wpm = 1'200.0 / dot_ms_, .confidence = confidence_,
          .text = stable_text_, .provisional_text = provisional_text_,
          .pending_elements = elements_};
}

}  // namespace cwassistant::core
