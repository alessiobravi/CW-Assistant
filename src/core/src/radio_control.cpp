#include "cwassistant/core/radio_control.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <type_traits>

namespace cwassistant::core {
namespace {

bool observation_is_valid(const RadioObservation observation,
                          const bool value_is_known) noexcept {
  return (observation == RadioObservation::Known) == value_is_known;
}

bool frequency_state_is_valid(const RadioFrequencyState& state) noexcept {
  return observation_is_valid(state.observation, state.hz != 0U);
}

bool mode_state_is_valid(const RadioModeState& state) noexcept {
  return observation_is_valid(state.observation,
                              state.mode != RadioMode::Unknown);
}

bool vfo_state_is_valid(const RadioVfoState& state) noexcept {
  return observation_is_valid(
      state.observation, radio_vfo_identifier_is_valid(state.identifier));
}

bool split_state_is_valid(const RadioSplitState& state) noexcept {
  return observation_is_valid(state.observation,
                              state.split != RadioSplit::Unknown);
}

RadioCapability required_capability(const RadioCommand& command) noexcept {
  return std::visit(
      [](const auto& value) {
        using Command = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Command, SetRxFrequency>) {
          return RadioCapability::SetRxFrequency;
        } else if constexpr (std::is_same_v<Command, SetTxFrequency>) {
          return RadioCapability::SetTxFrequency;
        } else if constexpr (std::is_same_v<Command, SetRxMode>) {
          return RadioCapability::SetRxMode;
        } else if constexpr (std::is_same_v<Command, SetTxMode>) {
          return RadioCapability::SetTxMode;
        } else if constexpr (std::is_same_v<Command, SelectRxVfo>) {
          return RadioCapability::SelectRxVfo;
        } else if constexpr (std::is_same_v<Command, SelectTxVfo>) {
          return RadioCapability::SelectTxVfo;
        } else {
          return RadioCapability::SetSplit;
        }
      },
      command);
}

RadioCommandValidation validate_value(const RadioCommand& command) noexcept {
  return std::visit(
      [](const auto& value) {
        using Command = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Command, SetRxFrequency> ||
                      std::is_same_v<Command, SetTxFrequency>) {
          return value.hz == 0U ? RadioCommandValidation::InvalidFrequency
                                : RadioCommandValidation::Valid;
        } else if constexpr (std::is_same_v<Command, SetRxMode> ||
                             std::is_same_v<Command, SetTxMode>) {
          return value.mode == RadioMode::Unknown
                     ? RadioCommandValidation::InvalidMode
                     : RadioCommandValidation::Valid;
        } else if constexpr (std::is_same_v<Command, SelectRxVfo> ||
                             std::is_same_v<Command, SelectTxVfo>) {
          return radio_vfo_identifier_is_valid(value.identifier)
                     ? RadioCommandValidation::Valid
                     : RadioCommandValidation::InvalidVfoIdentifier;
        } else {
          return RadioCommandValidation::Valid;
        }
      },
      command);
}

}  // namespace

RadioMode radio_mode_from_token(const std::string_view token) noexcept {
  std::string normalized;
  normalized.reserve(token.size());
  std::transform(token.begin(), token.end(), std::back_inserter(normalized),
                 [](const char value) {
                   return static_cast<char>(
                       std::toupper(static_cast<unsigned char>(value)));
                 });
  if (normalized == "CW" || normalized == "CWU" || normalized == "CW-U")
    return RadioMode::Cw;
  if (normalized == "CWR" || normalized == "CWL" ||
      normalized == "CW-R" || normalized == "CW-L")
    return RadioMode::CwReverse;
  if (normalized == "LSB") return RadioMode::LowerSideband;
  if (normalized == "USB") return RadioMode::UpperSideband;
  if (normalized == "AM") return RadioMode::Am;
  if (normalized == "FM") return RadioMode::Fm;
  if (normalized == "DIGL" || normalized == "DIG-L" ||
      normalized == "PKTLSB")
    return RadioMode::DigitalLower;
  if (normalized == "DIGU" || normalized == "DIG-U" ||
      normalized == "PKTUSB")
    return RadioMode::DigitalUpper;
  if (normalized == "RTTY") return RadioMode::Rtty;
  if (normalized == "RTTYR" || normalized == "RTTY-R")
    return RadioMode::RttyReverse;
  return RadioMode::Unknown;
}

std::string_view radio_mode_token(const RadioMode mode) noexcept {
  switch (mode) {
    case RadioMode::Cw: return "CW";
    case RadioMode::CwReverse: return "CW-R";
    case RadioMode::LowerSideband: return "LSB";
    case RadioMode::UpperSideband: return "USB";
    case RadioMode::Am: return "AM";
    case RadioMode::Fm: return "FM";
    case RadioMode::DigitalLower: return "DIG-L";
    case RadioMode::DigitalUpper: return "DIG-U";
    case RadioMode::Rtty: return "RTTY";
    case RadioMode::RttyReverse: return "RTTY-R";
    case RadioMode::Unknown: return "?";
  }
  return "?";
}

bool radio_tx_mode_target_is_valid(const RadioMode mode) noexcept {
  return mode == RadioMode::Cw || mode == RadioMode::CwReverse;
}

bool radio_mode_target_is_confirmed(
    const RadioModeState& observation, const RadioMode target) noexcept {
  return radio_tx_mode_target_is_valid(target) &&
         observation.observation == RadioObservation::Known &&
         observation.mode == target;
}

bool radio_tx_frequency_sync_is_available(const RadioState& state) noexcept {
  if (!radio_state_is_valid(state) ||
      state.availability != RadioObservation::Known ||
      state.rx_frequency.observation != RadioObservation::Known ||
      !radio_has_capability(state.capabilities,
                            RadioCapability::SetTxFrequency)) {
    return false;
  }
  return state.split.split == RadioSplit::Enabled ||
         radio_has_capability(state.capabilities, RadioCapability::SetSplit);
}

bool radio_vfo_identifier_is_valid(const std::string& identifier) noexcept {
  return !identifier.empty() &&
         identifier.size() <= kMaximumRadioVfoIdentifierLength &&
         std::all_of(identifier.begin(), identifier.end(), [](const char value) {
           const auto character = static_cast<unsigned char>(value);
           return character >= 0x21U && character <= 0x7EU;
         });
}

bool radio_state_is_valid(const RadioState& state) noexcept {
  constexpr RadioCapabilityBits kKnownCapabilities =
      RadioCapability::SetRxFrequency | RadioCapability::SetTxFrequency |
      RadioCapability::SetRxMode | RadioCapability::SetTxMode |
      RadioCapability::SelectRxVfo | RadioCapability::SelectTxVfo |
      RadioCapability::SetSplit;
  if (!frequency_state_is_valid(state.rx_frequency) ||
      !frequency_state_is_valid(state.tx_frequency) ||
      !mode_state_is_valid(state.rx_mode) ||
      !mode_state_is_valid(state.tx_mode) ||
      !vfo_state_is_valid(state.rx_vfo) ||
      !vfo_state_is_valid(state.tx_vfo) ||
      !split_state_is_valid(state.split)) {
    return false;
  }
  if ((state.capabilities.observation != RadioObservation::Known &&
       state.capabilities.bits != 0U) ||
      (state.capabilities.bits & ~kKnownCapabilities) != 0U) {
    return false;
  }
  return state.availability != RadioObservation::Unavailable ||
         (state.rx_frequency.observation != RadioObservation::Known &&
          state.tx_frequency.observation != RadioObservation::Known &&
          state.rx_mode.observation != RadioObservation::Known &&
          state.tx_mode.observation != RadioObservation::Known &&
          state.rx_vfo.observation != RadioObservation::Known &&
          state.tx_vfo.observation != RadioObservation::Known &&
          state.split.observation != RadioObservation::Known);
}

bool radio_has_capability(const RadioCapabilities& capabilities,
                          const RadioCapability capability) noexcept {
  return capabilities.observation == RadioObservation::Known &&
         (capabilities.bits & radio_capability_bit(capability)) != 0U;
}

RadioCommandValidation validate_radio_command(
    const RadioState& state, const RadioCommand& command) noexcept {
  const auto value_result = validate_value(command);
  if (value_result != RadioCommandValidation::Valid) {
    return value_result;
  }
  if (!radio_state_is_valid(state)) {
    return RadioCommandValidation::InvalidState;
  }
  if (state.availability == RadioObservation::Unavailable) {
    return RadioCommandValidation::RadioUnavailable;
  }
  if (state.availability != RadioObservation::Known) {
    return RadioCommandValidation::RadioStateUnknown;
  }
  if (state.capabilities.observation == RadioObservation::Unavailable) {
    return RadioCommandValidation::CapabilitiesUnavailable;
  }
  if (state.capabilities.observation != RadioObservation::Known) {
    return RadioCommandValidation::CapabilitiesUnknown;
  }
  return radio_has_capability(state.capabilities, required_capability(command))
             ? RadioCommandValidation::Valid
             : RadioCommandValidation::Unsupported;
}

}  // namespace cwassistant::core
