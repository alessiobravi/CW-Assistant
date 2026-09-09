#pragma once

#include <QString>

#include <memory>

#include "transmit/direct_keying_adapter.hpp"

namespace cwassistant::desktop {

struct DirectKeyingSenseState {
  bool cts{false};
  bool dsr{false};
};

class DirectKeyingAcceptanceBackend {
 public:
  virtual ~DirectKeyingAcceptanceBackend() = default;
  virtual bool acquire(const QString& port_name, QString& error) = 0;
  virtual bool open(QString& error) = 0;
  virtual bool setOutput(DirectKeyingLine line, bool high, QString& error) = 0;
  virtual bool readInputs(DirectKeyingSenseState& state, QString& error) = 0;
  virtual void waitForSettling() noexcept = 0;
  virtual void close() noexcept = 0;
  virtual void releaseOwnership() noexcept = 0;
};

enum class DirectKeyingProbeFailure {
  None,
  RadioDisconnectNotConfirmed,
  InvalidConfiguration,
  OwnershipUnavailable,
  PortOpenFailed,
  InactiveInitializationFailed,
  InputsActiveAtBaseline,
  PttAssertionFailed,
  PttLoopbackNotObserved,
  PttReleaseFailed,
  KeyAssertionFailed,
  KeyLoopbackNotObserved,
  KeyReleaseFailed,
  FinalInactiveNotObserved,
};

struct DirectKeyingProbeResult {
  bool passed{false};
  bool ptt_transition_observed{false};
  bool key_transition_observed{false};
  bool outputs_released{false};
  bool inputs_inactive_after_release{false};
  DirectKeyingProbeFailure failure{DirectKeyingProbeFailure::None};
  QString detail;
};

// Performs an electrical loopback observation only after a separate explicit
// operator confirmation that the radio is disconnected. The probe never
// enumerates ports. It opens exactly config.port_name, tests one output at a
// time (RTS->CTS, DTR->DSR), then releases KEY before PTT and closes ownership.
class DirectKeyingAcceptanceProbe final {
 public:
  DirectKeyingAcceptanceProbe();
  explicit DirectKeyingAcceptanceProbe(
      std::unique_ptr<DirectKeyingAcceptanceBackend> backend);
  ~DirectKeyingAcceptanceProbe();

  DirectKeyingAcceptanceProbe(const DirectKeyingAcceptanceProbe&) = delete;
  DirectKeyingAcceptanceProbe& operator=(
      const DirectKeyingAcceptanceProbe&) = delete;

  [[nodiscard]] DirectKeyingProbeResult run(
      const DirectKeyingConfig& config,
      bool radio_disconnected_confirmed);

 private:
  [[nodiscard]] bool waitForSense(DirectKeyingLine output,
                                  bool expected_active,
                                  QString& error) noexcept;
  void releaseAndClose(DirectKeyingProbeResult& result) noexcept;

  std::unique_ptr<DirectKeyingAcceptanceBackend> backend_;
  DirectKeyingConfig config_;
  bool ownership_acquired_{false};
  bool port_open_{false};
};

// Stable privacy-preserving identity for acceptance invalidation. It includes
// the effective port, line assignments, polarities and a schema discriminator,
// but returns only a lowercase SHA-256 digest suitable for persistence.
[[nodiscard]] QString directKeyingConfigurationSha256(
    const DirectKeyingConfig& config);

}  // namespace cwassistant::desktop
