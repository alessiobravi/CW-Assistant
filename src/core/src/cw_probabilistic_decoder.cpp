#include "cwassistant/core/cw_probabilistic_decoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace cwassistant::core {
namespace {

double milliseconds(const std::uint64_t nanoseconds) noexcept {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

struct Candidate {
  CwLatticeAlternative alternative;
  double wpm{0.0};
  double cost{0.0};
};

bool sameSymbol(const CwLatticeSymbol& left,
                const CwLatticeSymbol& right) noexcept {
  return left.symbol == right.symbol && left.elements == right.elements &&
         left.first_observation_id == right.first_observation_id &&
         left.last_observation_id == right.last_observation_id &&
         left.known == right.known &&
         left.word_boundary_after == right.word_boundary_after;
}

bool sameCandidate(const Candidate& left, const Candidate& right) {
  if (left.alternative.provisional_elements !=
          right.alternative.provisional_elements ||
      left.alternative.symbols.size() != right.alternative.symbols.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.alternative.symbols.size();
       ++index) {
    if (!sameSymbol(left.alternative.symbols[index],
                    right.alternative.symbols[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace

CwProbabilisticMorseDecoder::CwProbabilisticMorseDecoder(
    CwProbabilisticDecoderConfig config)
    : config_(std::move(config)), lattice_(config_.lattice) {
  const auto finite = [](const double value, const double fallback) {
    return std::isfinite(value) ? value : fallback;
  };
  config_.key_on_probability = std::clamp(
      std::isfinite(config_.key_on_probability)
          ? config_.key_on_probability : 0.65F,
      0.51F, 0.95F);
  config_.key_off_probability = std::clamp(
      std::isfinite(config_.key_off_probability)
          ? config_.key_off_probability : 0.35F,
      0.05F, config_.key_on_probability - 0.05F);
  config_.minimum_transition_ms = std::clamp(
      finite(config_.minimum_transition_ms, 8.0), 0.0, 15.0);
  config_.minimum_wpm = std::clamp(
      finite(config_.minimum_wpm, 8.0), 5.0, 60.0);
  config_.maximum_wpm = std::clamp(
      finite(config_.maximum_wpm, 60.0), config_.minimum_wpm, 80.0);
  config_.initial_wpm = std::clamp(
      finite(config_.initial_wpm, 20.0), config_.minimum_wpm,
      config_.maximum_wpm);
  config_.checkpoint_ms = std::clamp(
      finite(config_.checkpoint_ms, 250.0), 50.0, 2'000.0);
  config_.competitive_cost_margin = std::clamp(
      finite(config_.competitive_cost_margin, 1.0), 0.1, 5.0);
  config_.minimum_stable_evidence_confidence = std::clamp(
      std::isfinite(config_.minimum_stable_evidence_confidence)
          ? config_.minimum_stable_evidence_confidence : 0.40F,
      0.20F, 0.90F);
  config_.maximum_alternatives = std::clamp<std::size_t>(
      config_.maximum_alternatives, 2U, 16U);
  config_.maximum_stable_characters = std::clamp<std::size_t>(
      config_.maximum_stable_characters, 256U, 16'384U);
  estimated_dot_ms_ = 1'200.0 / config_.initial_wpm;
}

void CwProbabilisticMorseDecoder::reset() noexcept {
  lattice_.reset();
  stable_text_.clear();
  provisional_text_.clear();
  provisional_elements_.clear();
  alternatives_.clear();
  state_started_ns_ = 0;
  last_timestamp_ns_ = 0;
  last_decode_ns_ = 0;
  committed_observation_id_ = 0;
  state_probability_sum_ = 0.0;
  state_probability_duration_ms_ = 0.0;
  estimated_dot_ms_ = 1'200.0 / config_.initial_wpm;
  evidence_confidence_ = 0.0F;
  initialized_ = false;
  key_down_ = false;
  pending_transition_ = false;
  pending_key_down_ = false;
  pending_transition_started_ns_ = 0;
  pending_current_probability_sum_ = 0.0;
  pending_new_probability_sum_ = 0.0;
  pending_probability_duration_ms_ = 0.0;
}

CwProbabilisticDecoderUpdate CwProbabilisticMorseDecoder::process(
    CwProbabilityObservation observation) {
  if (!std::isfinite(observation.key_down_probability))
    observation.key_down_probability = 0.5F;
  observation.key_down_probability = std::clamp(
      observation.key_down_probability, 0.0F, 1.0F);
  if (!initialized_) {
    initialized_ = true;
    // An isolated first-frame spike is not enough to establish key-down.
    // Start from the radio-safe idle state and let the normal debounce prove
    // the first mark.
    key_down_ = false;
    state_started_ns_ = observation.timestamp_ns;
    last_timestamp_ns_ = observation.timestamp_ns;
    return snapshot(false);
  }
  if (observation.timestamp_ns < last_timestamp_ns_) {
    reset();
    initialized_ = true;
    key_down_ = false;
    state_started_ns_ = observation.timestamp_ns;
    last_timestamp_ns_ = observation.timestamp_ns;
    return snapshot(true);
  }

  const std::uint64_t interval_started_ns = last_timestamp_ns_;
  const double elapsed_ms = milliseconds(observation.timestamp_ns -
                                         interval_started_ns);
  const double probability_for_state = key_down_
      ? observation.key_down_probability
      : 1.0F - observation.key_down_probability;
  state_probability_sum_ += probability_for_state * elapsed_ms;
  state_probability_duration_ms_ += elapsed_ms;
  last_timestamp_ns_ = observation.timestamp_ns;
  const bool observed_down = key_down_
      ? observation.key_down_probability >= config_.key_off_probability
      : observation.key_down_probability >= config_.key_on_probability;

  if (observed_down != key_down_) {
    if (!pending_transition_ || pending_key_down_ != observed_down) {
      pending_transition_ = true;
      pending_key_down_ = observed_down;
      pending_transition_started_ns_ = interval_started_ns;
      pending_current_probability_sum_ = probability_for_state * elapsed_ms;
      pending_new_probability_sum_ =
          (key_down_ ? 1.0F - observation.key_down_probability
                     : observation.key_down_probability) * elapsed_ms;
      pending_probability_duration_ms_ = elapsed_ms;
    } else {
      pending_current_probability_sum_ += probability_for_state * elapsed_ms;
      pending_new_probability_sum_ +=
          (key_down_ ? 1.0F - observation.key_down_probability
                     : observation.key_down_probability) * elapsed_ms;
      pending_probability_duration_ms_ += elapsed_ms;
    }
    if (pending_probability_duration_ms_ <
        config_.minimum_transition_ms) {
      return snapshot(false);
    }
    const bool completed_gap = !key_down_;
    const double completed_duration_ms = milliseconds(
        pending_transition_started_ns_ - state_started_ns_);
    state_probability_sum_ = std::max(
        0.0, state_probability_sum_ - pending_current_probability_sum_);
    state_probability_duration_ms_ = std::max(
        0.0, state_probability_duration_ms_ -
                 pending_probability_duration_ms_);
    finishRun(pending_transition_started_ns_);
    key_down_ = observed_down;
    state_started_ns_ = pending_transition_started_ns_;
    state_probability_sum_ = pending_new_probability_sum_;
    state_probability_duration_ms_ = pending_probability_duration_ms_;
    pending_transition_ = false;
    pending_current_probability_sum_ = 0.0;
    pending_new_probability_sum_ = 0.0;
    pending_probability_duration_ms_ = 0.0;
    if (completed_gap && completed_duration_ms >= 1.4 * estimated_dot_ms_) {
      last_decode_ns_ = observation.timestamp_ns;
      return decode(false, false);
    }
    return snapshot(true);
  }
  pending_transition_ = false;
  pending_current_probability_sum_ = 0.0;
  pending_new_probability_sum_ = 0.0;
  pending_probability_duration_ms_ = 0.0;

  if (!key_down_ && lattice_.observationCount() > 0U &&
      milliseconds(observation.timestamp_ns - state_started_ns_) >=
          1.4 * estimated_dot_ms_ &&
      (last_decode_ns_ == 0U ||
       milliseconds(observation.timestamp_ns - last_decode_ns_) >=
           config_.checkpoint_ms)) {
    last_decode_ns_ = observation.timestamp_ns;
    return decode(false, true);
  }
  return snapshot(false);
}

CwProbabilisticDecoderUpdate CwProbabilisticMorseDecoder::flush(
    const std::uint64_t timestamp_ns) {
  if (!initialized_) return snapshot(false);
  // A timestamp without a corresponding probability sample is a boundary,
  // not acoustic evidence.  Extending the final run to a later wall-clock
  // flush time would bias the speed search (especially after long silence).
  static_cast<void>(timestamp_ns);
  finishRun(last_timestamp_ns_);
  // Consume the accumulated interval exactly once. A repeated lifecycle flush
  // must not append a duplicate physical run or change the transcript.
  state_started_ns_ = last_timestamp_ns_;
  state_probability_sum_ = 0.0;
  state_probability_duration_ms_ = 0.0;
  pending_transition_ = false;
  pending_current_probability_sum_ = 0.0;
  pending_new_probability_sum_ = 0.0;
  pending_probability_duration_ms_ = 0.0;
  const auto result = decode(true, false);
  return result;
}

void CwProbabilisticMorseDecoder::finishRun(
    const std::uint64_t timestamp_ns) {
  if (timestamp_ns <= state_started_ns_) return;
  const double duration_ms = milliseconds(timestamp_ns - state_started_ns_);
  const float confidence = state_probability_duration_ms_ > 0.0
      ? static_cast<float>(std::clamp(
            state_probability_sum_ / state_probability_duration_ms_,
            0.0, 1.0))
      : 0.0F;
  // Discard leading silence: it is recording context, not Morse spacing.
  if (lattice_.observationCount() > 0U || key_down_) {
    static_cast<void>(lattice_.append({
        .keyed = key_down_,
        .duration_ms = duration_ms,
        .confidence = confidence,
        .started_ns = state_started_ns_,
        .ended_ns = timestamp_ns,
    }));
  }
}

CwProbabilisticDecoderUpdate CwProbabilisticMorseDecoder::decode(
    const bool flush, const bool partial_gap) {
  CwEventLattice working = lattice_;
  if (partial_gap && last_timestamp_ns_ > state_started_ns_) {
    const double duration_ms = milliseconds(last_timestamp_ns_ -
                                             state_started_ns_);
    const float confidence = state_probability_duration_ms_ > 0.0
        ? static_cast<float>(std::clamp(
              state_probability_sum_ / state_probability_duration_ms_,
              0.0, 1.0))
        : 0.0F;
    static_cast<void>(working.append({
        .keyed = false,
        .duration_ms = duration_ms,
        .confidence = confidence,
        .started_ns = state_started_ns_,
        .ended_ns = last_timestamp_ns_,
    }));
  }
  if (working.observationCount() == 0U) return snapshot(false);

  constexpr std::array<double, 9> seed_wpm{
      8.0, 12.0, 16.0, 20.0, 25.0, 32.0, 40.0, 50.0, 60.0};
  std::array<double, 12> candidate_wpm{};
  std::size_t candidate_count = 0;
  const auto add_wpm = [&](const double value) {
    const double bounded = std::clamp(value, config_.minimum_wpm,
                                      config_.maximum_wpm);
    for (std::size_t index = 0; index < candidate_count; ++index) {
      if (std::abs(std::log(candidate_wpm[index] / bounded)) < 0.025) return;
    }
    if (candidate_count < candidate_wpm.size())
      candidate_wpm[candidate_count++] = bounded;
  };
  for (const double value : seed_wpm) add_wpm(value);
  const double estimated_wpm = 1'200.0 / estimated_dot_ms_;
  add_wpm(estimated_wpm * 0.90);
  add_wpm(estimated_wpm);
  add_wpm(estimated_wpm * 1.10);

  std::vector<Candidate> candidates;
  candidates.reserve(candidate_count * config_.lattice.maximum_alternatives);
  for (std::size_t index = 0; index < candidate_count; ++index) {
    const double wpm = candidate_wpm[index];
    auto result = working.decode(
        1'200.0 / wpm, flush ? CwLatticeDecodeMode::Flush
                            : CwLatticeDecodeMode::Provisional);
    const double prior = 0.02 * static_cast<double>(result.observations.size()) *
        std::abs(std::log2(wpm / estimated_wpm));
    for (auto& alternative : result.alternatives) {
      const double cost = alternative.acoustic_cost + prior;
      candidates.push_back({.alternative = std::move(alternative),
                            .wpm = wpm,
                            .cost = cost});
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& left, const Candidate& right) {
                     return left.cost < right.cost;
                   });
  std::vector<Candidate> unique;
  unique.reserve(std::min(candidates.size(), config_.maximum_alternatives));
  for (auto& candidate : candidates) {
    if (std::any_of(unique.cbegin(), unique.cend(),
                    [&](const Candidate& existing) {
                      return sameCandidate(existing, candidate);
                    })) {
      continue;
    }
    unique.push_back(std::move(candidate));
    if (unique.size() >= config_.maximum_alternatives) break;
  }
  if (unique.empty()) return snapshot(false);

  const double selected_dot = 1'200.0 / unique.front().wpm;
  evidence_confidence_ = unique.front().alternative.evidence_confidence;
  if (evidence_confidence_ >= 0.40F &&
      selected_dot / estimated_dot_ms_ >= 0.60 &&
      selected_dot / estimated_dot_ms_ <= 1.60) {
    estimated_dot_ms_ += 0.20 * (selected_dot - estimated_dot_ms_);
  } else if (evidence_confidence_ >= 0.75F) {
    estimated_dot_ms_ = selected_dot;
  }

  alternatives_.clear();
  alternatives_.reserve(unique.size());
  for (const auto& candidate : unique) {
    const auto& alternative = candidate.alternative;
    std::uint64_t first_id = alternative.provisional_first_observation_id;
    std::uint64_t last_id = alternative.provisional_last_observation_id;
    if (!alternative.symbols.empty()) {
      first_id = alternative.symbols.front().first_observation_id;
      last_id = alternative.symbols.back().last_observation_id;
    }
    last_id = std::max(last_id,
                       alternative.provisional_last_observation_id);
    alternatives_.push_back({
        .text = alternative.text(),
        .provisional_elements = alternative.provisional_elements,
        .wpm = candidate.wpm,
        .acoustic_cost = candidate.cost,
        .evidence_confidence = alternative.evidence_confidence,
        .first_observation_id = first_id,
        .last_observation_id = last_id,
    });
  }

  const double competitive_cost = unique.front().cost +
                                  config_.competitive_cost_margin;
  std::size_t competitive_count = 1;
  while (competitive_count < unique.size() &&
         unique[competitive_count].cost <= competitive_cost) {
    ++competitive_count;
  }
  // Flush is an explicit segment-finalization decision by the caller. There
  // is no future acoustic evidence with which to grow the common prefix, so
  // finalize the MAP suffix while still returning the bounded alternatives
  // (and their costs) to expose residual uncertainty.
  if (flush) competitive_count = 1U;
  const auto& best = unique.front().alternative;
  std::size_t stable_limit = best.symbols.size();
  if (partial_gap && stable_limit > 0U) --stable_limit;
  if (evidence_confidence_ >= config_.minimum_stable_evidence_confidence) {
    for (std::size_t symbol_index = 0; symbol_index < stable_limit;
         ++symbol_index) {
      const auto& symbol = best.symbols[symbol_index];
      if (symbol.last_observation_id <= committed_observation_id_) continue;
      if (symbol.first_observation_id <= committed_observation_id_) break;
      bool agreed = true;
      for (std::size_t path_index = 1; path_index < competitive_count;
           ++path_index) {
        const auto& compared = unique[path_index].alternative.symbols;
        if (symbol_index >= compared.size() ||
            !sameSymbol(symbol, compared[symbol_index])) {
          agreed = false;
          break;
        }
      }
      if (!agreed) break;
      const std::string_view emitted = symbol.known
          ? std::string_view(symbol.symbol) : std::string_view("?");
      if (stable_text_.size() + emitted.size() >
          config_.maximum_stable_characters) {
        break;
      }
      stable_text_ += emitted;
      if (symbol.word_boundary_after &&
          stable_text_.size() < config_.maximum_stable_characters &&
          (stable_text_.empty() || stable_text_.back() != ' ')) {
        stable_text_.push_back(' ');
      }
      committed_observation_id_ = symbol.last_observation_id;
    }
  }

  provisional_text_.clear();
  for (const auto& symbol : best.symbols) {
    if (symbol.last_observation_id <= committed_observation_id_) continue;
    if (symbol.first_observation_id <= committed_observation_id_) break;
    provisional_text_ += symbol.known ? symbol.symbol : "?";
    if (symbol.word_boundary_after &&
        (provisional_text_.empty() || provisional_text_.back() != ' ')) {
      provisional_text_.push_back(' ');
    }
  }
  provisional_elements_ = best.provisional_elements;
  if (flush && provisional_text_.empty() &&
      provisional_elements_.empty() && !stable_text_.empty() &&
      stable_text_.back() != ' ') {
    stable_text_.push_back(' ');
  }
  return snapshot(true);
}

CwProbabilisticDecoderUpdate CwProbabilisticMorseDecoder::snapshot(
    const bool changed) const {
  if (!changed) {
    // High-rate probability samples do not allocate or copy transcript/N-best
    // payloads. Consumers retain the most recent changed update.
    return {.changed = false,
            .key_down = key_down_,
            .stable_text = {},
            .provisional_text = {},
            .provisional_elements = {},
            .estimated_wpm = 1'200.0 / estimated_dot_ms_,
            .evidence_confidence = evidence_confidence_,
            .alternatives = {}};
  }
  return {.changed = changed,
          .key_down = key_down_,
          .stable_text = stable_text_,
          .provisional_text = provisional_text_,
          .provisional_elements = provisional_elements_,
          .estimated_wpm = 1'200.0 / estimated_dot_ms_,
          .evidence_confidence = evidence_confidence_,
          .alternatives = alternatives_};
}

std::size_t CwProbabilisticMorseDecoder::stateBytes() const noexcept {
  std::size_t bytes = sizeof(*this) + lattice_.stateBytes() -
      sizeof(CwEventLattice) + stable_text_.capacity() +
      provisional_text_.capacity() + provisional_elements_.capacity() +
      alternatives_.capacity() * sizeof(CwProbabilisticAlternative);
  for (const auto& alternative : alternatives_) {
    bytes += alternative.text.capacity() +
             alternative.provisional_elements.capacity();
  }
  return bytes;
}

}  // namespace cwassistant::core
