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
  std::size_t result = update.text.capacity() +
                       update.provisional_text.capacity() +
                       update.pending_elements.capacity() +
                       update.refined_text.capacity() +
                       characterEvidenceBytes(update.characters) +
                       update.acoustic_alternatives.capacity() *
                           sizeof(CwAcousticAlternative);
  for (const auto& alternative : update.acoustic_alternatives) {
    result += alternative.text.capacity() +
              alternative.provisional_elements.capacity();
  }
  return result;
}

constexpr std::size_t kRecentCharacterWindow = 32;
constexpr std::size_t kRecentCadenceWindow = 64;

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
  timing_confidence_sum_ = 0.0F;
  mark_probability_sum_ = 0.0F; mark_probability_duration_ms_ = 0.0;
  decoded_symbol_count_ = 0;
  unknown_symbol_count_ = 0;
  key_transition_count_ = 0;
  cadence_observation_count_ = 0;
  element_count_ = 0;
  characters_.clear();
  recent_cadence_quality_.clear();
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
        const float cadence_quality = static_cast<float>(
            std::exp(-1.2 * std::min(distance, 8.0)));
        if (recent_cadence_quality_.size() >= kRecentCadenceWindow) {
          recent_cadence_quality_.erase(recent_cadence_quality_.begin());
        }
        recent_cadence_quality_.push_back(cadence_quality);
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
  if (key_down_) {
    // Flush is an explicit end-of-input boundary, so it must release a keyed
    // state even when called only one evidence interval after the last update
    // and the probability smoother has not naturally crossed key-off yet.
    const double duration_ms = timestamp_ns >= state_started_ns_
        ? milliseconds(timestamp_ns - state_started_ns_)
        : 0.0;
    if (key_transition_count_ < std::numeric_limits<std::uint32_t>::max())
      ++key_transition_count_;
    finishElement(duration_ms);
    key_down_ = false;
    key_down_probability_ = 0.0F;
    state_started_ns_ = timestamp_ns;
    last_timestamp_ns_ = timestamp_ns;
    result = snapshot(true);
  }
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
         recent_cadence_quality_.capacity() * sizeof(float) +
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
  timing_confidence_sum_ += timing_confidence;
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
  // Pure duration-ratio precision, independent of confidence_'s blended
  // amplitude/keying-probability component: two tracks with identical
  // cadence precision but different SNR must not report identical
  // "timing quality" just because they report identical character
  // confidence -- that collapses two lines of verification evidence into
  // one (see BACKLOG.md CW-001's known-defect note).
  float character_timing_quality = element_count_ == 0
      ? 0.0F
      : std::clamp(timing_confidence_sum_ /
                       static_cast<float>(element_count_),
                   0.0F, 1.0F);
  if (provisional_text_ == "?") {
    confidence_ *= 0.35F;
    character_timing_quality *= 0.35F;
  }
  if (decoded_symbol_count_ < std::numeric_limits<std::uint32_t>::max())
    ++decoded_symbol_count_;
  if (provisional_text_ == "?" &&
      unknown_symbol_count_ < std::numeric_limits<std::uint32_t>::max())
    ++unknown_symbol_count_;
  provisional_character_ = {
      .symbol = provisional_text_,
      .confidence = confidence_,
      .timing_quality = character_timing_quality,
      .known = provisional_text_ != "?",
  };
  elements_.clear(); character_finished_ = true;
  element_confidence_sum_ = 0.0F;
  timing_confidence_sum_ = 0.0F;
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
  const std::size_t character_count = std::min(
      characters_.size(), kRecentCharacterWindow);
  const auto character_begin = characters_.end() -
      static_cast<std::ptrdiff_t>(character_count);
  float recent_timing_sum = 0.0F;
  float recent_confidence_sum = 0.0F;
  std::uint32_t recent_unknown = 0;
  for (auto character = character_begin; character != characters_.end();
       ++character) {
    recent_timing_sum += character->timing_quality;
    recent_confidence_sum += character->confidence;
    if (!character->known) ++recent_unknown;
  }
  // Include the current provisional character in the live quality metrics.
  // It is not counted as recent decoded evidence until it is promoted.
  const bool has_provisional = !provisional_character_.symbol.empty();
  const float quality_count = static_cast<float>(
      character_count + (has_provisional ? 1U : 0U));
  if (has_provisional) {
    recent_timing_sum += provisional_character_.timing_quality;
    recent_confidence_sum += provisional_character_.confidence;
  }
  const float timing_quality = quality_count == 0.0F
      ? 0.0F : recent_timing_sum / quality_count;
  const float mean_character_confidence = quality_count == 0.0F
      ? 0.0F : recent_confidence_sum / quality_count;
  float recent_cadence_sum = 0.0F;
  for (const float quality : recent_cadence_quality_)
    recent_cadence_sum += quality;
  const float cadence_quality = recent_cadence_quality_.empty()
      ? 0.0F
      : recent_cadence_sum /
            static_cast<float>(recent_cadence_quality_.size());
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
          .characters = characters_,
          .refined_text = {},
          .acoustic_alternatives = {},
          .recent_decoded_symbols =
              static_cast<std::uint32_t>(character_count),
          .recent_unknown_symbols = recent_unknown,
          .recent_cadence_observations = static_cast<std::uint32_t>(
              recent_cadence_quality_.size())};
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
  config_.lattice_checkpoint_ms = std::clamp(
      std::isfinite(config_.lattice_checkpoint_ms)
          ? config_.lattice_checkpoint_ms : 500.0,
      100.0, 2'000.0);
  config_.lattice_competitive_cost_margin = std::clamp(
      std::isfinite(config_.lattice_competitive_cost_margin)
          ? config_.lattice_competitive_cost_margin : 1.0,
      0.10, 5.0);
  config_.minimum_lattice_evidence_confidence = std::clamp(
      std::isfinite(config_.minimum_lattice_evidence_confidence)
          ? config_.minimum_lattice_evidence_confidence : 0.40F,
      0.20F, 0.90F);
  reset();
}

void CwMultiSpeedDecoder::reset() {
  committed_prefix_.clear();
  refined_text_.clear();
  lattice_committed_observation_id_ = 0;
  resetLatticeSegment();
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
  recent_mark_count_ = 0;
  recent_gap_count_ = 0;
  recent_mark_index_ = 0;
  recent_gap_index_ = 0;
  cadence_state_started_ns_ = 0;
  cadence_dot_ms_ = 0.0;
  cadence_confidence_ = 0.0F;
  cadence_initialized_ = false;
  cadence_key_down_ = false;
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
    bool changed = false;
    for (auto& hypothesis : hypotheses_) {
      hypothesis.update = hypothesis.decoder.process(timestamp_ns, snr_db);
      changed = changed || hypothesis.update.changed;
    }
    observeCadence(hypotheses_.front().update.key_down, timestamp_ns);
    observeLattice(hypotheses_.front().update.key_down,
                   hypotheses_.front().update.key_down_probability,
                   timestamp_ns);
    const auto& selected = hypotheses_[locked_index_];
    leader_index_ = locked_index_;
    const double silence_ms = signal_seen_ &&
            timestamp_ns >= last_signal_timestamp_ns_
        ? milliseconds(timestamp_ns - last_signal_timestamp_ns_)
        : 0.0;
    if (!selected.update.key_down && selected.update.decoded_symbols > 0 &&
        silence_ms >= config_.reacquire_after_silence_ms) {
      // All fixed speed anchors continue observing after the initial
      // selection. At a safe segment boundary, commit the best complete
      // hypothesis rather than making the early WPM choice irreversible.
      const std::size_t final_leader = selectLeader();
      committed_prefix_ += hypotheses_[final_leader].update.text;
      if (!committed_prefix_.empty() && committed_prefix_.back() != ' ')
        committed_prefix_.push_back(' ');
      if (committed_prefix_.size() > 4'096)
        committed_prefix_.erase(0, committed_prefix_.size() - 4'096);
      refreshLattice(CwLatticeDecodeMode::Flush);
      if (!refined_text_.empty() && refined_text_.back() != ' ')
        refined_text_.push_back(' ');
      resetLatticeSegment();
      resetHypotheses();
      return snapshot(true);
    }
    return snapshot(changed);
  }

  bool changed = false;
  for (auto& hypothesis : hypotheses_) {
    hypothesis.update = hypothesis.decoder.process(timestamp_ns, snr_db);
    changed = changed || hypothesis.update.changed;
  }
  observeCadence(hypotheses_.front().update.key_down, timestamp_ns);
  observeLattice(hypotheses_.front().update.key_down,
                 hypotheses_.front().update.key_down_probability,
                 timestamp_ns);
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
  for (auto& hypothesis : hypotheses_)
    hypothesis.update = hypothesis.decoder.flush(timestamp_ns);
  leader_index_ = selectLeader();
  locked_index_ = leader_index_;
  locked_ = true;
  observeLattice(false, 0.0F, timestamp_ns);
  refreshLattice(CwLatticeDecodeMode::Flush);
  // Flush is an explicit end-of-segment boundary. Preserve that fact in the
  // append-only refined transcript so a callsign ending the transmission is
  // complete even when no following mark arrived to classify the final gap.
  if (!refined_text_.empty() && refined_text_.back() != ' ')
    refined_text_.push_back(' ');
  return snapshot(true);
}

std::size_t CwMultiSpeedDecoder::hypothesisCount() const noexcept {
  return hypotheses_.size();
}

std::size_t CwMultiSpeedDecoder::stateBytes() const noexcept {
  std::size_t result = sizeof(*this) +
      hypotheses_.capacity() * sizeof(Hypothesis) +
      committed_prefix_.capacity() + refined_text_.capacity() +
      acoustic_alternatives_.capacity() * sizeof(CwAcousticAlternative) +
      event_lattice_.stateBytes() - sizeof(CwEventLattice);
  for (const auto& alternative : acoustic_alternatives_) {
    result += alternative.text.capacity() +
              alternative.provisional_elements.capacity();
  }
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
  const std::uint32_t recent_symbols = update.recent_decoded_symbols > 0
      ? update.recent_decoded_symbols : update.decoded_symbols;
  const std::uint32_t recent_unknown = update.recent_decoded_symbols > 0
      ? update.recent_unknown_symbols : update.unknown_symbols;
  const float known_fraction = 1.0F -
      static_cast<float>(std::min(recent_unknown, recent_symbols)) /
          static_cast<float>(std::max<std::uint32_t>(recent_symbols, 1U));
  // Hypothesis selection deliberately scores on mean_character_confidence
  // (the blended amplitude/keying-probability and timing signal), not the
  // now-independent pure-cadence timing_quality: this preserves the
  // original, already-tuned hypothesis-selection behavior, since
  // timing_quality's separation (see BACKLOG.md CW-001) was made only to
  // stop it duplicating mean_character_confidence for the verification
  // gate, not to change which WPM hypothesis wins here.
  return 2.5F * update.mean_character_confidence + 0.4F * known_fraction -
         0.15F * (1.0F - update.confidence) -
         0.20F * prior_distance;
}

void CwMultiSpeedDecoder::observeCadence(const bool key_down,
                                         const std::uint64_t timestamp_ns) {
  if (!cadence_initialized_) {
    cadence_initialized_ = true;
    cadence_key_down_ = key_down;
    cadence_state_started_ns_ = timestamp_ns;
    return;
  }
  if (timestamp_ns < cadence_state_started_ns_) {
    cadence_initialized_ = false;
    cadence_confidence_ = 0.0F;
    return;
  }
  if (key_down == cadence_key_down_) return;

  const double duration_ms = milliseconds(timestamp_ns -
                                           cadence_state_started_ns_);
  if (duration_ms >= 10.0 && duration_ms <= 2'000.0) {
    if (cadence_key_down_) {
      recent_mark_ms_[recent_mark_index_] = duration_ms;
      recent_mark_index_ =
          (recent_mark_index_ + 1U) % kCadenceDurationWindow;
      recent_mark_count_ = std::min(recent_mark_count_ + 1U,
                                    kCadenceDurationWindow);
    } else {
      recent_gap_ms_[recent_gap_index_] = duration_ms;
      recent_gap_index_ =
          (recent_gap_index_ + 1U) % kCadenceDurationWindow;
      recent_gap_count_ = std::min(recent_gap_count_ + 1U,
                                   kCadenceDurationWindow);
    }
    recomputeCadenceEstimate();
  }
  cadence_key_down_ = key_down;
  cadence_state_started_ns_ = timestamp_ns;
}

void CwMultiSpeedDecoder::recomputeCadenceEstimate() {
  const std::size_t observation_count = recent_mark_count_ +
                                        recent_gap_count_;
  if (recent_mark_count_ < 3U || observation_count < 6U) {
    cadence_confidence_ = 0.0F;
    return;
  }

  std::array<double, kCadenceDurationWindow * 5U> candidates{};
  std::size_t candidate_count = 0;
  const auto add_candidate = [&](const double value) {
    if (value >= 15.0 && value <= 240.0 &&
        candidate_count < candidates.size()) {
      candidates[candidate_count++] = value;
    }
  };
  for (std::size_t index = 0; index < recent_mark_count_; ++index) {
    add_candidate(recent_mark_ms_[index]);
    add_candidate(recent_mark_ms_[index] / 3.0);
  }
  for (std::size_t index = 0; index < recent_gap_count_; ++index) {
    add_candidate(recent_gap_ms_[index]);
    add_candidate(recent_gap_ms_[index] / 3.0);
    add_candidate(recent_gap_ms_[index] / 7.0);
  }
  if (candidate_count == 0) return;

  const auto mark_residual = [](const double duration,
                                const double dot) {
    const double ratio = duration / dot;
    return std::min(std::abs(ratio - 1.0) / 0.40,
                    std::abs(ratio - 3.0) / 0.85);
  };
  const auto gap_residual = [](const double duration,
                               const double dot) {
    const double ratio = duration / dot;
    return std::min({std::abs(ratio - 1.0) / 0.45,
                     std::abs(ratio - 3.0) / 1.0,
                     std::abs(ratio - 7.0) / 2.2});
  };

  double best_dot = candidates[0];
  double best_cost = std::numeric_limits<double>::max();
  for (std::size_t candidate = 0; candidate < candidate_count; ++candidate) {
    const double dot = candidates[candidate];
    double cost = 0.0;
    for (std::size_t index = 0; index < recent_mark_count_; ++index) {
      cost += std::min(mark_residual(recent_mark_ms_[index], dot), 1.0);
    }
    for (std::size_t index = 0; index < recent_gap_count_; ++index) {
      cost += std::min(gap_residual(recent_gap_ms_[index], dot), 1.0);
    }
    // Clipping every observation bounds the influence of key clicks and
    // missed edges without sorting inside the per-track real-time path.
    cost /= static_cast<double>(observation_count);
    // Only resolve otherwise-near ties toward the normal operating range.
    // The measured ratios, not this weak prior, remain decisive.
    cost += 0.015 * std::abs(std::log2(dot / 60.0));
    if (cost < best_cost) {
      best_cost = cost;
      best_dot = dot;
    }
  }

  const float coverage = std::min(
      1.0F, static_cast<float>(observation_count) / 18.0F);
  const float fit = static_cast<float>(std::clamp(1.0 - best_cost,
                                                  0.0, 1.0));
  const float confidence = coverage * fit;
  if (cadence_dot_ms_ <= 0.0 || cadence_confidence_ < 0.25F) {
    cadence_dot_ms_ = best_dot;
  } else if (best_dot / cadence_dot_ms_ >= 0.55 &&
             best_dot / cadence_dot_ms_ <= 1.8) {
    cadence_dot_ms_ += 0.25 * (best_dot - cadence_dot_ms_);
  } else if (confidence >= 0.75F) {
    cadence_dot_ms_ = best_dot;
  }
  cadence_confidence_ = confidence;
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
  result.acoustic_wpm = cadence_dot_ms_ > 0.0
      ? 1'200.0 / cadence_dot_ms_ : 0.0;
  result.acoustic_cadence_confidence = cadence_confidence_;
  result.refined_text = refined_text_;
  result.acoustic_alternatives = acoustic_alternatives_;
  if (!locked_) {
    result.provisional_text = result.text + result.provisional_text;
    result.text = committed_prefix_;
  } else {
    result.text = committed_prefix_ + result.text;
  }
  return result;
}

void CwMultiSpeedDecoder::observeLattice(
    const bool key_down, const float key_down_probability,
    const std::uint64_t timestamp_ns) {
  const float probability = std::clamp(key_down_probability, 0.0F, 1.0F);
  if (!lattice_initialized_) {
    lattice_initialized_ = true;
    lattice_key_down_ = key_down;
    lattice_state_started_ns_ = timestamp_ns;
    lattice_last_timestamp_ns_ = timestamp_ns;
    return;
  }
  if (timestamp_ns < lattice_last_timestamp_ns_ ||
      timestamp_ns < lattice_state_started_ns_) {
    resetLatticeSegment();
    lattice_initialized_ = true;
    lattice_key_down_ = key_down;
    lattice_state_started_ns_ = timestamp_ns;
    lattice_last_timestamp_ns_ = timestamp_ns;
    return;
  }

  const double elapsed_ms = milliseconds(timestamp_ns -
                                          lattice_last_timestamp_ns_);
  const float state_confidence = lattice_key_down_ ? probability
                                                    : 1.0F - probability;
  lattice_confidence_sum_ += static_cast<double>(state_confidence) *
                             elapsed_ms;
  lattice_confidence_duration_ms_ += elapsed_ms;
  lattice_last_timestamp_ns_ = timestamp_ns;
  if (key_down == lattice_key_down_) return;

  const double duration_ms = milliseconds(timestamp_ns -
                                           lattice_state_started_ns_);
  const float confidence = lattice_confidence_duration_ms_ > 0.0
      ? static_cast<float>(std::clamp(
            lattice_confidence_sum_ / lattice_confidence_duration_ms_,
            0.0, 1.0))
      : state_confidence;
  const bool completed_gap = !lattice_key_down_;
  // Ignore leading silence. The lattice starts with the first physical mark,
  // so a recording or newly acquired track does not treat arbitrary pre-key
  // time as Morse spacing evidence.
  if (event_lattice_.observationCount() > 0U || lattice_key_down_) {
    static_cast<void>(event_lattice_.append({
        .keyed = lattice_key_down_,
        .duration_ms = duration_ms,
        .confidence = confidence,
        .started_ns = lattice_state_started_ns_,
        .ended_ns = timestamp_ns,
    }));
  }
  lattice_key_down_ = key_down;
  lattice_state_started_ns_ = timestamp_ns;
  lattice_confidence_sum_ = 0.0;
  lattice_confidence_duration_ms_ = 0.0;

  // A completed gap is the only point at which another stable character can
  // exist. Keep this bounded batch calculation off the per-sample path.
  if (completed_gap &&
      (lattice_last_decode_ns_ == 0U ||
       milliseconds(timestamp_ns - lattice_last_decode_ns_) >=
           config_.lattice_checkpoint_ms)) {
    refreshLattice(CwLatticeDecodeMode::Provisional);
    lattice_last_decode_ns_ = timestamp_ns;
  }
}

void CwMultiSpeedDecoder::refreshLattice(const CwLatticeDecodeMode mode) {
  if (event_lattice_.observationCount() == 0U || hypotheses_.empty()) return;
  // Every fixed timing anchor continues running after presentation lock. Use
  // the currently strongest complete acoustic path, not the historical lock,
  // so the refinement path can recover from an early cadence choice without
  // rewriting the primary transcript.
  const std::size_t lattice_leader = selectLeader();
  double candidate_wpm = hypotheses_[lattice_leader].update.wpm;
  const double cadence_wpm = cadence_dot_ms_ > 0.0
      ? 1'200.0 / cadence_dot_ms_ : 0.0;
  const double cadence_ratio = candidate_wpm > 0.0
      ? cadence_wpm / candidate_wpm : 0.0;
  // The independent estimator is deliberately a guard, not a broad override:
  // noisy pileups can produce a confident-looking harmonic fit at roughly
  // twice the selected speed. Use it only when strong evidence agrees with
  // the continuously evaluated timing bank's neighborhood.
  if (cadence_confidence_ >= 0.65F && cadence_wpm > 0.0 &&
      cadence_ratio >= 0.75 && cadence_ratio <= 1.35) {
    candidate_wpm = cadence_wpm;
  }
  if (!std::isfinite(candidate_wpm) || candidate_wpm <= 0.0) return;

  const auto decoded = event_lattice_.decode(1'200.0 / candidate_wpm, mode);
  acoustic_alternatives_.clear();
  if (decoded.alternatives.empty()) return;
  acoustic_alternatives_.reserve(decoded.alternatives.size());
  for (const auto& alternative : decoded.alternatives) {
    std::uint64_t first_id = alternative.provisional_first_observation_id;
    std::uint64_t last_id = alternative.provisional_last_observation_id;
    if (!alternative.symbols.empty()) {
      first_id = alternative.symbols.front().first_observation_id;
      last_id = alternative.symbols.back().last_observation_id;
    }
    last_id = std::max(last_id,
                       alternative.provisional_last_observation_id);
    acoustic_alternatives_.push_back({
        .text = alternative.text(),
        .provisional_elements = alternative.provisional_elements,
        .wpm = candidate_wpm,
        .acoustic_cost = alternative.acoustic_cost,
        .evidence_confidence = alternative.evidence_confidence,
        .first_observation_id = first_id,
        .last_observation_id = last_id,
    });
  }

  const double maximum_cost = decoded.alternatives.front().acoustic_cost +
                              config_.lattice_competitive_cost_margin;
  std::size_t competitive_count = 1U;
  while (competitive_count < decoded.alternatives.size() &&
         decoded.alternatives[competitive_count].acoustic_cost <=
             maximum_cost) {
    ++competitive_count;
  }
  // An early preferred-speed path can be internally self-consistent while it
  // is still the wrong cadence (a slow dit resembles a faster dash). Expose
  // its segment-scoped alternatives, but do not make them append-only until
  // the multi-speed acquisition has settled or an explicit flush closes the
  // segment.
  if ((!locked_ && mode == CwLatticeDecodeMode::Provisional) ||
      decoded.alternatives.front().evidence_confidence <
          config_.minimum_lattice_evidence_confidence) {
    return;
  }

  const auto& best_symbols = decoded.alternatives.front().symbols;
  for (std::size_t symbol_index = 0; symbol_index < best_symbols.size();
       ++symbol_index) {
    const auto& candidate = best_symbols[symbol_index];
    if (candidate.last_observation_id <= lattice_committed_observation_id_) {
      continue;
    }
    // Never let a later best path reinterpret a run that has already crossed
    // the append-only boundary. A newly agreed symbol must begin entirely to
    // the right of the last committed physical observation.
    if (candidate.first_observation_id <=
        lattice_committed_observation_id_) {
      break;
    }
    bool agreed = true;
    for (std::size_t path_index = 1U; path_index < competitive_count;
         ++path_index) {
      const auto& symbols = decoded.alternatives[path_index].symbols;
      if (symbol_index >= symbols.size()) {
        agreed = false;
        break;
      }
      const auto& compared = symbols[symbol_index];
      if (candidate.symbol != compared.symbol ||
          candidate.known != compared.known ||
          candidate.word_boundary_after != compared.word_boundary_after ||
          candidate.first_observation_id != compared.first_observation_id ||
          candidate.last_observation_id != compared.last_observation_id) {
        agreed = false;
        break;
      }
    }
    if (!agreed) break;
    refined_text_ += candidate.known ? candidate.symbol : "?";
    if (candidate.word_boundary_after &&
        (refined_text_.empty() || refined_text_.back() != ' ')) {
      refined_text_.push_back(' ');
    }
    lattice_committed_observation_id_ = candidate.last_observation_id;
    if (refined_text_.size() > 4'096) {
      refined_text_.erase(0, refined_text_.size() - 4'096);
    }
  }
}

void CwMultiSpeedDecoder::resetLatticeSegment() noexcept {
  event_lattice_.reset();
  acoustic_alternatives_.clear();
  lattice_state_started_ns_ = 0;
  lattice_last_timestamp_ns_ = 0;
  lattice_last_decode_ns_ = 0;
  lattice_confidence_sum_ = 0.0;
  lattice_confidence_duration_ms_ = 0.0;
  lattice_initialized_ = false;
  lattice_key_down_ = false;
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
