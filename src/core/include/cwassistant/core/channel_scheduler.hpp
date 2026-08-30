#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cwassistant::core {

enum class ChannelSelectionPolicy {
  StrongestSignal,
  ArrivalQueue,
  UserSelectedFirst,
};

struct DetectedChannel {
  std::uint64_t id{0};
  double offset_hz{0.0};
  float snr_db{0.0F};
  std::uint64_t arrival_sequence{0};
  bool user_selected{false};
};

class ChannelScheduler {
 public:
  [[nodiscard]] std::vector<std::uint64_t> select(
      std::span<const DetectedChannel> candidates,
      std::size_t maximum_channels,
      ChannelSelectionPolicy policy) const;
};

}  // namespace cwassistant::core
