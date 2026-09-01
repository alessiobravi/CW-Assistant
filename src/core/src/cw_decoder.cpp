#include "cwassistant/core/cw_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
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

std::size_t characterEvidenceBytes(
    const std::vector<CwCharacterEvidence>& characters) noexcept {
  std::size_t result = characters.capacity() * sizeof(CwCharacterEvidence);
  for (const auto& character : characters)
    result += character.symbol.capacity();
  return result;
}

std::size_t updateDynamicBytes(const CwDecoderUpdate& update) noexcept {
  return update.text.capacity() + update.provisional_text.capacity() +
         update.pending_elements.capacity() +
         characterEvidenceBytes(update.characters);
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
  timing_quality_sum_ = 0.0F;
  cadence_quality_sum_ = 0.0F;
  character_confidence_sum_ = 0.0F;
  decoded_symbol_count_ = 0;
  unknown_symbol_count_ = 0;
  key_transition_count_ = 0;
  cadence_observation_count_ = 0;
  element_count_ = 0;
  characters_.clear();
  provisional_character_ = {};
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
    if (key_transition_count_ < std::numeric_limits<std::uint32_t>::max())
      ++key_transition_count_;
    if (key_down_) {
      finishElement(duration_ms); changed = true;
    } else {
      if (!elements_.empty() || character_finished_ ||
          decoded_symbol_count_ > 0) {
        const double ratio = duration_ms / dot_ms_;
        const double distance = !elements_.empty()
            ? std::abs(ratio - 1.0)
            : std::min(std::abs(ratio - 3.0) / 1.5,
                       std::abs(ratio - 7.0) / 3.0);
        cadence_quality_sum_ += static_cast<float>(
            std::exp(-1.2 * std::min(distance, 8.0)));
        if (cadence_observation_count_ <
            std::numeric_limits<std::uint32_t>::max()) {
          ++cadence_observation_count_;
        }
      }
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

std::size_t CwTimingDecoder::stateBytes() const noexcept {
  return sizeof(*this) + stable_text_.capacity() +
         provisional_text_.capacity() + elements_.capacity() +
         characterEvidenceBytes(characters_) +
         provisional_character_.symbol.capacity();
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
  timing_quality_sum_ += confidence_;
  character_confidence_sum_ += confidence_;
  if (decoded_symbol_count_ < std::numeric_limits<std::uint32_t>::max())
    ++decoded_symbol_count_;
  if (provisional_text_ == "?" &&
      unknown_symbol_count_ < std::numeric_limits<std::uint32_t>::max())
    ++unknown_symbol_count_;
  provisional_character_ = {
      .symbol = provisional_text_,
      .confidence = confidence_,
      .timing_quality = confidence_,
      .known = provisional_text_ != "?",
  };
  elements_.clear(); character_finished_ = true;
  element_confidence_sum_ = 0.0F;
  element_count_ = 0;
}

void CwTimingDecoder::promoteProvisional() {
  if (provisional_text_.empty()) return;
  stable_text_ += provisional_text_;
  if (characters_.size() >= 256) characters_.erase(characters_.begin());
  characters_.push_back(provisional_character_);
  if (stable_text_.size() > 4'096)
    stable_text_.erase(0, stable_text_.size() - 4'096);
  provisional_text_.clear();
  provisional_character_ = {};
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
  const float timing_quality = decoded_symbol_count_ == 0
      ? 0.0F
      : timing_quality_sum_ / static_cast<float>(decoded_symbol_count_);
  const float cadence_quality = cadence_observation_count_ == 0
      ? 0.0F
      : cadence_quality_sum_ /
            static_cast<float>(cadence_observation_count_);
  const float mean_character_confidence = decoded_symbol_count_ == 0
      ? 0.0F
      : character_confidence_sum_ /
            static_cast<float>(decoded_symbol_count_);
  return {.changed = changed, .key_down = key_down_,
          .key_down_probability = key_down_probability_,
          .wpm = 1'200.0 / dot_ms_, .confidence = confidence_,
          .text = stable_text_, .provisional_text = provisional_text_,
          .pending_elements = elements_, .timing_quality = timing_quality,
          .cadence_quality = cadence_quality,
          .mean_character_confidence = mean_character_confidence,
          .decoded_symbols = decoded_symbol_count_,
          .unknown_symbols = unknown_symbol_count_,
          .key_transitions = key_transition_count_,
          .cadence_observations = cadence_observation_count_,
          .characters = characters_};
}

CwMultiSpeedDecoder::Hypothesis::Hypothesis(
    const double speed_wpm, CwDecoderConfig config)
    : seed_wpm(speed_wpm),
      decoder([&] {
        config.initial_wpm = speed_wpm;
        return config;
      }()) {}

CwMultiSpeedDecoder::CwMultiSpeedDecoder(
    CwDecoderConfig decoder_config, CwMultiSpeedConfig config)
    : decoder_config_(decoder_config), config_(config) {
  config_.preferred_wpm = std::clamp(config_.preferred_wpm, 5.0, 80.0);
  config_.minimum_acquisition_ms =
      std::clamp(config_.minimum_acquisition_ms, 100.0, 5'000.0);
  config_.reacquire_after_silence_ms =
      std::clamp(config_.reacquire_after_silence_ms, 500.0, 15'000.0);
  config_.lock_after_symbols =
      std::clamp<std::uint8_t>(config_.lock_after_symbols, 1, 8);
  config_.lock_score_margin =
      std::clamp(config_.lock_score_margin, 0.0F, 1.0F);
  reset();
}

void CwMultiSpeedDecoder::reset() {
  committed_prefix_.clear();
  resetHypotheses();
}

void CwMultiSpeedDecoder::resetHypotheses() {
  static constexpr double speeds[]{8.0, 12.0, 16.0, 20.0, 25.0,
                                   32.0, 40.0, 50.0, 60.0};
  hypotheses_.clear();
  hypotheses_.reserve(std::size(speeds));
  for (const double speed : speeds)
    hypotheses_.emplace_back(speed, decoder_config_);
  leader_index_ = 3;
  locked_index_ = 0;
  first_timestamp_ns_ = 0;
  last_signal_timestamp_ns_ = 0;
  locked_ = false;
  initialized_ = false;
  signal_seen_ = false;
}

CwDecoderUpdate CwMultiSpeedDecoder::process(
    const std::uint64_t timestamp_ns, const float snr_db) {
  if (snr_db >= decoder_config_.key_off_snr_db) {
    last_signal_timestamp_ns_ = timestamp_ns;
    signal_seen_ = true;
  }
  if (!initialized_ && snr_db >= decoder_config_.key_on_snr_db) {
    first_timestamp_ns_ = timestamp_ns;
    initialized_ = true;
  }
  if (locked_) {
    auto& selected = hypotheses_[locked_index_];
    selected.update = selected.decoder.process(timestamp_ns, snr_db);
    leader_index_ = locked_index_;
    const double silence_ms = signal_seen_ &&
            timestamp_ns >= last_signal_timestamp_ns_
        ? milliseconds(timestamp_ns - last_signal_timestamp_ns_)
        : 0.0;
    if (!selected.update.key_down && selected.update.decoded_symbols > 0 &&
        silence_ms >= config_.reacquire_after_silence_ms) {
      committed_prefix_ += selected.update.text;
      if (!committed_prefix_.empty() && committed_prefix_.back() != ' ')
        committed_prefix_.push_back(' ');
      if (committed_prefix_.size() > 4'096)
        committed_prefix_.erase(0, committed_prefix_.size() - 4'096);
      resetHypotheses();
      return snapshot(true);
    }
    return snapshot(selected.update.changed);
  }

  bool changed = false;
  for (auto& hypothesis : hypotheses_) {
    hypothesis.update = hypothesis.decoder.process(timestamp_ns, snr_db);
    changed = changed || hypothesis.update.changed;
  }
  const std::size_t previous_leader = leader_index_;
  float margin = 0.0F;
  leader_index_ = selectLeader(&margin);
  changed = changed || leader_index_ != previous_leader;
  const double observed_ms = timestamp_ns >= first_timestamp_ns_
      ? milliseconds(timestamp_ns - first_timestamp_ns_)
      : 0.0;
  if (observed_ms >= config_.minimum_acquisition_ms) considerLock(margin);
  return snapshot(changed || locked_);
}

CwDecoderUpdate CwMultiSpeedDecoder::flush(
    const std::uint64_t timestamp_ns) {
  if (locked_) {
    auto& selected = hypotheses_[locked_index_];
    selected.update = selected.decoder.flush(timestamp_ns);
    leader_index_ = locked_index_;
    return snapshot(true);
  }
  for (auto& hypothesis : hypotheses_)
    hypothesis.update = hypothesis.decoder.flush(timestamp_ns);
  leader_index_ = selectLeader();
  locked_index_ = leader_index_;
  locked_ = true;
  return snapshot(true);
}

std::size_t CwMultiSpeedDecoder::hypothesisCount() const noexcept {
  return hypotheses_.size();
}

std::size_t CwMultiSpeedDecoder::stateBytes() const noexcept {
  std::size_t result = sizeof(*this) +
      hypotheses_.capacity() * sizeof(Hypothesis) +
      committed_prefix_.capacity();
  for (const auto& hypothesis : hypotheses_) {
    result += hypothesis.decoder.stateBytes() - sizeof(CwTimingDecoder);
    result += updateDynamicBytes(hypothesis.update);
  }
  return result;
}

float CwMultiSpeedDecoder::score(
    const Hypothesis& hypothesis) const noexcept {
  const auto& update = hypothesis.update;
  const float prior_distance = static_cast<float>(std::abs(
      std::log2(hypothesis.seed_wpm / config_.preferred_wpm)));
  if (update.decoded_symbols == 0)
    return -0.20F * prior_distance;
  const float known_fraction = 1.0F -
      static_cast<float>(update.unknown_symbols) /
          static_cast<float>(update.decoded_symbols);
  return 2.5F * update.timing_quality + 0.4F * known_fraction -
         0.15F * (1.0F - update.confidence) -
         0.20F * prior_distance;
}

std::size_t CwMultiSpeedDecoder::selectLeader(float* margin) const {
  std::size_t best_index = 0;
  float best_score = score(hypotheses_.front());
  float second_score = -1'000.0F;
  for (std::size_t index = 1; index < hypotheses_.size(); ++index) {
    const float candidate_score = score(hypotheses_[index]);
    if (candidate_score > best_score) {
      second_score = best_score;
      best_score = candidate_score;
      best_index = index;
    } else {
      second_score = std::max(second_score, candidate_score);
    }
  }
  if (margin != nullptr) *margin = best_score - second_score;
  return best_index;
}

CwDecoderUpdate CwMultiSpeedDecoder::snapshot(const bool changed) const {
  CwDecoderUpdate result = hypotheses_[leader_index_].update;
  result.changed = changed;
  if (!locked_) {
    result.provisional_text = result.text + result.provisional_text;
    result.text = committed_prefix_;
  } else {
    result.text = committed_prefix_ + result.text;
  }
  return result;
}

void CwMultiSpeedDecoder::considerLock(const float margin) {
  const auto& leader = hypotheses_[leader_index_].update;
  if (leader.decoded_symbols < config_.lock_after_symbols) return;
  if (margin < config_.lock_score_margin &&
      leader.decoded_symbols <
          static_cast<std::uint32_t>(config_.lock_after_symbols) + 3U) {
    return;
  }
  locked_index_ = leader_index_;
  locked_ = true;
}

}  // namespace cwassistant::core
