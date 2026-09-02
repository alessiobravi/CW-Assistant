#include "cwassistant/core/reference_rig_profiles.hpp"

#include <array>

namespace cwassistant::core {
namespace {

constexpr SerialSettings cat_settings(const std::uint32_t baud_rate,
                                      const std::uint8_t stop_bits,
                                      const bool hardware_flow_control) {
  return SerialSettings{
      .port = {},
      .baud_rate = baud_rate,
      .data_bits = 8,
      .stop_bits = stop_bits,
      .parity = SerialSettings::Parity::None,
      .flow_control = hardware_flow_control
                          ? SerialSettings::FlowControl::Hardware
                          : SerialSettings::FlowControl::None,
  };
}

constexpr SerialSettings direct_keying_settings() {
  return SerialSettings{
      .port = {},
      .baud_rate = 9'600,
      .data_bits = 8,
      .stop_bits = 1,
      .parity = SerialSettings::Parity::None,
      .flow_control = SerialSettings::FlowControl::None,
      .rts_active_high = true,
      .dtr_active_high = true,
  };
}

// Hamlib model numbers are retained for diagnostics and compatibility with
// rigctl. Adapters should resolve by model/name when the installed library can
// provide a runtime model list.
const std::array<RigProfile, 2> kReferenceProfiles{{
    {
        .id = "yaesu-ft-450d",
        .display_name = "Yaesu FT-450D",
        .hamlib_model_id = 1046,
        .omnirig_rig_type = "FT-450",
        .frequency_backend = FrequencyControlBackend::OmniRig,
        .omnirig_slot = 1,
        .poll_interval_ms = 500,
        .timeout_ms = 4'000,
        .cat = cat_settings(4'800, 1, true),
        .keying = direct_keying_settings(),
        .ptt_line = SerialKeyLine::Rts,
        .key_line = SerialKeyLine::Dtr,
    },
    {
        .id = "yaesu-ft-818",
        .display_name = "Yaesu FT-818/FT-818ND",
        .hamlib_model_id = 1041,
        // OmniRig currently publishes the compatible FT-817 command profile.
        .omnirig_rig_type = "FT-817",
        .frequency_backend = FrequencyControlBackend::OmniRig,
        .omnirig_slot = 1,
        .poll_interval_ms = 500,
        .timeout_ms = 4'000,
        .cat = cat_settings(4'800, 2, false),
        .keying = direct_keying_settings(),
        .ptt_line = SerialKeyLine::Rts,
        .key_line = SerialKeyLine::Dtr,
    },
}};

}  // namespace

std::span<const RigProfile> reference_rig_profiles() noexcept {
  return kReferenceProfiles;
}

const RigProfile* find_reference_rig_profile(const std::string_view id) noexcept {
  for (const auto& profile : kReferenceProfiles) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

}  // namespace cwassistant::core
