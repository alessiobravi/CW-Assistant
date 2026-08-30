#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cwassistant/core/adif.hpp"
#include "cwassistant/core/channel_scheduler.hpp"
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
  using cwassistant::core::TransmitGuard;
  TransmitGuard guard;
  expect(!guard.begin_transmission(), "cannot transmit while disarmed");
  expect(guard.arm(), "operator can arm TX");
  expect(guard.request_qso("i1abc"), "valid selected call requests QSO");
  expect(!guard.confirm("I1XYZ"), "confirmation must match selected call");
  expect(guard.confirm("I1ABC"), "operator confirms selected call");
  expect(guard.begin_transmission(), "confirmed QSO permits TX");
  expect(guard.finish_transmission(), "TX completes back to armed state");
  guard.trip_fault();
  expect(!guard.arm(), "fault cannot be bypassed by arming");
  expect(guard.reset_fault(), "fault reset returns to disarmed");
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
  test_transmit_guard();
  test_adif();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
