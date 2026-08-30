#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/frequency_plan.hpp"

namespace cwassistant::core {

inline constexpr std::string_view kCat4OmProtocolVersion{"1.0.0"};

enum class Cat4OmRole { Observer, Slave, Master, Unknown };

struct Cat4OmVfoState {
  std::string id;
  std::uint64_t frequency_hz{0};
};

struct Cat4OmRadioState {
  std::string radio_id;
  std::string connection_status;
  std::string active_vfo;
  std::string tx_vfo;
  bool split{false};
  std::vector<Cat4OmVfoState> vfos;
  std::vector<std::string> available_commands;
};

[[nodiscard]] std::optional<unsigned int> protocol_major(
    std::string_view version) noexcept;
[[nodiscard]] bool cat4om_protocol_compatible(
    std::string_view version) noexcept;
[[nodiscard]] Cat4OmRole cat4om_role_from_string(
    std::string_view role) noexcept;
[[nodiscard]] bool cat4om_has_command(const Cat4OmRadioState& state,
                                      std::string_view command) noexcept;
[[nodiscard]] std::optional<VfoFrequencyPlan> cat4om_frequency_plan(
    const Cat4OmRadioState& state) noexcept;

}  // namespace cwassistant::core
