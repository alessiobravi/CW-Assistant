#include "cwassistant/core/cw_transmit_scheduler.hpp"

#include <limits>

namespace cwassistant::core {

CwTransmitScheduler::CwTransmitScheduler(
    const CwTransmitSchedulerConfig config) noexcept : config_(config) {}

bool CwTransmitScheduler::validConfig() const noexcept {
  return config_.ptt_lead_ns <= kMaximumLeadOrHangNs &&
      config_.ptt_hang_ns <= kMaximumLeadOrHangNs &&
      config_.maximum_message_duration_ns > 0U &&
      config_.maximum_message_duration_ns <=
          kMaximumAllowedMessageDurationNs &&
      config_.maximum_transition_lateness_ns <=
          kMaximumAllowedLatenessNs;
}

bool CwTransmitScheduler::validatePlan(const CwTransmitPlan& plan) const noexcept {
  if (!validConfig() || plan.text.empty() || plan.spans.empty() ||
      plan.dot_duration_ns == 0U || !plan.spans.front().key_down ||
      !plan.spans.back().key_down) {
    return false;
  }

  std::uint64_t total_units = 0U;
  bool previous_key_down = !plan.spans.front().key_down;
  for (const auto& span : plan.spans) {
    if (span.duration_units == 0U || span.key_down == previous_key_down ||
        total_units > std::numeric_limits<std::uint64_t>::max() -
                          span.duration_units) {
      return false;
    }
    if (span.duration_units >
        std::numeric_limits<std::uint64_t>::max() / plan.dot_duration_ns) {
      return false;
    }
    const std::uint64_t span_ns =
        static_cast<std::uint64_t>(span.duration_units) * plan.dot_duration_ns;
    if (span.key_down && span_ns > kMaximumContinuousKeyDownNs) return false;
    total_units += span.duration_units;
    previous_key_down = span.key_down;
  }
  if (total_units >
      std::numeric_limits<std::uint64_t>::max() / plan.dot_duration_ns) {
    return false;
  }
  const std::uint64_t computed_duration = total_units * plan.dot_duration_ns;
  return computed_duration == plan.total_duration_ns &&
      computed_duration <= config_.maximum_message_duration_ns;
}

bool CwTransmitScheduler::setNextTransition(const std::uint64_t base_ns,
                                            const std::uint64_t delay_ns) noexcept {
  if (base_ns > std::numeric_limits<std::uint64_t>::max() - delay_ns) {
    static_cast<void>(tripFault());
    return false;
  }
  next_transition_ns_ = base_ns + delay_ns;
  return true;
}

bool CwTransmitScheduler::start(const CwTransmitPlan& plan,
                                const std::uint64_t now_ns) {
  if (state_ != CwTransmitSchedulerState::Idle || !validatePlan(plan)) {
    static_cast<void>(tripFault());
    return false;
  }
  if (plan.total_duration_ns >
          std::numeric_limits<std::uint64_t>::max() - config_.ptt_lead_ns ||
      plan.total_duration_ns + config_.ptt_lead_ns >
          std::numeric_limits<std::uint64_t>::max() - config_.ptt_hang_ns ||
      now_ns > std::numeric_limits<std::uint64_t>::max() -
                   (plan.total_duration_ns + config_.ptt_lead_ns +
                    config_.ptt_hang_ns)) {
    static_cast<void>(tripFault());
    return false;
  }

  plan_ = plan;
  state_ = CwTransmitSchedulerState::Running;
  phase_ = Phase::Lead;
  lines_ = {.ptt = true, .key = false};
  last_advance_ns_ = now_ns;
  clock_initialized_ = true;
  span_index_ = 0U;
  release_pending_ = false;
  return setNextTransition(now_ns, config_.ptt_lead_ns);
}

void CwTransmitScheduler::enterTerminal(
    const CwTransmitSchedulerState state) noexcept {
  state_ = state;
  phase_ = Phase::None;
  next_transition_ns_ = 0U;
  lines_.key = false;
  release_pending_ = lines_.ptt;
}

CwTransmitAdvanceResult CwTransmitScheduler::tripFault() noexcept {
  enterTerminal(CwTransmitSchedulerState::Fault);
  return CwTransmitAdvanceResult::Faulted;
}

CwTransmitAdvanceResult CwTransmitScheduler::advance(
    const std::uint64_t now_ns) noexcept {
  if (state_ != CwTransmitSchedulerState::Running) {
    if (release_pending_) {
      lines_.ptt = false;
      release_pending_ = false;
      return CwTransmitAdvanceResult::Transition;
    }
    return CwTransmitAdvanceResult::NoChange;
  }
  if (!clock_initialized_ || now_ns < last_advance_ns_) return tripFault();
  last_advance_ns_ = now_ns;
  if (now_ns < next_transition_ns_) return CwTransmitAdvanceResult::NoChange;
  if (now_ns - next_transition_ns_ >
      config_.maximum_transition_lateness_ns) {
    return tripFault();
  }

  const std::uint64_t boundary_ns = next_transition_ns_;
  if (phase_ == Phase::Lead) {
    phase_ = Phase::Spans;
    span_index_ = 0U;
    lines_.key = true;
    const auto& span = plan_->spans[span_index_];
    if (!setNextTransition(
            boundary_ns,
            static_cast<std::uint64_t>(span.duration_units) *
                plan_->dot_duration_ns)) {
      return CwTransmitAdvanceResult::Faulted;
    }
    return CwTransmitAdvanceResult::Transition;
  }

  if (phase_ == Phase::Spans) {
    ++span_index_;
    if (span_index_ < plan_->spans.size()) {
      const auto& span = plan_->spans[span_index_];
      lines_.key = span.key_down;
      if (!setNextTransition(
              boundary_ns,
              static_cast<std::uint64_t>(span.duration_units) *
                  plan_->dot_duration_ns)) {
        return CwTransmitAdvanceResult::Faulted;
      }
      return CwTransmitAdvanceResult::Transition;
    }
    // A valid plan ends with a keyed span. Expose its release separately from
    // the later PTT release, including when the configured hang is zero.
    lines_.key = false;
    phase_ = Phase::Hang;
    if (!setNextTransition(boundary_ns, config_.ptt_hang_ns)) {
      return CwTransmitAdvanceResult::Faulted;
    }
    return CwTransmitAdvanceResult::Transition;
  }

  if (phase_ == Phase::Hang) {
    lines_.ptt = false;
    state_ = CwTransmitSchedulerState::Completed;
    phase_ = Phase::None;
    next_transition_ns_ = 0U;
    return CwTransmitAdvanceResult::Transition;
  }
  return tripFault();
}

bool CwTransmitScheduler::cancel() noexcept {
  if (state_ != CwTransmitSchedulerState::Running) return false;
  enterTerminal(CwTransmitSchedulerState::Cancelled);
  return true;
}

void CwTransmitScheduler::emergencyRelease() noexcept {
  enterTerminal(CwTransmitSchedulerState::Emergency);
}

bool CwTransmitScheduler::reset() noexcept {
  if (state_ == CwTransmitSchedulerState::Running || lines_.ptt || lines_.key ||
      release_pending_) {
    return false;
  }
  plan_.reset();
  state_ = CwTransmitSchedulerState::Idle;
  phase_ = Phase::None;
  last_advance_ns_ = 0U;
  next_transition_ns_ = 0U;
  span_index_ = 0U;
  clock_initialized_ = false;
  return true;
}

CwTransmitSchedulerSnapshot CwTransmitScheduler::snapshot() const noexcept {
  return {.state = state_,
          .lines = lines_,
          .next_transition_ns = next_transition_ns_,
          .active_span = span_index_,
          .release_pending = release_pending_};
}

const CwTransmitPlan* CwTransmitScheduler::plan() const noexcept {
  return plan_ ? &*plan_ : nullptr;
}

}  // namespace cwassistant::core
