#include "cwassistant/core/transmit_guard.hpp"

#include <algorithm>
#include <cctype>

namespace cwassistant::core {
namespace {

bool plausible_callsign(const std::string_view value) {
  if (value.size() < 3 || value.size() > 16) {
    return false;
  }
  const auto has_letter = std::ranges::any_of(value, [](const unsigned char c) {
    return std::isalpha(c) != 0;
  });
  const auto has_digit = std::ranges::any_of(value, [](const unsigned char c) {
    return std::isdigit(c) != 0;
  });
  const auto valid = std::ranges::all_of(value, [](const unsigned char c) {
    return std::isalnum(c) != 0 || c == '/';
  });
  return has_letter && has_digit && valid;
}

}  // namespace

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
  if (state_ != TransmitState::Armed || !plausible_callsign(callsign)) {
    return false;
  }
  std::ranges::transform(callsign, callsign.begin(), [](const unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  pending_callsign_ = std::move(callsign);
  state_ = TransmitState::AwaitingConfirmation;
  return true;
}

bool TransmitGuard::confirm(const std::string_view callsign) noexcept {
  if (state_ != TransmitState::AwaitingConfirmation ||
      callsign.size() != pending_callsign_.size()) {
    return false;
  }
  const auto matches = std::ranges::equal(
      callsign, pending_callsign_, {},
      [](const unsigned char c) { return static_cast<char>(std::toupper(c)); },
      [](const char c) { return c; });
  if (!matches) {
    return false;
  }
  state_ = TransmitState::Confirmed;
  return true;
}

bool TransmitGuard::begin_transmission() noexcept {
  if (state_ != TransmitState::Confirmed) {
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
