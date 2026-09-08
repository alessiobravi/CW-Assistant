#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace cwassistant::core {

inline constexpr std::size_t kMaximumRadioVfoIdentifierLength = 32U;

enum class RadioObservation { Unavailable, Unknown, Known };

enum class RadioMode {
  Unknown,
  Cw,
  CwReverse,
  LowerSideband,
  UpperSideband,
  Am,
  Fm,
  DigitalLower,
  DigitalUpper,
  Rtty,
  RttyReverse,
};

enum class RadioSplit { Unknown, Disabled, Enabled };

enum class RadioCapability : std::uint32_t {
  None = 0,
  SetRxFrequency = 1U << 0U,
  SetTxFrequency = 1U << 1U,
  SetRxMode = 1U << 2U,
  SetTxMode = 1U << 3U,
  SelectRxVfo = 1U << 4U,
  SelectTxVfo = 1U << 5U,
  SetSplit = 1U << 6U,
};

using RadioCapabilityBits = std::uint32_t;

[[nodiscard]] constexpr RadioCapabilityBits radio_capability_bit(
    const RadioCapability capability) noexcept {
  return static_cast<RadioCapabilityBits>(capability);
}

[[nodiscard]] constexpr RadioCapabilityBits operator|(
    const RadioCapability left, const RadioCapability right) noexcept {
  return radio_capability_bit(left) | radio_capability_bit(right);
}

[[nodiscard]] constexpr RadioCapabilityBits operator|(
    const RadioCapabilityBits left, const RadioCapability right) noexcept {
  return left | radio_capability_bit(right);
}

struct RadioFrequencyState {
  RadioObservation observation{RadioObservation::Unknown};
  std::uint64_t hz{0};
  bool operator==(const RadioFrequencyState&) const = default;
};

struct RadioModeState {
  RadioObservation observation{RadioObservation::Unknown};
  RadioMode mode{RadioMode::Unknown};
  bool operator==(const RadioModeState&) const = default;
};

struct RadioVfoState {
  RadioObservation observation{RadioObservation::Unknown};
  std::string identifier;
  bool operator==(const RadioVfoState&) const = default;
};

struct RadioSplitState {
  RadioObservation observation{RadioObservation::Unknown};
  RadioSplit split{RadioSplit::Unknown};
  bool operator==(const RadioSplitState&) const = default;
};

struct RadioCapabilities {
  RadioObservation observation{RadioObservation::Unknown};
  RadioCapabilityBits bits{0};
  bool operator==(const RadioCapabilities&) const = default;
};

struct RadioState {
  RadioObservation availability{RadioObservation::Unknown};
  RadioFrequencyState rx_frequency;
  RadioFrequencyState tx_frequency;
  RadioModeState rx_mode;
  RadioModeState tx_mode;
  RadioVfoState rx_vfo;
  RadioVfoState tx_vfo;
  RadioSplitState split;
  RadioCapabilities capabilities;
  bool operator==(const RadioState&) const = default;
};

struct SetRxFrequency {
  std::uint64_t hz{0};
};
struct SetTxFrequency {
  std::uint64_t hz{0};
};
struct SetRxMode {
  RadioMode mode{RadioMode::Unknown};
};
struct SetTxMode {
  RadioMode mode{RadioMode::Unknown};
};
struct SelectRxVfo {
  std::string identifier;
};
struct SelectTxVfo {
  std::string identifier;
};
struct SetSplit {
  bool enabled{false};
};

using RadioCommand =
    std::variant<SetRxFrequency, SetTxFrequency, SetRxMode, SetTxMode,
                 SelectRxVfo, SelectTxVfo, SetSplit>;

enum class RadioCommandValidation {
  Valid,
  InvalidState,
  RadioUnavailable,
  RadioStateUnknown,
  CapabilitiesUnavailable,
  CapabilitiesUnknown,
  Unsupported,
  InvalidFrequency,
  InvalidMode,
  InvalidVfoIdentifier,
};

[[nodiscard]] bool radio_vfo_identifier_is_valid(
    const std::string& identifier) noexcept;
[[nodiscard]] RadioMode radio_mode_from_token(std::string_view token) noexcept;
[[nodiscard]] std::string_view radio_mode_token(RadioMode mode) noexcept;
[[nodiscard]] bool radio_tx_mode_target_is_valid(RadioMode mode) noexcept;
[[nodiscard]] bool radio_mode_target_is_confirmed(
    const RadioModeState& observation, RadioMode target) noexcept;
[[nodiscard]] bool radio_state_is_valid(const RadioState& state) noexcept;
[[nodiscard]] bool radio_has_capability(
    const RadioCapabilities& capabilities,
    RadioCapability capability) noexcept;
[[nodiscard]] RadioCommandValidation validate_radio_command(
    const RadioState& state, const RadioCommand& command) noexcept;

}  // namespace cwassistant::core
