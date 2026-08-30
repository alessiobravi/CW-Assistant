#include "cwassistant/core/transmit_guard.hpp"

namespace cwassistant::core {

std::string_view TransmitGuard::state_name() const noexcept {
  switch (state_) {
    case TransmitState::Disarmed: return "disarmed";
    case TransmitState::Armed: return "armed";
    case TransmitState::AwaitingConfirmation: return "awaiting-confirmation";
    case TransmitState::Confirmed: return "confirmed";
    case TransmitState::Transmitting: return "transmitting";
    case TransmitState::Fault: return "fault";
  }
  return "unknown";
}

bool TransmitGuard::arm() noexcept {
  if (state_ != TransmitState::Disarmed) {
    return false;
  }
  state_ = TransmitState::Armed;
  return true;
}

void TransmitGuard::disarm() noexcept {
  pending_callsign_.clear();
  state_ = TransmitState::Disarmed;
}

bool TransmitGuard::request_qso(std::string callsign) {
  const auto normalized = CallsignPolicy::normalize(callsign);
  if (state_ != TransmitState::Armed || !normalized.has_value() ||
      callsign_policy_.is_ignored(*normalized)) {
    return false;
  }
  pending_callsign_ = *normalized;
  state_ = TransmitState::AwaitingConfirmation;
  return true;
}

bool TransmitGuard::confirm(const std::string_view callsign) {
  const auto normalized = CallsignPolicy::normalize(callsign);
  if (state_ != TransmitState::AwaitingConfirmation || !normalized.has_value() ||
      *normalized != pending_callsign_) {
    return false;
  }
  if (callsign_policy_.is_ignored(pending_callsign_)) {
    pending_callsign_.clear();
    state_ = TransmitState::Armed;
    return false;
  }
  state_ = TransmitState::Confirmed;
  return true;
}

bool TransmitGuard::begin_transmission() {
  if (state_ != TransmitState::Confirmed) {
    return false;
  }
  if (callsign_policy_.is_ignored(pending_callsign_)) {
    pending_callsign_.clear();
    state_ = TransmitState::Armed;
    return false;
  }
  state_ = TransmitState::Transmitting;
  return true;
}

bool TransmitGuard::finish_transmission() noexcept {
  if (state_ != TransmitState::Transmitting) {
    return false;
  }
  pending_callsign_.clear();
  state_ = TransmitState::Armed;
  return true;
}

void TransmitGuard::trip_fault() noexcept {
  pending_callsign_.clear();
  state_ = TransmitState::Fault;
}

bool TransmitGuard::reset_fault() noexcept {
  if (state_ != TransmitState::Fault) {
    return false;
  }
  state_ = TransmitState::Disarmed;
  return true;
}

}  // namespace cwassistant::core
