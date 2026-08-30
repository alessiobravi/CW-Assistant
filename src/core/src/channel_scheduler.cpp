#include "cwassistant/core/channel_scheduler.hpp"

#include <algorithm>

namespace cwassistant::core {

std::vector<std::uint64_t> ChannelScheduler::select(
    std::span<const DetectedChannel> candidates,
    const std::size_t maximum_channels,
    const ChannelSelectionPolicy policy) const {
  std::vector<DetectedChannel> ordered(candidates.begin(), candidates.end());

  std::stable_sort(ordered.begin(), ordered.end(), [policy](const auto& left,
                                                            const auto& right) {
    if (policy == ChannelSelectionPolicy::UserSelectedFirst &&
        left.user_selected != right.user_selected) {
      return left.user_selected;
    }
    if (policy == ChannelSelectionPolicy::ArrivalQueue ||
        policy == ChannelSelectionPolicy::UserSelectedFirst) {
      return left.arrival_sequence < right.arrival_sequence;
    }
    return left.snr_db > right.snr_db;
  });

  const auto count = std::min(maximum_channels, ordered.size());
  std::vector<std::uint64_t> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(ordered[index].id);
  }
  return result;
}

}  // namespace cwassistant::core
