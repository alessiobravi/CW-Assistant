#include "cwassistant/core/hardware_acceptance.hpp"

#include <algorithm>
#include <utility>

namespace cwassistant::core {
namespace {

bool isLowerHex(const char value) noexcept {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

}  // namespace

bool HardwareAcceptanceWorkflow::validFingerprint(
    const std::string& value) noexcept {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), isLowerHex);
}

bool HardwareAcceptanceWorkflow::validRecordPrefix(
    const HardwareAcceptanceRecord& record) noexcept {
  if (record.schema_version != 1 ||
      !validFingerprint(record.configuration_sha256) ||
      record.platform == HardwareAcceptancePlatform::Unknown) {
    return false;
  }
  if (record.disconnected_line_utc_seconds == 0) {
    return record.physical_loopback_utc_seconds == 0 &&
           record.dummy_load_utc_seconds == 0;
  }
  if (record.physical_loopback_utc_seconds == 0) {
    return record.dummy_load_utc_seconds == 0;
  }
  if (record.physical_loopback_utc_seconds <
      record.disconnected_line_utc_seconds) {
    return false;
  }
  return record.dummy_load_utc_seconds == 0 ||
         record.dummy_load_utc_seconds >= record.physical_loopback_utc_seconds;
}

bool HardwareAcceptanceWorkflow::setConfiguration(
    std::string configuration_sha256,
    const HardwareAcceptancePlatform platform) noexcept {
  if (!validFingerprint(configuration_sha256) ||
      platform == HardwareAcceptancePlatform::Unknown) {
    clear();
    fail(HardwareAcceptanceFailure::InvalidConfigurationFingerprint);
    return false;
  }
  if (record_.configuration_sha256 == configuration_sha256 &&
      record_.platform == platform) {
    succeed();
    return true;
  }
  record_ = {};
  record_.configuration_sha256 = std::move(configuration_sha256);
  record_.platform = platform;
  succeed();
  return true;
}

void HardwareAcceptanceWorkflow::clear() noexcept {
  record_ = {};
  last_failure_ = HardwareAcceptanceFailure::None;
}

bool HardwareAcceptanceWorkflow::confirmDisconnectedLine(
    const DisconnectedLineEvidence& evidence,
    const std::uint64_t observed_utc_seconds) noexcept {
  if (status() != HardwareAcceptanceStatus::DisconnectedLineRequired) {
    fail(HardwareAcceptanceFailure::WrongStep);
    return false;
  }
  if (observed_utc_seconds == 0) {
    fail(HardwareAcceptanceFailure::InvalidTimestamp);
    return false;
  }
  if (!evidence.radio_disconnected) {
    fail(HardwareAcceptanceFailure::RadioMustBeDisconnected);
    return false;
  }
  if (!evidence.ptt_inactive_observed || !evidence.key_inactive_observed) {
    fail(HardwareAcceptanceFailure::InactiveLinesNotObserved);
    return false;
  }
  if (!evidence.independent_release_available) {
    fail(HardwareAcceptanceFailure::IndependentReleaseUnavailable);
    return false;
  }
  record_.disconnected_line_utc_seconds = observed_utc_seconds;
  succeed();
  return true;
}

bool HardwareAcceptanceWorkflow::confirmPhysicalLoopback(
    const PhysicalLoopbackEvidence& evidence,
    const std::uint64_t observed_utc_seconds) noexcept {
  if (status() != HardwareAcceptanceStatus::PhysicalLoopbackRequired) {
    fail(HardwareAcceptanceFailure::WrongStep);
    return false;
  }
  if (observed_utc_seconds < record_.disconnected_line_utc_seconds) {
    fail(HardwareAcceptanceFailure::InvalidTimestamp);
    return false;
  }
  if (!evidence.radio_disconnected) {
    fail(HardwareAcceptanceFailure::RadioMustBeDisconnected);
    return false;
  }
  if (!evidence.ptt_transition_observed || !evidence.key_transition_observed) {
    fail(HardwareAcceptanceFailure::LoopbackTransitionsNotObserved);
    return false;
  }
  if (!evidence.both_lines_returned_inactive) {
    fail(HardwareAcceptanceFailure::LoopbackDidNotReturnInactive);
    return false;
  }
  record_.physical_loopback_utc_seconds = observed_utc_seconds;
  succeed();
  return true;
}

bool HardwareAcceptanceWorkflow::confirmDummyLoad(
    const DummyLoadEvidence& evidence,
    const std::uint64_t observed_utc_seconds) noexcept {
  if (status() != HardwareAcceptanceStatus::DummyLoadRequired) {
    fail(HardwareAcceptanceFailure::WrongStep);
    return false;
  }
  if (observed_utc_seconds < record_.physical_loopback_utc_seconds) {
    fail(HardwareAcceptanceFailure::InvalidTimestamp);
    return false;
  }
  if (!evidence.dummy_load_connected) {
    fail(HardwareAcceptanceFailure::DummyLoadNotConfirmed);
    return false;
  }
  if (!evidence.minimum_power_selected) {
    fail(HardwareAcceptanceFailure::MinimumPowerNotConfirmed);
    return false;
  }
  if (!evidence.message_keying_observed) {
    fail(HardwareAcceptanceFailure::MessageKeyingNotObserved);
    return false;
  }
  if (!evidence.cancellation_release_observed) {
    fail(HardwareAcceptanceFailure::CancellationReleaseNotObserved);
    return false;
  }
  if (!evidence.watchdog_release_observed) {
    fail(HardwareAcceptanceFailure::WatchdogReleaseNotObserved);
    return false;
  }
  if (!evidence.emergency_release_observed) {
    fail(HardwareAcceptanceFailure::EmergencyReleaseNotObserved);
    return false;
  }
  record_.dummy_load_utc_seconds = observed_utc_seconds;
  succeed();
  return true;
}

bool HardwareAcceptanceWorkflow::restore(
    const HardwareAcceptanceRecord& record) noexcept {
  if (!validRecordPrefix(record)) {
    clearEvidence();
    fail(HardwareAcceptanceFailure::StoredRecordInvalid);
    return false;
  }
  if (record.configuration_sha256 != record_.configuration_sha256 ||
      record.platform != record_.platform) {
    clearEvidence();
    fail(HardwareAcceptanceFailure::StoredRecordDoesNotMatchConfiguration);
    return false;
  }
  record_ = record;
  succeed();
  return true;
}

HardwareAcceptanceStatus HardwareAcceptanceWorkflow::status() const noexcept {
  if (!validFingerprint(record_.configuration_sha256) ||
      record_.platform == HardwareAcceptancePlatform::Unknown) {
    return HardwareAcceptanceStatus::ConfigurationRequired;
  }
  if (record_.disconnected_line_utc_seconds == 0) {
    return HardwareAcceptanceStatus::DisconnectedLineRequired;
  }
  if (record_.physical_loopback_utc_seconds == 0) {
    return HardwareAcceptanceStatus::PhysicalLoopbackRequired;
  }
  if (record_.dummy_load_utc_seconds == 0) {
    return HardwareAcceptanceStatus::DummyLoadRequired;
  }
  return HardwareAcceptanceStatus::Accepted;
}

bool HardwareAcceptanceWorkflow::readyForSafeHardwareOpen() const noexcept {
  const auto current = status();
  return current == HardwareAcceptanceStatus::DummyLoadRequired ||
         current == HardwareAcceptanceStatus::Accepted;
}

bool HardwareAcceptanceWorkflow::acceptedForOnAirUse() const noexcept {
  return status() == HardwareAcceptanceStatus::Accepted;
}

void HardwareAcceptanceWorkflow::clearEvidence() noexcept {
  record_.disconnected_line_utc_seconds = 0;
  record_.physical_loopback_utc_seconds = 0;
  record_.dummy_load_utc_seconds = 0;
}

void HardwareAcceptanceWorkflow::fail(
    const HardwareAcceptanceFailure failure) noexcept {
  last_failure_ = failure;
}

void HardwareAcceptanceWorkflow::succeed() noexcept {
  last_failure_ = HardwareAcceptanceFailure::None;
}

}  // namespace cwassistant::core
