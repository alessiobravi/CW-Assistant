#include "cwassistant/core/adif.hpp"

#include <string_view>
#include <utility>

namespace cwassistant::core {
namespace {

void append_field(std::string& output,
                  const std::string_view name,
                  const std::string_view value) {
  if (value.empty()) {
    return;
  }
  output += '<';
  output += name;
  output += ':';
  output += std::to_string(value.size());
  output += '>';
  output += value;
  output += ' ';
}

}  // namespace

std::string to_adif(const QsoRecord& record) {
  std::string output;
  output.reserve(192);
  append_field(output, "CALL", record.callsign);
  append_field(output, "QSO_DATE", record.qso_date);
  append_field(output, "TIME_ON", record.time_on);
  append_field(output, "BAND", record.band);
  append_field(output, "BAND_RX", record.band_rx);
  append_field(output, "MODE", record.mode);
  append_field(output, "FREQ", record.frequency_mhz);
  append_field(output, "FREQ_RX", record.frequency_rx_mhz);
  append_field(output, "PROP_MODE", record.propagation_mode);
  append_field(output, "SAT_NAME", record.satellite_name);
  append_field(output, "SAT_MODE", record.satellite_mode);
  append_field(output, "RST_SENT", record.rst_sent);
  append_field(output, "RST_RCVD", record.rst_received);
  append_field(output, "STATION_CALLSIGN", record.station_callsign);
  append_field(output, "MY_RIG", record.station_rig);
  append_field(output, "MY_ANTENNA", record.station_antenna);
  output += "<EOR>";
  return output;
}

bool populate_qso_frequencies(QsoRecord& record,
                              const VfoFrequencyPlan& plan,
                              const TransverterOffsets& offsets,
                              const SatelliteQsoDetails* satellite) {
  const auto resolved = resolve_frequencies(plan, offsets);
  if (!resolved) {
    return false;
  }
  const auto tx_band = adif_band_from_frequency(resolved->tx_rf_hz);
  const auto rx_band = adif_band_from_frequency(resolved->rx_rf_hz);
  if (tx_band.empty() || rx_band.empty()) {
    return false;
  }

  QsoRecord updated = record;
  updated.band = tx_band;
  updated.band_rx = rx_band;
  updated.frequency_mhz = format_frequency_mhz(resolved->tx_rf_hz);
  updated.frequency_rx_mhz = format_frequency_mhz(resolved->rx_rf_hz);
  if (satellite != nullptr) {
    if (satellite->name.empty() || satellite->mode.empty()) {
      return false;
    }
    updated.propagation_mode = "SAT";
    updated.satellite_name = satellite->name;
    updated.satellite_mode = satellite->mode;
  } else {
    updated.satellite_name.clear();
    updated.satellite_mode.clear();
    if (updated.propagation_mode == "SAT") {
      updated.propagation_mode.clear();
    }
  }
  record = std::move(updated);
  return true;
}

bool populate_qso_station_equipment(
    QsoRecord& record,
    const ResolvedFrequencies& frequencies,
    const std::span<const StationEquipmentRule> rules) {
  const auto equipment = resolve_station_equipment(frequencies, rules);
  if (!equipment) {
    return false;
  }
  QsoRecord updated = record;
  updated.station_rig = describe_station_rig(*equipment);
  updated.station_antenna = describe_station_antenna(*equipment);
  if (updated.station_rig.empty() || updated.station_antenna.empty()) {
    return false;
  }
  record = std::move(updated);
  return true;
}

}  // namespace cwassistant::core
