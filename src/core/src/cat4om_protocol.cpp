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

}  // namespace cwassistant::core
