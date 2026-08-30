#pragma once

#include <span>
#include <string_view>

#include "cwassistant/core/interfaces.hpp"

namespace cwassistant::core {

// Reference profiles are editable starting points, never locked settings.
[[nodiscard]] std::span<const RigProfile> reference_rig_profiles() noexcept;
[[nodiscard]] const RigProfile* find_reference_rig_profile(
    std::string_view id) noexcept;

}  // namespace cwassistant::core
