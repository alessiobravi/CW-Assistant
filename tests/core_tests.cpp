#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cwassistant/core/adif.hpp"
#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cat4om_protocol.hpp"
#include "cwassistant/core/channel_scheduler.hpp"
#include "cwassistant/core/frequency_plan.hpp"
#include "cwassistant/core/remote_control.hpp"
#include "cwassistant/core/reference_rig_profiles.hpp"
#include "cwassistant/core/spectrum_visualization_settings.hpp"
#include "cwassistant/core/station_equipment.hpp"
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

void test_split_transverter_and_satellite_adif() {
  using namespace cwassistant::core;
  const VfoFrequencyPlan plan{
      .rx_dial_hz = 29'900'000,
      .tx_dial_hz = 28'300'000,
      .split_enabled = true,
  };
  const TransverterOffsets offsets{
      .rx_offset_hz = 116'000'000,
      .tx_offset_hz = 407'000'000,
  };
  const auto resolved = resolve_frequencies(plan, offsets);
  expect(resolved.has_value(), "split transverter frequencies resolve");
  expect(resolved && resolved->rx_rf_hz == 145'900'000,
         "positive RX transverter offset produces actual downlink RF");
  expect(resolved && resolved->tx_rf_hz == 435'300'000,
         "independent positive TX offset produces actual uplink RF");

  QsoRecord qso{
      .callsign = "I1ABC",
      .qso_date = "20260830",
      .time_on = "143512",
      .mode = "CW",
      .rst_sent = "599",
      .rst_received = "579",
      .station_callsign = "IU0XYZ",
  };
  const SatelliteQsoDetails satellite{.name = "AO-7", .mode = "U/V"};
  expect(populate_qso_frequencies(qso, plan, offsets, &satellite),
         "satellite QSO receives calculated RF fields");
  expect(qso.band == "70CM" && qso.band_rx == "2M",
         "ADIF TX and RX bands derive from actual RF frequencies");
  expect(qso.frequency_mhz == "435.300000" &&
             qso.frequency_rx_mhz == "145.900000",
         "ADIF keeps exact TX and RX frequencies to one hertz");

  const auto adif = to_adif(qso);
  expect(adif.find("<BAND:4>70CM") != std::string::npos,
         "ADIF exports transmit band");
  expect(adif.find("<BAND_RX:2>2M") != std::string::npos,
         "ADIF exports receive band");
  expect(adif.find("<FREQ:10>435.300000") != std::string::npos,
         "ADIF exports actual transmit frequency");
  expect(adif.find("<FREQ_RX:10>145.900000") != std::string::npos,
         "ADIF exports actual receive frequency");
  expect(adif.find("<PROP_MODE:3>SAT") != std::string::npos &&
             adif.find("<SAT_NAME:4>AO-7") != std::string::npos &&
             adif.find("<SAT_MODE:3>U/V") != std::string::npos,
         "ADIF exports satellite propagation, name, and mode");
}

void test_negative_transverter_offset_and_invalid_frequency() {
  using namespace cwassistant::core;
  const auto resolved = resolve_frequencies(
      {.rx_dial_hz = 145'900'000, .split_enabled = false},
      {.rx_offset_hz = -116'000'000, .tx_offset_hz = -116'000'000});
  expect(resolved && resolved->rx_rf_hz == 29'900'000 &&
             resolved->tx_rf_hz == 29'900'000,
         "negative transverter offsets are supported for RX and TX");
  expect(adif_band_from_frequency(29'900'000).empty(),
         "out-of-band frequency is not mislabeled in ADIF");
  expect(!resolve_frequencies(
              {.rx_dial_hz = 10'000'000, .split_enabled = false},
              {.rx_offset_hz = -10'000'000, .tx_offset_hz = 0}),
         "offset calculation rejects zero or underflowed actual RF");
}

void test_band_selected_station_equipment_adif() {
  using namespace cwassistant::core;
  const std::vector<StationEquipmentRule> rules{
      {
          .bands = {"6M", "10M", "12M", "15M", "17M", "20M"},
          .equipment = {.radio = "Yaesu FT-450D", .antenna = "Dipole"},
      },
      {
          .bands = {"13CM"},
          .equipment = {
              .radio = "Microwave IF radio",
              .transverter = "DXPatrol Transverter",
              .antenna = "Offset parabolic dish",
          },
      },
  };
  const ResolvedFrequencies hf{
      .rx_rf_hz = 14'025'000,
      .tx_rf_hz = 14'025'000,
  };
  const auto hf_equipment = resolve_station_equipment(hf, rules);
  expect(hf_equipment && describe_station_rig(*hf_equipment) == "Yaesu FT-450D",
         "HF band rule selects its configured radio");
  expect(hf_equipment && describe_station_antenna(*hf_equipment) == "Dipole",
         "HF band rule selects its configured antenna");

  const ResolvedFrequencies cross_band{
      .rx_rf_hz = 14'025'000,
      .tx_rf_hz = 2'320'100'000,
      .split_enabled = true,
  };
  QsoRecord qso;
  expect(populate_qso_station_equipment(qso, cross_band, rules),
         "cross-band equipment chains resolve from actual RF bands");
  expect(qso.station_rig ==
             "TX: Microwave IF radio + DXPatrol Transverter; RX: Yaesu FT-450D",
         "different TX/RX radio chains are explicit");
  expect(qso.station_antenna ==
             "TX: Offset parabolic dish; RX: Dipole",
         "different TX/RX antennas are explicit");
  const auto adif = to_adif(qso);
  expect(adif.find("<MY_RIG:") != std::string::npos &&
             adif.find("<MY_ANTENNA:") != std::string::npos,
         "ADIF exports logging-station rig and antenna fields");
}

void test_reference_rig_profiles() {
  using namespace cwassistant::core;
  const auto profiles = reference_rig_profiles();
  expect(profiles.size() == 2, "two Yaesu reference profiles are available");

  const auto* ft450d = find_reference_rig_profile("yaesu-ft-450d");
  expect(ft450d != nullptr, "FT-450D profile is selectable");
  expect(ft450d != nullptr && ft450d->cat.baud_rate == 4'800 &&
             ft450d->cat.data_bits == 8 && ft450d->cat.stop_bits == 1,
         "FT-450D starts with documented 4800 8-N-1 CAT framing");
  expect(ft450d != nullptr && ft450d->omnirig_rig_type == "FT-450",
         "FT-450D maps to the OmniRig FT-450 command description");

  const auto* ft818 = find_reference_rig_profile("yaesu-ft-818");
  expect(ft818 != nullptr, "FT-818 profile is selectable");
  expect(ft818 != nullptr && ft818->cat.baud_rate == 4'800 &&
             ft818->cat.data_bits == 8 && ft818->cat.stop_bits == 2,
         "FT-818 starts with documented 4800 8-N-2 CAT framing");
  expect(ft818 != nullptr && ft818->omnirig_rig_type == "FT-817",
         "FT-818 uses the compatible OmniRig FT-817 command description");

  expect(ft450d != nullptr && ft450d->cat.port.empty() &&
             ft450d->keying.port.empty(),
         "reference profiles never guess physical COM ports");
  expect(ft450d != nullptr && ft450d->ptt_line != ft450d->key_line,
         "direct keying defaults PTT and KEY to different lines");
}

void test_cat4om_protocol_contract() {
  using namespace cwassistant::core;
  expect(cat4om_protocol_compatible("1.0.0") &&
             cat4om_protocol_compatible("1.99.3"),
         "CAT4OM accepts additive changes within protocol major 1");
  expect(!cat4om_protocol_compatible("2.0.0") &&
             !cat4om_protocol_compatible("invalid"),
         "CAT4OM rejects incompatible or malformed protocol versions");
  expect(cat4om_role_from_string("master") == Cat4OmRole::Master &&
             cat4om_role_from_string("new-role") == Cat4OmRole::Unknown,
         "CAT4OM roles degrade safely when a future value is unknown");

  const Cat4OmRadioState simplex{
      .radio_id = "run",
      .connection_status = "connected",
      .active_vfo = "MAIN",
      .tx_vfo = "SUB",
      .split = false,
      .vfos = {{.id = "MAIN", .frequency_hz = 14'025'000},
               {.id = "SUB", .frequency_hz = 7'010'000}},
      .available_commands = {"SetFrequency", "SetSplit"},
  };
  const auto simplex_plan = cat4om_frequency_plan(simplex);
  expect(simplex_plan && simplex_plan->rx_dial_hz == 14'025'000 &&
             simplex_plan->tx_dial_hz == 14'025'000 &&
             !simplex_plan->split_enabled,
         "CAT4OM simplex state uses the active VFO for RX and TX");
  expect(cat4om_has_command(simplex, "setfrequency"),
         "CAT4OM command capability matching tolerates case only");

  auto split = simplex;
  split.split = true;
  const auto split_plan = cat4om_frequency_plan(split);
  expect(split_plan && split_plan->rx_dial_hz == 14'025'000 &&
             split_plan->tx_dial_hz == 7'010'000 &&
             split_plan->split_enabled,
         "CAT4OM split state preserves independent opaque VFO names");
  split.tx_vfo = "missing";
  expect(!cat4om_frequency_plan(split),
         "CAT4OM refuses an incomplete split frequency snapshot");
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
  test_split_transverter_and_satellite_adif();
  test_negative_transverter_offset_and_invalid_frequency();
  test_band_selected_station_equipment_adif();
  test_reference_rig_profiles();
  test_cat4om_protocol_contract();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
