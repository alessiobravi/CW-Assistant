#include "transmit/direct_keying_adapter.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QSerialPort>
#include <QStandardPaths>

#include <utility>

namespace cwassistant::desktop {
namespace {

QString lockPathForPort(const QString& port_name) {
  QString directory = QStandardPaths::writableLocation(
      QStandardPaths::RuntimeLocation);
  if (directory.isEmpty()) {
    directory = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
  }
  const QByteArray identity = QCryptographicHash::hash(
      port_name.toUtf8(), QCryptographicHash::Sha256).toHex();
  return QDir(directory).filePath(
      QStringLiteral("cw-buddy-direct-keying-%1.lock")
          .arg(QString::fromLatin1(identity)));
}

class QtSerialKeyingBackend final : public DirectKeyingBackend {
 public:
  QtSerialKeyingBackend() {
    QObject::connect(
        &serial_, &QSerialPort::errorOccurred,
        [this](const QSerialPort::SerialPortError error) {
          if (error == QSerialPort::NoError || !error_handler_) return;
          QString message = serial_.errorString();
          if (message.isEmpty())
            message = QStringLiteral("Serial keying device error");
          error_handler_(std::move(message));
        });
  }

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

  bool setLine(const DirectKeyingLine line, const bool high,
               QString& error) override {
    if (!serial_.isOpen()) {
      error = QStringLiteral("The serial keying port is not open");
      return false;
    }
    const bool changed = line == DirectKeyingLine::Rts
        ? serial_.setRequestToSend(high)
        : serial_.setDataTerminalReady(high);
    if (!changed) {
      error = QStringLiteral("Cannot set the serial %1 line: %2")
                  .arg(line == DirectKeyingLine::Rts
                           ? QStringLiteral("RTS") : QStringLiteral("DTR"),
                       serial_.errorString());
    }
    return changed;
  }

  [[nodiscard]] bool isOpen() const noexcept override {
    return serial_.isOpen();
  }

  void close() noexcept override {
    if (serial_.isOpen()) serial_.close();
  }

  void releaseOwnership() noexcept override {
    if (!lock_) return;
    lock_->unlock();
    lock_.reset();
    port_name_.clear();
  }

  void setErrorHandler(ErrorHandler handler) override {
    error_handler_ = std::move(handler);
  }

 private:
  QSerialPort serial_;
  std::unique_ptr<QLockFile> lock_;
  QString port_name_;
  ErrorHandler error_handler_;
};

}  // namespace

DirectKeyingAdapter::DirectKeyingAdapter()
    : DirectKeyingAdapter(std::make_unique<QtSerialKeyingBackend>()) {}

DirectKeyingAdapter::DirectKeyingAdapter(
    std::unique_ptr<DirectKeyingBackend> backend)
    : backend_(std::move(backend)) {
  if (backend_) {
    backend_->setErrorHandler(
        [this](QString error) { handleBackendError(std::move(error)); });
  } else {
    snapshot_.fault = QStringLiteral("No serial keying backend is available");
  }
  refreshUnknownFlag();
}

DirectKeyingAdapter::~DirectKeyingAdapter() {
  if (backend_) backend_->setErrorHandler({});
  close();
}

bool DirectKeyingAdapter::open(const DirectKeyingConfig& config) {
  close();
  snapshot_.fault.clear();
  if (!backend_) {
    snapshot_.fault = QStringLiteral("No serial keying backend is available");
    refreshUnknownFlag();
    return false;
  }
  if (config.port_name.trimmed().isEmpty()) {
    snapshot_.fault = QStringLiteral(
        "An explicit serial keying port must be selected");
    refreshUnknownFlag();
    return false;
  }
  if (config.ptt_line == config.key_line) {
    snapshot_.fault = QStringLiteral("PTT and KEY must use distinct lines");
    refreshUnknownFlag();
    return false;
  }
  if (!config.ptt_active_high || !config.key_active_high) {
    snapshot_.fault = QStringLiteral(
        "This direct-keying slice supports active-high PTT and KEY only");
    refreshUnknownFlag();
    return false;
  }

  config_ = config;
  QString error;
  if (!backend_->acquire(config.port_name, error)) {
    snapshot_.fault = error.isEmpty()
        ? QStringLiteral("Cannot acquire serial keying ownership") : error;
    refreshUnknownFlag();
    return false;
  }
  ownership_acquired_ = true;
  if (!backend_->open(error)) {
    snapshot_.fault = error.isEmpty()
        ? QStringLiteral("Cannot open the selected serial keying port") : error;
    backend_->releaseOwnership();
    ownership_acquired_ = false;
    markClosedUnknown();
    return false;
  }

  // No other port operation may intervene between open and the safe line
  // initialization. KEY is commanded inactive first, then PTT.
  snapshot_.open_safe = false;
  snapshot_.key = DirectKeyingLineState::Unknown;
  snapshot_.ptt = DirectKeyingLineState::Unknown;
  const bool released = releaseAll();
  if (!released) {
    const QString release_error = snapshot_.fault.isEmpty()
        ? QStringLiteral("Cannot initialize serial keying lines inactive")
        : snapshot_.fault;
    faultAndClose(release_error, false);
    return false;
  }
  snapshot_.open_safe = true;
  refreshUnknownFlag();
  return true;
}

bool DirectKeyingAdapter::setPtt(const bool asserted) {
  return setLogicalLine(config_.ptt_line, asserted, snapshot_.ptt);
}

bool DirectKeyingAdapter::setKey(const bool asserted) {
  return setLogicalLine(config_.key_line, asserted, snapshot_.key);
}

bool DirectKeyingAdapter::setLogicalLine(
    const DirectKeyingLine line, const bool asserted,
    DirectKeyingLineState& state) {
  if (!backend_ || !snapshot_.open_safe || !backend_->isOpen() ||
      !snapshot_.fault.isEmpty()) {
    refreshUnknownFlag();
    return false;
  }
  const auto wanted = asserted ? DirectKeyingLineState::Active
                               : DirectKeyingLineState::Inactive;
  if (state == wanted) return true;
  QString error;
  if (!backend_->setLine(line, asserted, error)) {
    state = DirectKeyingLineState::Unknown;
    faultAndClose(error.isEmpty()
                      ? QStringLiteral("Serial control-line write failed")
                      : error,
                  true);
    return false;
  }
  state = wanted;
  refreshUnknownFlag();
  return true;
}

bool DirectKeyingAdapter::releaseAll() noexcept {
  if (!backend_ || !backend_->isOpen()) {
    markClosedUnknown();
    return false;
  }

  bool released = true;
  QString first_error;
  if (snapshot_.key != DirectKeyingLineState::Inactive) {
    QString error;
    if (backend_->setLine(config_.key_line, false, error)) {
      snapshot_.key = DirectKeyingLineState::Inactive;
    } else {
      snapshot_.key = DirectKeyingLineState::Unknown;
      first_error = error;
      released = false;
    }
  }
  if (snapshot_.ptt != DirectKeyingLineState::Inactive) {
    QString error;
    if (backend_->setLine(config_.ptt_line, false, error)) {
      snapshot_.ptt = DirectKeyingLineState::Inactive;
    } else {
      snapshot_.ptt = DirectKeyingLineState::Unknown;
      if (first_error.isEmpty()) first_error = error;
      released = false;
    }
  }
  if (!released) {
    snapshot_.fault = first_error.isEmpty()
        ? QStringLiteral("Cannot release serial keying lines") : first_error;
  }
  refreshUnknownFlag();
  return released;
}

void DirectKeyingAdapter::close() noexcept {
  if (!backend_) {
    markClosedUnknown();
    return;
  }
  if (backend_->isOpen()) {
    (void)releaseAll();
    backend_->close();
  }
  if (ownership_acquired_) backend_->releaseOwnership();
  ownership_acquired_ = false;
  markClosedUnknown();
}

void DirectKeyingAdapter::handleBackendError(QString error) noexcept {
  if (handling_fault_) return;
  if (error.isEmpty()) error = QStringLiteral("Serial keying device error");
  faultAndClose(std::move(error), true);
}

void DirectKeyingAdapter::faultAndClose(QString error,
                                        const bool release_lines) noexcept {
  if (handling_fault_) return;
  handling_fault_ = true;
  if (release_lines && backend_ && backend_->isOpen()) (void)releaseAll();
  if (backend_) {
    backend_->close();
    if (ownership_acquired_) backend_->releaseOwnership();
  }
  ownership_acquired_ = false;
  markClosedUnknown();
  snapshot_.fault = std::move(error);
  refreshUnknownFlag();
  handling_fault_ = false;
}

void DirectKeyingAdapter::markClosedUnknown() noexcept {
  snapshot_.open_safe = false;
  snapshot_.ptt = DirectKeyingLineState::Unknown;
  snapshot_.key = DirectKeyingLineState::Unknown;
  refreshUnknownFlag();
}

void DirectKeyingAdapter::refreshUnknownFlag() noexcept {
  snapshot_.fault_or_unknown = !snapshot_.fault.isEmpty() ||
      !snapshot_.open_safe ||
      snapshot_.ptt == DirectKeyingLineState::Unknown ||
      snapshot_.key == DirectKeyingLineState::Unknown;
}

}  // namespace cwassistant::desktop
