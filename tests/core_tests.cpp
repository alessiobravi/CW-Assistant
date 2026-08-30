#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cwassistant/core/adif.hpp"
#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/channel_scheduler.hpp"
#include "cwassistant/core/remote_control.hpp"
#include "cwassistant/core/spectrum_visualization_settings.hpp"
#include "cwassistant/core/spsc_ring_buffer.hpp"
#include "cwassistant/core/transmit_guard.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_ring_buffer() {
  cwassistant::core::SpscRingBuffer<int, 2> queue;
  expect(queue.empty(), "new ring is empty");
  expect(queue.try_push(10), "push first item");
  expect(queue.try_push(20), "push second item");
  expect(!queue.try_push(30), "bounded ring reports full");

  int value = 0;
  expect(queue.try_pop(value) && value == 10, "ring preserves FIFO order");
  expect(queue.try_pop(value) && value == 20, "ring pops second item");
  expect(!queue.try_pop(value), "empty ring reports empty");
}

void test_scheduler() {
  using namespace cwassistant::core;
  const std::vector<DetectedChannel> channels{
      {.id = 1, .snr_db = 4.0F, .arrival_sequence = 30},
      {.id = 2, .snr_db = 18.0F, .arrival_sequence = 20},
      {.id = 3, .snr_db = 8.0F, .arrival_sequence = 10,
       .user_selected = true},
  };
  ChannelScheduler scheduler;
  expect(scheduler.select(channels, 2, ChannelSelectionPolicy::StrongestSignal) ==
             std::vector<std::uint64_t>({2, 3}),
         "strongest policy ranks by SNR");
  expect(scheduler.select(channels, 2, ChannelSelectionPolicy::ArrivalQueue) ==
             std::vector<std::uint64_t>({3, 2}),
         "queue policy ranks by arrival");
  expect(scheduler.select(channels, 2,
                          ChannelSelectionPolicy::UserSelectedFirst) ==
             std::vector<std::uint64_t>({3, 2}),
         "manual choice is scheduled first");
}

void test_transmit_guard() {
  using cwassistant::core::CallsignPolicy;
  using cwassistant::core::TransmitGuard;
  CallsignPolicy policy;
  TransmitGuard guard(policy);
  expect(!guard.begin_transmission(), "cannot transmit while disarmed");
  expect(guard.arm(), "operator can arm TX");
  expect(guard.request_qso("i1abc"), "valid selected call requests QSO");
  expect(!guard.confirm("I1XYZ"), "confirmation must match selected call");
  expect(guard.confirm("I1ABC"), "operator confirms selected call");
  expect(guard.begin_transmission(), "confirmed QSO permits TX");
  expect(guard.finish_transmission(), "TX completes back to armed state");
  expect(policy.add_ignored("w1aw"), "operator can ignore a callsign");
  expect(!guard.request_qso("W1AW"), "ignored callsign cannot request QSO");
  expect(guard.request_qso("K1ABC"), "another call can request QSO");
  expect(policy.add_ignored("K1ABC"), "pending call can become ignored");
  expect(!guard.confirm("K1ABC"), "new ignore rule cancels confirmation");
  expect(guard.state() == cwassistant::core::TransmitState::Armed,
         "ignore rule returns TX guard to armed state");
  guard.trip_fault();
  expect(!guard.arm(), "fault cannot be bypassed by arming");
  expect(guard.reset_fault(), "fault reset returns to disarmed");
}

void test_callsign_policy() {
  using cwassistant::core::CallsignPolicy;
  CallsignPolicy policy;
  expect(policy.add_ignored("  i1abc/p  "), "ignore list normalizes callsign");
  expect(policy.is_ignored("I1ABC/P"), "ignore matching is case insensitive");
  expect(!policy.add_ignored("I1ABC/P"), "ignore list rejects duplicates");
  expect(!policy.add_ignored("NOT A CALL"), "ignore list rejects invalid text");
  expect(policy.remove_ignored("i1abc/p"), "ignored callsign can be restored");
  expect(!policy.is_ignored("I1ABC/P"), "removed call is no longer ignored");
}

void test_spectrum_settings() {
  cwassistant::core::SpectrumVisualizationSettings settings{
      .target_fps = 500,
      .waterfall_lines_per_second = 0,
      .lower_bound_db = -20.0F,
      .upper_bound_db = -19.0F,
      .averaging_frames = 100,
  };
  const auto safe = settings.sanitized();
  expect(safe.target_fps == 120, "spectrum FPS is bounded");
  expect(safe.waterfall_lines_per_second == 1,
         "waterfall speed is independently bounded");
  expect(safe.upper_bound_db - safe.lower_bound_db >= 10.0F,
         "manual range retains visible span");
  expect(safe.averaging_frames == 32, "averaging is bounded");
}

void test_remote_control_lease() {
  using namespace std::chrono_literals;
  using cwassistant::core::ControlLeaseManager;
  ControlLeaseManager leases;
  const auto start = ControlLeaseManager::TimePoint{};

  expect(leases.acquire("client-a", "rig-1", start, 10s),
         "first remote client acquires rig lease");
  expect(!leases.acquire("client-b", "rig-1", start, 10s),
         "second client cannot steal active rig lease");
  expect(leases.acquire("client-b", "rig-2", start, 10s),
         "different rigs have independent leases");
  expect(leases.owns("client-a", "rig-1", start + 1s),
         "lease owner is recognized");
  expect(leases.renew("client-a", "rig-1", start + 1s, 20s),
         "lease owner can renew heartbeat");
  expect(leases.owns("client-a", "rig-1", start + 15s),
         "renewed lease remains active");
  expect(!leases.owns("client-a", "rig-1", start + 22s),
         "lease expires without heartbeat");
  expect(leases.acquire("client-b", "rig-1", start + 22s, 1ms),
         "new client can acquire expired lease");
  expect(leases.owns("client-b", "rig-1", start + 23s),
         "minimum TTL clamp prevents unsafe instant expiry");
  expect(!leases.release("client-a", "rig-1"),
         "non-owner cannot release another lease");
  expect(leases.release("client-b", "rig-1"),
         "owner can explicitly release lease");
}

void test_adif() {
  const cwassistant::core::QsoRecord qso{
      .callsign = "I1ABC",
      .qso_date = "20260830",
      .time_on = "143512",
      .band = "20M",
      .mode = "CW",
      .frequency_mhz = "14.025000",
      .rst_sent = "599",
      .rst_received = "579",
      .station_callsign = "IU0XYZ",
  };
  const auto adif = cwassistant::core::to_adif(qso);
  expect(adif.find("<CALL:5>I1ABC") != std::string::npos,
         "ADIF encodes field length");
  expect(adif.ends_with("<EOR>"), "ADIF terminates the record");
}

}  // namespace

int main() {
  test_ring_buffer();
  test_scheduler();
  test_callsign_policy();
  test_spectrum_settings();
  test_remote_control_lease();
  test_transmit_guard();
  test_adif();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
