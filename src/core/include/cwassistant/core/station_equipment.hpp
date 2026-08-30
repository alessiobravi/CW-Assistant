#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "cwassistant/core/frequency_plan.hpp"

namespace cwassistant::core {

struct StationEquipment {
  std::string radio;
  std::string transverter;
  std::string antenna;

  bool operator==(const StationEquipment&) const = default;
};

// Bands use canonical ADIF names (for example 20M, 6M, 70CM, or 13CM).
// Rules are ordered; the first rule containing the actual RF band wins.
struct StationEquipmentRule {
  std::vector<std::string> bands;
  StationEquipment equipment;
};

struct ResolvedStationEquipment {
  StationEquipment rx;
  StationEquipment tx;
};

[[nodiscard]] std::optional<ResolvedStationEquipment> resolve_station_equipment(
    const ResolvedFrequencies& frequencies,
    std::span<const StationEquipmentRule> rules);

[[nodiscard]] std::string describe_station_rig(
    const ResolvedStationEquipment& equipment);
[[nodiscard]] std::string describe_station_antenna(
    const ResolvedStationEquipment& equipment);

}  // namespace cwassistant::core
