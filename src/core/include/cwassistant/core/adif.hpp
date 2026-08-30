#pragma once

#include <string>

#include "cwassistant/core/frequency_plan.hpp"
#include "cwassistant/core/station_equipment.hpp"

namespace cwassistant::core {

struct QsoRecord {
  std::string callsign;
  std::string qso_date;
  std::string time_on;
  std::string band;
  std::string band_rx;
  std::string mode{"CW"};
  std::string frequency_mhz;
  std::string frequency_rx_mhz;
  std::string propagation_mode;
  std::string satellite_name;
  std::string satellite_mode;
  std::string rst_sent;
  std::string rst_received;
  std::string station_callsign;
  std::string station_rig;
  std::string station_antenna;
};

struct SatelliteQsoDetails {
  std::string name;
  std::string mode;
};

// Populates transmit (FREQ/BAND) and receive (FREQ_RX/BAND_RX) from actual RF
// frequencies. Returns false without modifying the record if calculation or
// ADIF band resolution fails.
[[nodiscard]] bool populate_qso_frequencies(
    QsoRecord& record,
    const VfoFrequencyPlan& plan,
    const TransverterOffsets& offsets,
    const SatelliteQsoDetails* satellite = nullptr);

[[nodiscard]] bool populate_qso_station_equipment(
    QsoRecord& record,
    const ResolvedFrequencies& frequencies,
    std::span<const StationEquipmentRule> rules);

// Serializes one ADIF record suitable for Log4OM's inbound ADIF UDP service.
[[nodiscard]] std::string to_adif(const QsoRecord& record);

}  // namespace cwassistant::core
