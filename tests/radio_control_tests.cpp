#include <cstdlib>
#include <iostream>
#include <string>

#include "cwassistant/core/radio_control.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

cwassistant::core::RadioState writable_radio() {
  using namespace cwassistant::core;
  return RadioState{
      .availability = RadioObservation::Known,
      .rx_frequency = {RadioObservation::Known, 14'025'000U},
      .tx_frequency = {RadioObservation::Known, 14'027'500U},
      .rx_mode = {RadioObservation::Known, RadioMode::Cw},
      .tx_mode = {RadioObservation::Known, RadioMode::CwReverse},
      .rx_vfo = {RadioObservation::Known, "VFO-A"},
      .tx_vfo = {RadioObservation::Known, "VFO-B"},
      .split = {RadioObservation::Known, RadioSplit::Enabled},
      .capabilities = {
          RadioObservation::Known,
          RadioCapability::SetRxFrequency | RadioCapability::SetTxFrequency |
              RadioCapability::SetRxMode | RadioCapability::SetTxMode |
              RadioCapability::SelectRxVfo |
              RadioCapability::SelectTxVfo | RadioCapability::SetSplit},
  };
}

void test_complete_state_and_capabilities() {
  using namespace cwassistant::core;
  const auto state = writable_radio();
  expect(radio_state_is_valid(state), "complete state is valid");
  expect(state.rx_frequency.hz == 14'025'000U &&
             state.tx_frequency.hz == 14'027'500U,
         "RX and TX frequencies remain distinct");
  expect(state.rx_mode.mode == RadioMode::Cw &&
             state.tx_mode.mode == RadioMode::CwReverse,
         "RX and TX modes remain distinct");
  expect(state.rx_vfo.identifier == "VFO-A" &&
             state.tx_vfo.identifier == "VFO-B" &&
             state.split.split == RadioSplit::Enabled,
         "VFO and split state retain authoritative values");
  expect(radio_has_capability(state.capabilities,
                              RadioCapability::SetTxFrequency),
         "known advertised capability is available");
  expect(!radio_has_capability({RadioObservation::Unknown, 0U},
                               RadioCapability::SetTxFrequency),
         "unknown capability state never grants a command");
}

void test_mode_tokens_are_provider_neutral() {
  using namespace cwassistant::core;
  expect(radio_mode_from_token("usb") == RadioMode::UpperSideband &&
             radio_mode_from_token("CW-L") == RadioMode::CwReverse &&
             radio_mode_from_token("PKTUSB") == RadioMode::DigitalUpper &&
             radio_mode_from_token("invented") == RadioMode::Unknown,
         "provider mode spellings normalize without guessing unknown values");
  expect(radio_mode_token(RadioMode::CwReverse) == "CW-R" &&
             radio_mode_token(RadioMode::Unknown) == "?",
         "radio modes have stable UI tokens");
}

void test_unknown_and_unavailable_are_not_fabricated() {
  using namespace cwassistant::core;
  RadioState state;
  expect(radio_state_is_valid(state), "fully unknown state is representable");
  expect(state.rx_frequency.observation == RadioObservation::Unknown &&
             state.tx_frequency.observation == RadioObservation::Unknown &&
             state.rx_mode.mode == RadioMode::Unknown &&
             state.tx_mode.mode == RadioMode::Unknown &&
             state.split.split == RadioSplit::Unknown,
         "default state fabricates no frequency, mode, or simplex state");

  state.availability = RadioObservation::Unavailable;
  state.rx_frequency.observation = RadioObservation::Unavailable;
  state.tx_frequency.observation = RadioObservation::Unavailable;
  state.rx_mode.observation = RadioObservation::Unavailable;
  state.tx_mode.observation = RadioObservation::Unavailable;
  state.rx_vfo.observation = RadioObservation::Unavailable;
  state.tx_vfo.observation = RadioObservation::Unavailable;
  state.split.observation = RadioObservation::Unavailable;
  state.capabilities.observation = RadioObservation::Unavailable;
  expect(radio_state_is_valid(state), "unavailable state is explicit and valid");
  expect(validate_radio_command(state, SetSplit{false}) ==
             RadioCommandValidation::RadioUnavailable,
         "unavailable radio rejects even a simplex request");
}

void test_invalid_state_combinations() {
  using namespace cwassistant::core;
  auto state = writable_radio();
  state.tx_frequency = {RadioObservation::Unknown, 14'027'500U};
  expect(!radio_state_is_valid(state),
         "unknown frequency cannot carry a fabricated value");
  state = writable_radio();
  state.rx_mode = {RadioObservation::Known, RadioMode::Unknown};
  expect(!radio_state_is_valid(state), "known mode cannot be Unknown");
  state = writable_radio();
  state.split = {RadioObservation::Known, RadioSplit::Unknown};
  expect(!radio_state_is_valid(state), "known split cannot be Unknown");
  state = writable_radio();
  state.availability = RadioObservation::Unavailable;
  expect(!radio_state_is_valid(state),
         "unavailable radio cannot retain known observations");
  state = writable_radio();
  state.capabilities = {RadioObservation::Unknown,
                        radio_capability_bit(RadioCapability::SetRxMode)};
  expect(!radio_state_is_valid(state),
         "unknown capabilities cannot retain effective bits");
  state = writable_radio();
  state.capabilities.bits |= 1U << 31U;
  expect(!radio_state_is_valid(state),
         "capability bits are bounded to the defined provider contract");
}

void test_every_command_and_capability() {
  using namespace cwassistant::core;
  const auto state = writable_radio();
  expect(validate_radio_command(state, SetRxFrequency{7'030'000U}) ==
             RadioCommandValidation::Valid,
         "RX frequency command validates");
  expect(validate_radio_command(state, SetTxFrequency{7'031'000U}) ==
             RadioCommandValidation::Valid,
         "TX frequency command validates");
  expect(validate_radio_command(state, SetRxMode{RadioMode::UpperSideband}) ==
             RadioCommandValidation::Valid,
         "RX mode command validates");
  expect(validate_radio_command(state, SetTxMode{RadioMode::DigitalLower}) ==
             RadioCommandValidation::Valid,
         "TX mode command validates");
  expect(validate_radio_command(state, SelectRxVfo{"MAIN"}) ==
             RadioCommandValidation::Valid,
         "RX VFO command validates");
  expect(validate_radio_command(state, SelectTxVfo{"SUB"}) ==
             RadioCommandValidation::Valid,
         "TX VFO command validates");
  expect(validate_radio_command(state, SetSplit{false}) ==
             RadioCommandValidation::Valid,
         "split command validates without inferring frequency or mode");

  auto read_only = state;
  read_only.capabilities.bits = 0U;
  expect(validate_radio_command(read_only, SetRxFrequency{7'030'000U}) ==
             RadioCommandValidation::Unsupported &&
             validate_radio_command(read_only, SetTxMode{RadioMode::Cw}) ==
                 RadioCommandValidation::Unsupported &&
             validate_radio_command(read_only, SetSplit{true}) ==
                 RadioCommandValidation::Unsupported,
         "read-only capability set rejects all writes");
}

void test_invalid_commands_and_indeterminate_capabilities() {
  using namespace cwassistant::core;
  auto state = writable_radio();
  expect(validate_radio_command(state, SetRxFrequency{0U}) ==
             RadioCommandValidation::InvalidFrequency,
         "zero frequency is invalid");
  expect(validate_radio_command(state, SetTxMode{RadioMode::Unknown}) ==
             RadioCommandValidation::InvalidMode,
         "Unknown is not a writable mode");
  expect(validate_radio_command(state, SelectRxVfo{""}) ==
             RadioCommandValidation::InvalidVfoIdentifier,
         "empty VFO identifier is invalid");
  expect(validate_radio_command(
             state, SelectTxVfo{std::string(
                        kMaximumRadioVfoIdentifierLength + 1U, 'A')}) ==
             RadioCommandValidation::InvalidVfoIdentifier,
         "VFO identifier length is bounded");
  expect(validate_radio_command(state, SelectTxVfo{"VFO B"}) ==
             RadioCommandValidation::InvalidVfoIdentifier,
         "VFO identifier rejects whitespace");

  state.availability = RadioObservation::Unknown;
  expect(validate_radio_command(state, SetSplit{true}) ==
             RadioCommandValidation::RadioStateUnknown,
         "unknown radio availability cannot authorize a write");
  state.availability = RadioObservation::Known;
  state.capabilities = {RadioObservation::Unknown, 0U};
  expect(validate_radio_command(state, SetSplit{true}) ==
             RadioCommandValidation::CapabilitiesUnknown,
         "unknown capabilities cannot authorize a write");
  state.capabilities = {RadioObservation::Unavailable, 0U};
  expect(validate_radio_command(state, SetSplit{true}) ==
             RadioCommandValidation::CapabilitiesUnavailable,
         "unavailable capabilities cannot authorize a write");

  state = writable_radio();
  state.tx_mode = {RadioObservation::Known, RadioMode::Unknown};
  expect(validate_radio_command(state, SetSplit{true}) ==
             RadioCommandValidation::InvalidState,
         "an inconsistent provider snapshot cannot authorize a write");
}

}  // namespace

int main() {
  test_complete_state_and_capabilities();
  test_mode_tokens_are_provider_neutral();
  test_unknown_and_unavailable_are_not_fabricated();
  test_invalid_state_combinations();
  test_every_command_and_capability();
  test_invalid_commands_and_indeterminate_capabilities();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
