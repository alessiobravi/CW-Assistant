#include "cwassistant/core/station_equipment.hpp"

#include <algorithm>
#include <string_view>

namespace cwassistant::core {
namespace {

const StationEquipment* find_equipment(
    const std::string_view band,
    const std::span<const StationEquipmentRule> rules) {
  for (const auto& rule : rules) {
    if (std::ranges::find(rule.bands, band) != rule.bands.end()) {
      return &rule.equipment;
    }
  }
  return nullptr;
}

std::string rig_chain(const StationEquipment& equipment) {
  if (equipment.radio.empty()) {
    return equipment.transverter;
  }
  if (equipment.transverter.empty()) {
    return equipment.radio;
  }
  return equipment.radio + " + " + equipment.transverter;
}

std::string describe_paths(const std::string& tx,
                           const std::string& rx,
                           const bool same_equipment) {
  if (same_equipment || tx == rx) {
    return tx;
  }
  return "TX: " + tx + "; RX: " + rx;
}

}  // namespace

std::optional<ResolvedStationEquipment> resolve_station_equipment(
    const ResolvedFrequencies& frequencies,
    const std::span<const StationEquipmentRule> rules) {
  const auto tx_band = adif_band_from_frequency(frequencies.tx_rf_hz);
  const auto rx_band = adif_band_from_frequency(frequencies.rx_rf_hz);
  if (tx_band.empty() || rx_band.empty()) {
    return std::nullopt;
  }
  const auto* tx = find_equipment(tx_band, rules);
  const auto* rx = find_equipment(rx_band, rules);
  if (tx == nullptr || rx == nullptr) {
    return std::nullopt;
  }
  return ResolvedStationEquipment{.rx = *rx, .tx = *tx};
}

std::string describe_station_rig(
    const ResolvedStationEquipment& equipment) {
  return describe_paths(rig_chain(equipment.tx), rig_chain(equipment.rx),
                        equipment.tx == equipment.rx);
}

std::string describe_station_antenna(
    const ResolvedStationEquipment& equipment) {
  return describe_paths(equipment.tx.antenna, equipment.rx.antenna,
                        equipment.tx == equipment.rx);
}

}  // namespace cwassistant::core
