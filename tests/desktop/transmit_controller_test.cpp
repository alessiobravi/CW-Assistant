#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "transmit/transmit_controller.hpp"

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

struct BackendRecord {
  QMutex mutex;
  std::vector<std::pair<cwassistant::desktop::DirectKeyingLine, bool>> writes;
  bool open{false};
};

class FakeBackend final : public cwassistant::desktop::DirectKeyingBackend {
 public:
  explicit FakeBackend(std::shared_ptr<BackendRecord> record)
      : record_(std::move(record)) {}
  bool acquire(const QString&, QString&) override { return true; }
  bool open(QString&) override {
    QMutexLocker lock(&record_->mutex);
    record_->open = true;
    return true;
  }
  bool setLine(const cwassistant::desktop::DirectKeyingLine line,
               const bool high, QString&) override {
    QMutexLocker lock(&record_->mutex);
    record_->writes.emplace_back(line, high);
    return true;
  }
  [[nodiscard]] bool isOpen() const noexcept override {
    QMutexLocker lock(&record_->mutex);
    return record_->open;
  }
  void close() noexcept override {
    QMutexLocker lock(&record_->mutex);
    record_->open = false;
  }
  void releaseOwnership() noexcept override {}
  void setErrorHandler(ErrorHandler handler) override {
    error_handler_ = std::move(handler);
  }

 private:
  std::shared_ptr<BackendRecord> record_;
  ErrorHandler error_handler_;
};

bool waitUntil(const std::function<bool()>& predicate, const int timeout_ms) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!predicate() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return predicate();
}

std::unique_ptr<cwassistant::desktop::TransmitController> makeController(
    const std::shared_ptr<BackendRecord>& record) {
  return std::make_unique<cwassistant::desktop::TransmitController>(
      [record] { return std::make_unique<FakeBackend>(record); });
}

void configureReadyStation(cwassistant::desktop::TransmitController& controller,
                           const QString& port = QStringLiteral("fake-port")) {
  controller.configureHardware(true, port, 0, 1, true, true, QString{}, true);
  controller.configureRadioSafety(true, 14'026'500, QStringLiteral("CW"),
                                  true, true, true);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto record = std::make_shared<BackendRecord>();
  auto owned_controller = makeController(record);
  auto& controller = *owned_controller;

  expect(!controller.arm(), "arming requires the operator's own callsign");
  controller.setOwnCallsign(QStringLiteral(" iu0lfq/p "));
  expect(!controller.arm(), "arming also requires configured transmit hardware");
  controller.configureHardware(true, QStringLiteral("fake-port"), 0, 1,
                               true, true, QString{}, false);
  controller.configureRadioSafety(true, 14'026'500, QStringLiteral("CW"),
                                  true, true, true);
  expect(!controller.arm(),
         "arming requires explicit disconnected-line and loopback validation");
  configureReadyStation(controller);
  expect(controller.hardwareAvailable() && controller.stationReady() &&
             controller.arm(),
         "arming opens a configured adapter only after radio confirmation");
  expect(!controller.selectTarget(7, QStringLiteral("?1ABC"), 0),
         "uncertain decoder output cannot become a TX target");
  expect(controller.selectTarget(7, QStringLiteral("k1abc"), 14'025'000) &&
             controller.targetRfHz() == 14'025'000,
         "an exact structurally valid decoder call may be selected");
  expect(!controller.confirmTarget(QStringLiteral("K1ABD")) &&
             controller.confirmTarget(QStringLiteral("K1ABC")),
         "the operator must retype the selected callsign exactly");
  expect(controller.prepareFreeText(QStringLiteral("  de iu0lfq/p   pse k ")) &&
             controller.preparedMessage() == QStringLiteral("DE IU0LFQ/P PSE K"),
         "operator free text is normalized into a visible Morse preview");
  expect(!controller.confirmPreview(QStringLiteral("DE IU0LFQ/P K")) &&
             controller.confirmPreview(QStringLiteral("DE IU0LFQ/P PSE K")),
         "the message guard rejects any preview mismatch");
  expect(controller.prepareFreeText(QStringLiteral("E")) &&
             controller.confirmPreview(QStringLiteral("E")),
         "a short exact fixture is prepared for realtime hardware sequencing");
  expect(controller.transmitPrepared() && controller.transmitting(),
         "an exactly confirmed plan reaches the guarded fake adapter");
  const bool message_completed = waitUntil(
      [&controller] { return !controller.transmitting(); }, 5'000);
  if (!message_completed || controller.state() != QStringLiteral("confirmed") ||
      controller.onAir()) {
    std::cerr << "TX completion state=" << controller.state().toStdString()
              << " status=" << controller.status().toStdString()
              << " hardware=" << controller.hardwareStatus().toStdString()
              << " onAir=" << controller.onAir() << '\n';
  }
  expect(message_completed && controller.state() == QStringLiteral("confirmed") &&
             !controller.onAir(),
         "scheduled Morse completes with KEY and PTT inactive");

  expect(controller.toggleTune() && controller.tuning() &&
             controller.onAir(),
         "an armed operator can start guarded TUNE on known-safe hardware");
  expect(controller.toggleTune() && !controller.tuning() &&
             !controller.onAir(),
         "a second TUNE action synchronously releases KEY and PTT");

  controller.configureTxSpeed(1, 5);
  expect(controller.prepareFreeText(QStringLiteral("VVV VVV")) &&
             controller.confirmPreview(QStringLiteral("VVV VVV")) &&
             controller.transmitPrepared() && controller.cancelTransmission() &&
             !controller.transmitting() && !controller.onAir(),
         "operator cancellation drains a live message without losing the QSO");

  controller.setAutoQsoEnabled(true);
  QVariantMap listening;
  listening.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(7));
  listening.insert(QStringLiteral("text"), QStringLiteral("CQ CQ DE K1ABC K"));
  listening.insert(QStringLiteral("refinedText"), QString{});
  controller.observeDecoderChannels(QVariantList{listening});
  expect(controller.proposedMessage() == QStringLiteral("IU0LFQ/P"),
         "a listening cue proposes the operator's call but does not send it");
  expect(controller.acceptProposal() && !controller.messageConfirmed(),
         "an accepted decoder proposal still requires exact message confirmation");

  auto uncertain_record = std::make_shared<BackendRecord>();
  auto owned_uncertain_controller = makeController(uncertain_record);
  auto& uncertain_controller = *owned_uncertain_controller;
  uncertain_controller.setOwnCallsign(QStringLiteral("IU0LFQ"));
  configureReadyStation(uncertain_controller, QStringLiteral("uncertain-port"));
  expect(uncertain_controller.arm() &&
             uncertain_controller.selectTarget(9, QStringLiteral("K2XYZ"),
                                                 7'025'000) &&
             uncertain_controller.confirmTarget(QStringLiteral("K2XYZ")),
         "near-call fixture reaches an exactly confirmed QSO");
  uncertain_controller.setAutoQsoEnabled(true);
  QVariantMap uncertain;
  uncertain.insert(QStringLiteral("id"), QVariant::fromValue<qulonglong>(9));
  uncertain.insert(QStringLiteral("text"),
                   QStringLiteral("IU0LF? IU0LF?"));
  uncertain.insert(QStringLiteral("refinedText"), QString{});
  uncertain_controller.observeDecoderChannels(QVariantList{uncertain});
  expect(uncertain_controller.proposedMessage() == QStringLiteral("IU0LFQ") &&
             !uncertain_controller.messageConfirmed() &&
             !uncertain_controller.onAir(),
         "a repeated similar own-call decode proposes a guarded resend only");

  uncertain_controller.configureRadioSafety(
      true, 14'026'600, QStringLiteral("CW"), true, true, true);
  expect(uncertain_controller.state() == QStringLiteral("disarmed") &&
             !uncertain_controller.onAir(),
         "a confirmed TX-frequency change forces explicit re-arming");

  auto conflict_record = std::make_shared<BackendRecord>();
  auto conflict_controller = makeController(conflict_record);
  conflict_controller->setOwnCallsign(QStringLiteral("IU0LFQ"));
  conflict_controller->configureHardware(
      true, QStringLiteral("shared-port"), 0, 1, true, true,
      QStringLiteral("shared-port"), true);
  conflict_controller->configureRadioSafety(
      true, 7'025'000, QStringLiteral("CW"), true, true, false);
  expect(!conflict_controller->hardwareAvailable() &&
             !conflict_controller->arm(),
         "CAT and direct KEY/PTT cannot share one serial port");

  conflict_controller->configureHardware(
      true, QStringLiteral("key-port"), -1, 2, true, true, QString{}, true);
  expect(!conflict_controller->hardwareAvailable() &&
             !conflict_controller->arm(),
         "out-of-range serial line selections fail closed");

  auto changed_record = std::make_shared<BackendRecord>();
  auto changed_controller = makeController(changed_record);
  changed_controller->setOwnCallsign(QStringLiteral("IU0LFQ"));
  configureReadyStation(*changed_controller, QStringLiteral("changed-port"));
  expect(changed_controller->arm() &&
             changed_controller->selectTarget(
                 11, QStringLiteral("K3ABC"), 14'025'000) &&
             changed_controller->confirmTarget(QStringLiteral("K3ABC")) &&
             changed_controller->prepareFreeText(QStringLiteral("VVV VVV")) &&
             changed_controller->confirmPreview(QStringLiteral("VVV VVV")) &&
             changed_controller->transmitPrepared(),
         "radio-change fixture reaches guarded transmission");
  changed_controller->configureRadioSafety(
      true, 14'026'600, QStringLiteral("CW"), true, true, true);
  expect(changed_controller->state() == QStringLiteral("fault") &&
             !changed_controller->transmitting() &&
             !changed_controller->onAir(),
         "a TX radio-state change on air forces synchronous emergency release");

  controller.emergencyRelease();
  expect(controller.state() == QStringLiteral("fault") && !controller.armed() &&
             controller.preparedMessage().isEmpty(),
         "emergency release clears pending TX and latches a fault");
  expect(controller.resetFault() &&
             controller.state() == QStringLiteral("disarmed"),
         "fault reset never silently re-arms TX");

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
