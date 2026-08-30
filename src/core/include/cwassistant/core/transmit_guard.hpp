#pragma once

#include <string>
#include <string_view>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {

enum class TransmitState {
  Disarmed,
  Armed,
  AwaitingConfirmation,
  Confirmed,
  Transmitting,
  Fault,
};

class TransmitGuard {
 public:
  explicit TransmitGuard(const CallsignPolicy& callsign_policy) noexcept
      : callsign_policy_(callsign_policy) {}

  [[nodiscard]] TransmitState state() const noexcept { return state_; }
  [[nodiscard]] std::string_view state_name() const noexcept;
  [[nodiscard]] std::string_view pending_callsign() const noexcept {
    return pending_callsign_;
  }

  [[nodiscard]] bool arm() noexcept;
  void disarm() noexcept;
  [[nodiscard]] bool request_qso(std::string callsign);
  [[nodiscard]] bool confirm(std::string_view callsign);
  [[nodiscard]] bool begin_transmission();
  [[nodiscard]] bool finish_transmission() noexcept;
  void trip_fault() noexcept;
  [[nodiscard]] bool reset_fault() noexcept;

 private:
  const CallsignPolicy& callsign_policy_;
  TransmitState state_{TransmitState::Disarmed};
  std::string pending_callsign_{};
};

}  // namespace cwassistant::core
