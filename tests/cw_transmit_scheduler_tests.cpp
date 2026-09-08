#include <cstdlib>
#include <iostream>
#include <limits>

#include "cwassistant/core/cw_transmit_encoder.hpp"
#include "cwassistant/core/cw_transmit_scheduler.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

using cwassistant::core::CwTransmitAdvanceResult;
using cwassistant::core::CwTransmitEncoder;
using cwassistant::core::CwTransmitScheduler;
using cwassistant::core::CwTransmitSchedulerConfig;
using cwassistant::core::CwTransmitSchedulerState;

void test_exact_sequence_and_immutable_plan() {
  const auto encoded = CwTransmitEncoder::encode("E", 20U);
  expect(encoded.has_value(), "fixture plan encodes");
  if (!encoded) return;
  auto source = *encoded;
  CwTransmitScheduler scheduler({.ptt_lead_ns = 10U,
                                 .ptt_hang_ns = 20U,
                                 .maximum_message_duration_ns = 1'000'000'000U,
                                 .maximum_transition_lateness_ns = 5U});
  expect(scheduler.start(source, 100U), "valid plan starts");
  expect(scheduler.snapshot().lines.ptt && !scheduler.snapshot().lines.key &&
             scheduler.snapshot().next_transition_ns == 110U,
         "start asserts only PTT for the lead interval");
  source.text = "MUTATED";
  source.spans.front().duration_units = 99U;
  expect(scheduler.plan() != nullptr && scheduler.plan()->text == "E" &&
             scheduler.plan()->spans.front().duration_units == 1U,
         "scheduler owns an immutable plan snapshot");
  expect(scheduler.advance(109U) == CwTransmitAdvanceResult::NoChange,
         "advance before a boundary changes nothing");
  expect(scheduler.advance(110U) == CwTransmitAdvanceResult::Transition &&
             scheduler.snapshot().lines.ptt && scheduler.snapshot().lines.key,
         "lead completion asserts KEY while PTT remains asserted");
  const std::uint64_t mark_end = 110U + encoded->dot_duration_ns;
  expect(scheduler.advance(mark_end) == CwTransmitAdvanceResult::Transition &&
             scheduler.snapshot().lines.ptt && !scheduler.snapshot().lines.key &&
             scheduler.snapshot().state == CwTransmitSchedulerState::Running,
         "final mark emits an explicit KEY-off transition before PTT release");
  expect(scheduler.advance(mark_end + 20U) ==
             CwTransmitAdvanceResult::Transition &&
             !scheduler.snapshot().lines.ptt && !scheduler.snapshot().lines.key &&
             scheduler.snapshot().state == CwTransmitSchedulerState::Completed,
         "hang completion releases PTT and completes the plan");
}

void test_zero_delays_still_order_release() {
  const auto plan = CwTransmitEncoder::encode("E", 80U);
  CwTransmitScheduler scheduler({.ptt_lead_ns = 0U,
                                 .ptt_hang_ns = 0U,
                                 .maximum_message_duration_ns = 1'000'000'000U,
                                 .maximum_transition_lateness_ns = 0U});
  expect(plan && scheduler.start(*plan, 7U), "zero-delay plan starts");
  expect(scheduler.advance(7U) == CwTransmitAdvanceResult::Transition &&
             scheduler.snapshot().lines.key,
         "zero lead still exposes PTT before KEY");
  const auto end = 7U + plan->dot_duration_ns;
  expect(scheduler.advance(end) == CwTransmitAdvanceResult::Transition &&
             !scheduler.snapshot().lines.key && scheduler.snapshot().lines.ptt,
         "zero hang first exposes KEY off with PTT held");
  expect(scheduler.advance(end) == CwTransmitAdvanceResult::Transition &&
             !scheduler.snapshot().lines.ptt,
         "a following same-time advance releases PTT");
}

void test_cancel_and_emergency_release_order() {
  const auto plan = CwTransmitEncoder::encode("T", 20U);
  CwTransmitScheduler cancelled;
  expect(plan && cancelled.start(*plan, 1U), "cancel fixture starts");
  expect(cancelled.advance(100'000'001U) ==
             CwTransmitAdvanceResult::Transition &&
             cancelled.snapshot().lines.key,
         "cancel fixture reaches KEY down");
  expect(cancelled.cancel() && !cancelled.snapshot().lines.key &&
             cancelled.snapshot().lines.ptt &&
             cancelled.snapshot().state == CwTransmitSchedulerState::Cancelled,
         "cancel first requests KEY off and retains PTT");
  expect(cancelled.advance(100'000'001U) ==
             CwTransmitAdvanceResult::Transition &&
             !cancelled.snapshot().lines.ptt,
         "cancel drain releases PTT on the following transition");

  CwTransmitScheduler emergency;
  expect(emergency.start(*plan, 2U), "emergency fixture starts");
  expect(emergency.advance(100'000'002U) ==
             CwTransmitAdvanceResult::Transition,
         "emergency fixture reaches KEY down");
  emergency.emergencyRelease();
  expect(emergency.snapshot().state == CwTransmitSchedulerState::Emergency &&
             !emergency.snapshot().lines.key && emergency.snapshot().lines.ptt,
         "emergency first requests KEY off");
  expect(emergency.advance(100'000'002U) ==
             CwTransmitAdvanceResult::Transition &&
             !emergency.snapshot().lines.ptt,
         "emergency drain then releases PTT");
}

void test_clock_faults_release_safely() {
  const auto plan = CwTransmitEncoder::encode("E", 20U);
  CwTransmitScheduler backwards;
  expect(plan && backwards.start(*plan, 1'000U), "backward fixture starts");
  expect(backwards.advance(999U) == CwTransmitAdvanceResult::Faulted &&
             backwards.snapshot().state == CwTransmitSchedulerState::Fault &&
             !backwards.snapshot().lines.key && backwards.snapshot().lines.ptt,
         "backward clock faults with KEY inactive before PTT");
  expect(backwards.advance(999U) == CwTransmitAdvanceResult::Transition &&
             !backwards.snapshot().lines.ptt,
         "fault drain releases PTT");

  CwTransmitScheduler late({.ptt_lead_ns = 10U,
                            .ptt_hang_ns = 0U,
                            .maximum_message_duration_ns = 1'000'000'000U,
                            .maximum_transition_lateness_ns = 5U});
  expect(late.start(*plan, 100U), "late fixture starts");
  expect(late.advance(116U) == CwTransmitAdvanceResult::Faulted &&
             late.snapshot().state == CwTransmitSchedulerState::Fault &&
             !late.snapshot().lines.key && late.snapshot().lines.ptt,
         "transition beyond the allowed lateness faults safely");
  expect(late.advance(116U) == CwTransmitAdvanceResult::Transition &&
             !late.snapshot().lines.ptt,
         "late-clock fault can be drained in release order");
}

void test_plan_and_duration_bounds() {
  const auto plan = CwTransmitEncoder::encode("T", 20U);
  expect(plan.has_value(), "bound fixture encodes");
  if (!plan) return;

  auto inconsistent = *plan;
  ++inconsistent.total_duration_ns;
  CwTransmitScheduler bad_total;
  expect(!bad_total.start(inconsistent, 0U) &&
             bad_total.snapshot().state == CwTransmitSchedulerState::Fault,
         "scheduler rejects inconsistent plan duration");

  CwTransmitScheduler too_long({
      .maximum_message_duration_ns = plan->total_duration_ns - 1U});
  expect(!too_long.start(*plan, 0U), "configured message bound is enforced");

  auto continuous = *plan;
  continuous.dot_duration_ns = CwTransmitScheduler::kMaximumContinuousKeyDownNs;
  continuous.total_duration_ns =
      continuous.dot_duration_ns * continuous.spans.front().duration_units;
  expect(continuous.spans.front().duration_units == 3U,
         "continuous-key fixture is a dash");
  CwTransmitScheduler over_key({
      .maximum_message_duration_ns = 10'000'000'000ULL});
  expect(!over_key.start(continuous, 0U),
         "continuous KEY beyond the hard limit is rejected");

  CwTransmitScheduler overflow;
  expect(!overflow.start(*plan, std::numeric_limits<std::uint64_t>::max()),
         "session timestamp overflow is rejected");
}

void test_reset_requires_inactive_terminal_state() {
  const auto plan = CwTransmitEncoder::encode("E", 20U);
  CwTransmitScheduler scheduler;
  expect(plan && scheduler.start(*plan, 0U), "reset fixture starts");
  expect(!scheduler.reset(), "running scheduler cannot reset");
  expect(scheduler.cancel(), "reset fixture cancels");
  expect(!scheduler.reset(), "scheduler cannot reset before PTT release");
  static_cast<void>(scheduler.advance(0U));
  expect(scheduler.reset() &&
             scheduler.snapshot().state == CwTransmitSchedulerState::Idle &&
             scheduler.plan() == nullptr,
         "inactive terminal scheduler resets explicitly");
}

}  // namespace

int main() {
  test_exact_sequence_and_immutable_plan();
  test_zero_delays_still_order_release();
  test_cancel_and_emergency_release_order();
  test_clock_faults_release_safely();
  test_plan_and_duration_bounds();
  test_reset_requires_inactive_terminal_state();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
