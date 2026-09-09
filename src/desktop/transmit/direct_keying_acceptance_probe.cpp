#include "transmit/direct_keying_acceptance_probe.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QSerialPort>
#include <QStandardPaths>
#include <QThread>

#include <array>
#include <memory>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr int kMaximumSenseAttempts = 20;

QString lockPathForPort(const QString& port_name) {
  QString directory =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (directory.isEmpty()) {
    directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  }
  const QByteArray identity = QCryptographicHash::hash(
      port_name.toUtf8(), QCryptographicHash::Sha256).toHex();
  return QDir(directory).filePath(
      QStringLiteral("cw-buddy-direct-keying-%1.lock")
          .arg(QString::fromLatin1(identity)));
}

class QtSerialAcceptanceBackend final
    : public DirectKeyingAcceptanceBackend {
 public:
  bool acquire(const QString& port_name, QString& error) override {
    if (lock_) {
      error = QStringLiteral("Serial keying ownership is already held");
      return false;
    }
    auto candidate = std::make_unique<QLockFile>(lockPathForPort(port_name));
    candidate->setStaleLockTime(30'000);
    if (!candidate->tryLock(0)) {
      error = QStringLiteral(
          "The selected serial keying port is owned by another process");
      return false;
    }
    port_name_ = port_name;
    lock_ = std::move(candidate);
    return true;
  }

  bool open(QString& error) override {
    if (!lock_) {
      error = QStringLiteral("Serial keying ownership was not acquired");
      return false;
    }
    serial_.setPortName(port_name_);
    if (!serial_.setFlowControl(QSerialPort::NoFlowControl)) {
      error = QStringLiteral("Cannot disable serial flow control: %1")
                  .arg(serial_.errorString());
      return false;
    }
    if (!serial_.open(QIODevice::ReadWrite)) {
      error = QStringLiteral("Cannot open the selected serial keying port: %1")
                  .arg(serial_.errorString());
      return false;
    }
    return true;
  }

  bool setOutput(const DirectKeyingLine line, const bool high,
                 QString& error) override {
    const bool changed = line == DirectKeyingLine::Rts
        ? serial_.setRequestToSend(high)
        : serial_.setDataTerminalReady(high);
    if (!changed) {
      error = QStringLiteral("Cannot set serial %1: %2")
                  .arg(line == DirectKeyingLine::Rts
                           ? QStringLiteral("RTS")
                           : QStringLiteral("DTR"),
                       serial_.errorString());
    }
    return changed;
  }

  bool readInputs(DirectKeyingSenseState& state, QString& error) override {
    if (!serial_.isOpen()) {
      error = QStringLiteral("The serial keying port is not open");
      return false;
    }
    const auto pinouts = serial_.pinoutSignals();
    if (serial_.error() != QSerialPort::NoError) {
      error = QStringLiteral("Cannot read serial input signals: %1")
                  .arg(serial_.errorString());
      return false;
    }
    state.cts = pinouts.testFlag(QSerialPort::ClearToSendSignal);
    state.dsr = pinouts.testFlag(QSerialPort::DataSetReadySignal);
    return true;
  }

  void waitForSettling() noexcept override { QThread::msleep(5); }
  void close() noexcept override {
    if (serial_.isOpen()) serial_.close();
  }
  void releaseOwnership() noexcept override {
    if (!lock_) return;
    lock_->unlock();
    lock_.reset();
    port_name_.clear();
  }

 private:
  QSerialPort serial_;
  std::unique_ptr<QLockFile> lock_;
  QString port_name_;
};

bool validConfig(const DirectKeyingConfig& config) {
  return !config.port_name.trimmed().isEmpty() &&
         config.ptt_line != config.key_line && config.ptt_active_high &&
         config.key_active_high;
}

QString defaultDetail(const QString& detail, const QString& fallback) {
  return detail.isEmpty() ? fallback : detail;
}

}  // namespace

DirectKeyingAcceptanceProbe::DirectKeyingAcceptanceProbe()
    : DirectKeyingAcceptanceProbe(
          std::make_unique<QtSerialAcceptanceBackend>()) {}

DirectKeyingAcceptanceProbe::DirectKeyingAcceptanceProbe(
    std::unique_ptr<DirectKeyingAcceptanceBackend> backend)
    : backend_(std::move(backend)) {}

DirectKeyingAcceptanceProbe::~DirectKeyingAcceptanceProbe() {
  DirectKeyingProbeResult ignored;
  releaseAndClose(ignored);
}

DirectKeyingProbeResult DirectKeyingAcceptanceProbe::run(
    const DirectKeyingConfig& config,
    const bool radio_disconnected_confirmed) {
  DirectKeyingProbeResult result;
  releaseAndClose(result);
  result = {};
  if (!radio_disconnected_confirmed) {
    result.failure = DirectKeyingProbeFailure::RadioDisconnectNotConfirmed;
    result.detail = QStringLiteral(
        "Disconnect the radio and explicitly confirm that state before probing");
    return result;
  }
  if (!backend_ || !validConfig(config)) {
    result.failure = DirectKeyingProbeFailure::InvalidConfiguration;
    result.detail = QStringLiteral(
        "Select one explicit port with distinct active-high PTT and KEY lines");
    return result;
  }
  config_ = config;
  QString error;
  if (!backend_->acquire(config.port_name, error)) {
    result.failure = DirectKeyingProbeFailure::OwnershipUnavailable;
    result.detail = defaultDetail(
        error, QStringLiteral("Cannot acquire exclusive keying-port ownership"));
    return result;
  }
  ownership_acquired_ = true;
  if (!backend_->open(error)) {
    result.failure = DirectKeyingProbeFailure::PortOpenFailed;
    result.detail = defaultDetail(
        error, QStringLiteral("Cannot open the selected keying port"));
    releaseAndClose(result);
    return result;
  }
  port_open_ = true;

  // Establish a known inactive state before observing any input or asserting
  // either output. Release order is always KEY then PTT.
  if (!backend_->setOutput(config.key_line, false, error) ||
      !backend_->setOutput(config.ptt_line, false, error)) {
    result.failure = DirectKeyingProbeFailure::InactiveInitializationFailed;
    result.detail = defaultDetail(
        error, QStringLiteral("Cannot initialize keying outputs inactive"));
    releaseAndClose(result);
    return result;
  }
  if (!waitForSense(config.ptt_line, false, error) ||
      !waitForSense(config.key_line, false, error)) {
    result.failure = DirectKeyingProbeFailure::InputsActiveAtBaseline;
    result.detail = defaultDetail(
        error, QStringLiteral("CTS or DSR is active at the inactive baseline"));
    releaseAndClose(result);
    return result;
  }

  if (!backend_->setOutput(config.ptt_line, true, error)) {
    result.failure = DirectKeyingProbeFailure::PttAssertionFailed;
    result.detail = defaultDetail(error, QStringLiteral("Cannot assert PTT output"));
    releaseAndClose(result);
    return result;
  }
  if (!waitForSense(config.ptt_line, true, error)) {
    result.failure = DirectKeyingProbeFailure::PttLoopbackNotObserved;
    result.detail = defaultDetail(
        error, QStringLiteral("Expected PTT loopback transition was not observed"));
    releaseAndClose(result);
    return result;
  }
  result.ptt_transition_observed = true;
  if (!backend_->setOutput(config.ptt_line, false, error) ||
      !waitForSense(config.ptt_line, false, error)) {
    result.failure = DirectKeyingProbeFailure::PttReleaseFailed;
    result.detail = defaultDetail(
        error, QStringLiteral("PTT did not return to inactive"));
    releaseAndClose(result);
    return result;
  }

  if (!backend_->setOutput(config.key_line, true, error)) {
    result.failure = DirectKeyingProbeFailure::KeyAssertionFailed;
    result.detail = defaultDetail(error, QStringLiteral("Cannot assert KEY output"));
    releaseAndClose(result);
    return result;
  }
  if (!waitForSense(config.key_line, true, error)) {
    result.failure = DirectKeyingProbeFailure::KeyLoopbackNotObserved;
    result.detail = defaultDetail(
        error, QStringLiteral("Expected KEY loopback transition was not observed"));
    releaseAndClose(result);
    return result;
  }
  result.key_transition_observed = true;
  if (!backend_->setOutput(config.key_line, false, error) ||
      !waitForSense(config.key_line, false, error)) {
    result.failure = DirectKeyingProbeFailure::KeyReleaseFailed;
    result.detail = defaultDetail(
        error, QStringLiteral("KEY did not return to inactive"));
    releaseAndClose(result);
    return result;
  }

  result.inputs_inactive_after_release = true;
  result.passed = true;
  result.failure = DirectKeyingProbeFailure::None;
  result.detail = QStringLiteral(
      "Physical loopback observed; dummy-load acceptance is still required");
  releaseAndClose(result);
  if (!result.outputs_released) {
    result.passed = false;
    result.failure = DirectKeyingProbeFailure::FinalInactiveNotObserved;
    result.detail = QStringLiteral(
        "Loopback transitions passed, but final output release was not confirmed");
  }
  return result;
}

bool DirectKeyingAcceptanceProbe::waitForSense(
    const DirectKeyingLine output, const bool expected_active,
    QString& error) noexcept {
  for (int attempt = 0; attempt < kMaximumSenseAttempts; ++attempt) {
    DirectKeyingSenseState state;
    if (!backend_->readInputs(state, error)) return false;
    const bool selected = output == DirectKeyingLine::Rts ? state.cts : state.dsr;
    const bool other = output == DirectKeyingLine::Rts ? state.dsr : state.cts;
    if (selected == expected_active && !other) return true;
    backend_->waitForSettling();
  }
  return false;
}

void DirectKeyingAcceptanceProbe::releaseAndClose(
    DirectKeyingProbeResult& result) noexcept {
  bool released = true;
  if (port_open_ && backend_) {
    QString error;
    // Even if the first write fails, always attempt the second one.
    if (!backend_->setOutput(config_.key_line, false, error)) released = false;
    error.clear();
    if (!backend_->setOutput(config_.ptt_line, false, error)) released = false;
    backend_->close();
  }
  if (ownership_acquired_ && backend_) backend_->releaseOwnership();
  result.outputs_released = port_open_ && released;
  port_open_ = false;
  ownership_acquired_ = false;
}

QString directKeyingConfigurationSha256(const DirectKeyingConfig& config) {
  QByteArray canonical =
      QByteArrayLiteral("cw-buddy/direct-keying-acceptance/v1|");
  const QByteArray port = config.port_name.trimmed().toUtf8();
  canonical.append(QByteArray::number(port.size()));
  canonical.append(':');
  canonical.append(port);
  const std::array<char, 4> settings{
      config.ptt_line == DirectKeyingLine::Rts ? 'R' : 'D',
      config.key_line == DirectKeyingLine::Rts ? 'R' : 'D',
      config.ptt_active_high ? '1' : '0',
      config.key_active_high ? '1' : '0'};
  canonical.append(settings.data(), static_cast<qsizetype>(settings.size()));
  return QString::fromLatin1(
      QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

}  // namespace cwassistant::desktop
