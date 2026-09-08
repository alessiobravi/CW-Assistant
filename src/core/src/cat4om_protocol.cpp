#include "cwassistant/core/cat4om_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace cwassistant::core {
namespace {

bool ascii_equal_case_insensitive(const std::string_view left,
                                  const std::string_view right) noexcept {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const char a, const char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

const Cat4OmVfoState* find_vfo(const Cat4OmRadioState& state,
                              const std::string_view id) noexcept {
  const auto found = std::find_if(
      state.vfos.begin(), state.vfos.end(), [&id](const Cat4OmVfoState& vfo) {
        return vfo.id == id;
      });
  return found == state.vfos.end() ? nullptr : &*found;
}

}  // namespace

std::optional<unsigned int> protocol_major(
    const std::string_view version) noexcept {
  const auto dot = version.find('.');
  const auto major_text = version.substr(0, dot);
  if (major_text.empty()) {
    return std::nullopt;
  }

  unsigned int value = 0;
  for (const char character : major_text) {
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    const auto digit = static_cast<unsigned int>(character - '0');
    if (value > (std::numeric_limits<unsigned int>::max() - digit) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
  }
  return value;
}

bool cat4om_protocol_compatible(const std::string_view version) noexcept {
  return protocol_major(version) == protocol_major(kCat4OmProtocolVersion);
}

Cat4OmRole cat4om_role_from_string(const std::string_view role) noexcept {
  if (role == "observer") {
    return Cat4OmRole::Observer;
  }
  if (role == "slave") {
    return Cat4OmRole::Slave;
  }
  if (role == "master") {
    return Cat4OmRole::Master;
  }
  return Cat4OmRole::Unknown;
}

bool cat4om_has_command(const Cat4OmRadioState& state,
                        const std::string_view command) noexcept {
  return std::any_of(state.available_commands.begin(),
                     state.available_commands.end(),
                     [command](const std::string& candidate) {
                       return ascii_equal_case_insensitive(candidate, command);
                     });
}

std::optional<VfoFrequencyPlan> cat4om_frequency_plan(
    const Cat4OmRadioState& state) noexcept {
  const auto* rx = find_vfo(state, state.active_vfo);
  if (rx == nullptr || rx->frequency_hz == 0) {
    return std::nullopt;
  }

  const auto* tx = state.split ? find_vfo(state, state.tx_vfo) : rx;
  if (tx == nullptr || tx->frequency_hz == 0) {
    return std::nullopt;
  }
  return VfoFrequencyPlan{
      .rx_dial_hz = rx->frequency_hz,
      .tx_dial_hz = tx->frequency_hz,
      .split_enabled = state.split,
  };
}

RadioState cat4om_radio_state(const Cat4OmRadioState& state,
                              const bool writable) noexcept {
  RadioState result;
  const bool connected = state.connection_status == "connected";
  result.availability = connected ? RadioObservation::Known
                                  : RadioObservation::Unavailable;
  if (!connected) {
    result.rx_frequency.observation = RadioObservation::Unavailable;
    result.tx_frequency.observation = RadioObservation::Unavailable;
    result.rx_mode.observation = RadioObservation::Unavailable;
    result.tx_mode.observation = RadioObservation::Unavailable;
    result.rx_vfo.observation = RadioObservation::Unavailable;
    result.tx_vfo.observation = RadioObservation::Unavailable;
    result.split.observation = RadioObservation::Unavailable;
    result.capabilities.observation = RadioObservation::Unavailable;
    return result;
  }

  const auto* rx = find_vfo(state, state.active_vfo);
  const auto* tx = state.split ? find_vfo(state, state.tx_vfo) : rx;
  if (rx != nullptr && rx->frequency_hz != 0U)
    result.rx_frequency = {RadioObservation::Known, rx->frequency_hz};
  if (tx != nullptr && tx->frequency_hz != 0U)
    result.tx_frequency = {RadioObservation::Known, tx->frequency_hz};
  if (radio_vfo_identifier_is_valid(state.active_vfo))
    result.rx_vfo = {RadioObservation::Known, state.active_vfo};
  const std::string& tx_vfo = state.split ? state.tx_vfo : state.active_vfo;
  if (radio_vfo_identifier_is_valid(tx_vfo))
    result.tx_vfo = {RadioObservation::Known, tx_vfo};
  result.split = {RadioObservation::Known,
                  state.split ? RadioSplit::Enabled : RadioSplit::Disabled};

  if (rx != nullptr) {
    const auto mode = radio_mode_from_token(rx->mode);
    if (mode != RadioMode::Unknown)
      result.rx_mode = {RadioObservation::Known, mode};
  }
  if (tx != nullptr) {
    const auto mode = radio_mode_from_token(tx->mode);
    if (mode != RadioMode::Unknown)
      result.tx_mode = {RadioObservation::Known, mode};
  }

  result.capabilities.observation = RadioObservation::Known;
  if (writable && cat4om_has_command(state, "SetFrequency")) {
    result.capabilities.bits = RadioCapability::SetRxFrequency |
                               RadioCapability::SetTxFrequency;
  }
  if (writable && cat4om_has_command(state, "SetMode")) {
    result.capabilities.bits |= RadioCapability::SetRxMode |
                                RadioCapability::SetTxMode;
  }
  if (writable && cat4om_has_command(state, "SetVfo")) {
    result.capabilities.bits |= RadioCapability::SelectRxVfo |
                                RadioCapability::SelectTxVfo;
  }
  if (writable && cat4om_has_command(state, "SetSplit"))
    result.capabilities.bits |= radio_capability_bit(RadioCapability::SetSplit);
  return result;
}

}  // namespace cwassistant::core
