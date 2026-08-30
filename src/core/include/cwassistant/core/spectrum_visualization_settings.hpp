#pragma once

#include <algorithm>
#include <cstdint>

namespace cwassistant::core {

enum class SpectrumRangeMode { Automatic, Manual };

struct SpectrumVisualizationSettings {
  SpectrumRangeMode range_mode{SpectrumRangeMode::Automatic};
  std::uint16_t target_fps{30};
  std::uint16_t waterfall_lines_per_second{20};
  float lower_bound_db{-130.0F};
  float upper_bound_db{-40.0F};
  std::uint8_t averaging_frames{3};
  bool peak_hold{true};
  bool show_grid{true};
  bool show_callsigns{true};

  [[nodiscard]] SpectrumVisualizationSettings sanitized() const noexcept {
    auto result = *this;
    result.target_fps = std::clamp<std::uint16_t>(target_fps, 5, 120);
    result.waterfall_lines_per_second =
        std::clamp<std::uint16_t>(waterfall_lines_per_second, 1, 120);
    result.lower_bound_db = std::clamp(lower_bound_db, -200.0F, 40.0F);
    result.upper_bound_db = std::clamp(upper_bound_db, -190.0F, 50.0F);
    if (result.upper_bound_db - result.lower_bound_db < 10.0F) {
      result.upper_bound_db = std::min(50.0F, result.lower_bound_db + 10.0F);
      result.lower_bound_db =
          std::min(result.lower_bound_db, result.upper_bound_db - 10.0F);
    }
    result.averaging_frames =
        std::clamp<std::uint8_t>(averaging_frames, 1, 32);
    return result;
  }
};

}  // namespace cwassistant::core
