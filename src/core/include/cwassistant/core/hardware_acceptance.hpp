#pragma once

#include <cstdint>
#include <string>

namespace cwassistant::core {

enum class HardwareAcceptancePlatform {
  Unknown,
  Windows,
  MacOS,
  Linux,
};

enum class HardwareAcceptanceStatus {
  ConfigurationRequired,
  DisconnectedLineRequired,
  PhysicalLoopbackRequired,
  DummyLoadRequired,
  Accepted,
};

enum class HardwareAcceptanceFailure {
  None,
  InvalidConfigurationFingerprint,
  InvalidTimestamp,
  WrongStep,
  RadioMustBeDisconnected,
  InactiveLinesNotObserved,
  IndependentReleaseUnavailable,
  LoopbackTransitionsNotObserved,
  LoopbackDidNotReturnInactive,
  DummyLoadNotConfirmed,
  MinimumPowerNotConfirmed,
  MessageKeyingNotObserved,
  CancellationReleaseNotObserved,
  WatchdogReleaseNotObserved,
  EmergencyReleaseNotObserved,
  StoredRecordInvalid,
  StoredRecordDoesNotMatchConfiguration,
};

struct DisconnectedLineEvidence {
  bool radio_disconnected{false};
  bool ptt_inactive_observed{false};
  bool key_inactive_observed{false};
  bool independent_release_available{false};
};

struct PhysicalLoopbackEvidence {
  bool radio_disconnected{false};
  bool ptt_transition_observed{false};
  bool key_transition_observed{false};
  bool both_lines_returned_inactive{false};
};

struct DummyLoadEvidence {
  bool dummy_load_connected{false};
  bool minimum_power_selected{false};
  bool message_keying_observed{false};
  bool cancellation_release_observed{false};
  bool watchdog_release_observed{false};
  bool emergency_release_observed{false};
};

// This record deliberately contains no device path, callsign, operator name,
// free-form note, or electrical measurement. The fingerprint is a lowercase
// SHA-256 digest computed by the platform adapter from the effective keying
// configuration.
struct HardwareAcceptanceRecord {
  std::uint32_t schema_version{1};
  std::string configuration_sha256;
  HardwareAcceptancePlatform platform{HardwareAcceptancePlatform::Unknown};
  std::uint64_t disconnected_line_utc_seconds{0};
  std::uint64_t physical_loopback_utc_seconds{0};
  std::uint64_t dummy_load_utc_seconds{0};
};

// A passive, operator-evidence state machine. It never opens a port, changes a
// control line, keys a transmitter, or infers that a physical observation was
// made. Callers may expose each transition only after the operator supplies all
// evidence required for that ordered step.
class HardwareAcceptanceWorkflow final {
 public:
  [[nodiscard]] bool setConfiguration(
      std::string configuration_sha256,
      HardwareAcceptancePlatform platform) noexcept;
  void clear() noexcept;

  [[nodiscard]] bool confirmDisconnectedLine(
      const DisconnectedLineEvidence& evidence,
      std::uint64_t observed_utc_seconds) noexcept;
  [[nodiscard]] bool confirmPhysicalLoopback(
      const PhysicalLoopbackEvidence& evidence,
      std::uint64_t observed_utc_seconds) noexcept;
  [[nodiscard]] bool confirmDummyLoad(
      const DummyLoadEvidence& evidence,
      std::uint64_t observed_utc_seconds) noexcept;

  // Restore is fail-closed and only accepts a complete, ordered prefix of the
  // workflow for the exact already-selected configuration and platform.
  [[nodiscard]] bool restore(const HardwareAcceptanceRecord& record) noexcept;

  [[nodiscard]] HardwareAcceptanceStatus status() const noexcept;
  [[nodiscard]] HardwareAcceptanceFailure lastFailure() const noexcept {
    return last_failure_;
  }
  [[nodiscard]] bool readyForSafeHardwareOpen() const noexcept;
  [[nodiscard]] bool acceptedForOnAirUse() const noexcept;
  [[nodiscard]] const HardwareAcceptanceRecord& record() const noexcept {
    return record_;
  }

 private:
  [[nodiscard]] static bool validFingerprint(const std::string& value) noexcept;
  [[nodiscard]] static bool validRecordPrefix(
      const HardwareAcceptanceRecord& record) noexcept;
  void clearEvidence() noexcept;
  void fail(HardwareAcceptanceFailure failure) noexcept;
  void succeed() noexcept;

  HardwareAcceptanceRecord record_;
  HardwareAcceptanceFailure last_failure_{HardwareAcceptanceFailure::None};
};

}  // namespace cwassistant::core
