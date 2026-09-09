#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QThread>

#include <cstdlib>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "cwassistant/core/cw_transmit_encoder.hpp"
#include "transmit/direct_transmit_engine.hpp"

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
  bool fail_key_assertion{false};
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
               const bool high, QString& error) override {
    QMutexLocker lock(&record_->mutex);
    record_->writes.emplace_back(line, high);
    if (record_->fail_key_assertion &&
        line == cwassistant::desktop::DirectKeyingLine::Dtr && high) {
      error = QStringLiteral("Injected KEY assertion failure");
      return false;
    }
    return true;
  }
  bool isOpen() const noexcept override {
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto record = std::make_shared<BackendRecord>();
  cwassistant::desktop::DirectTransmitEngine engine(
      [record] { return std::make_unique<FakeBackend>(record); });
  engine.configure({
      .keying = {.port_name = QStringLiteral("fake-port")},
      .scheduler = {.ptt_lead_ns = 5'000'000ULL,
                    .ptt_hang_ns = 5'000'000ULL,
                    .maximum_message_duration_ns = 1'000'000'000ULL,
                    .maximum_transition_lateness_ns = 100'000'000ULL},
  });
  expect(engine.available() && !engine.openSafeState(),
         "inert configuration exposes availability without opening hardware");
  expect(engine.openSafe() && engine.openSafeState() && !engine.ptt() &&
             !engine.key(),
         "blocking open returns only after known-inactive line state");

  const auto plan = cwassistant::core::CwTransmitEncoder::encode("E", 80U);
  expect(plan.has_value(), "engine fixture plan encodes");
  if (plan) {
    expect(!engine.start(*plan, false) && !engine.busy(),
           "engine rejects a plan without explicit exact-preview confirmation");
    expect(engine.start(*plan, true) && engine.busy() && engine.ptt(),
           "confirmed immutable plan starts with PTT asserted");
    expect(engine.remainingNs() > 0U && engine.progress() >= 0.0 &&
               engine.progress() < 1.0,
           "a running immutable plan exposes bounded authoritative progress");
    expect(waitUntil([&engine] { return !engine.busy(); }, 1'000) &&
               !engine.ptt() && !engine.key() && !engine.fault(),
           "dedicated timer completes with both lines inactive");
    expect(engine.elapsedNs() == 0U && engine.remainingNs() == 0U &&
               engine.progress() == 0.0,
           "completed transmission clears activity timing");
    expect(engine.start(*plan, true) && engine.cancel() && !engine.busy() &&
               !engine.ptt() && !engine.key() && !engine.fault(),
           "operator cancellation synchronously releases KEY before PTT");
    expect(!engine.startTune(false),
           "TUNE rejects a request without explicit operator authorization");
    expect(engine.startTune(true) && engine.busy() && engine.ptt() &&
               engine.key() && engine.remainingNs() > 0U &&
               !engine.startTune(true),
           "authorized TUNE asserts PTT then KEY and cannot extend itself");
    const auto initial_tune_remaining = engine.remainingNs();
    expect(waitUntil([&engine, initial_tune_remaining] {
               return engine.elapsedNs() > 0U &&
                   engine.remainingNs() < initial_tune_remaining;
             }, 500),
           "TUNE countdown advances from the worker watchdog clock");
    expect(engine.stopTune() && !engine.busy() && !engine.ptt() &&
               !engine.key() && !engine.fault(),
           "stopping TUNE synchronously releases KEY then PTT");
    expect(engine.elapsedNs() == 0U && engine.remainingNs() == 0U &&
               engine.progress() == 0.0,
           "stopped TUNE clears its watchdog countdown");
  }

  {
    QMutexLocker lock(&record->mutex);
    using Line = cwassistant::desktop::DirectKeyingLine;
    const std::vector<std::pair<Line, bool>> expected{
        {Line::Dtr, false}, {Line::Rts, false}, {Line::Rts, true},
        {Line::Dtr, true},  {Line::Dtr, false}, {Line::Rts, false}};
    expect(record->writes.size() >= expected.size() &&
               std::equal(expected.begin(), expected.end(),
                          record->writes.begin()),
           "adapter writes PTT before KEY assertion and KEY before PTT release");
  }

  if (plan) {
    expect(engine.start(*plan, true), "engine can safely reuse a completed scheduler");
    engine.emergencyRelease();
    expect(engine.fault() && !engine.busy() && !engine.ptt() && !engine.key(),
           "blocking emergency release drains both lines and faults the engine");
  }
  engine.close();
  expect(!engine.openSafeState() && !engine.busy() && !engine.ptt() &&
             !engine.key(),
         "blocking close leaves no active cached output");

  auto failed_record = std::make_shared<BackendRecord>();
  cwassistant::desktop::DirectTransmitEngine failed_engine(
      [failed_record] { return std::make_unique<FakeBackend>(failed_record); });
  failed_engine.configure({
      .keying = {.port_name = QStringLiteral("failing-port")},
      .scheduler = {.ptt_lead_ns = 0U,
                    .ptt_hang_ns = 0U,
                    .maximum_message_duration_ns = 1'000'000'000ULL,
                    .maximum_transition_lateness_ns = 100'000'000ULL},
  });
  expect(failed_engine.openSafe(), "failure fixture opens safely");
  {
    QMutexLocker lock(&failed_record->mutex);
    failed_record->fail_key_assertion = true;
  }
  expect(plan && failed_engine.start(*plan, true),
         "failure fixture starts before its scheduled KEY transition");
  expect(waitUntil([&failed_engine] { return failed_engine.fault(); }, 1'000) &&
             !failed_engine.busy() && !failed_engine.openSafeState() &&
             !failed_engine.ptt() && !failed_engine.key(),
         "adapter KEY failure faults the engine and drains/closes the output");

  auto watchdog_record = std::make_shared<BackendRecord>();
  cwassistant::desktop::DirectTransmitEngine watchdog_engine(
      [watchdog_record] { return std::make_unique<FakeBackend>(watchdog_record); });
  watchdog_engine.configure({
      .keying = {.port_name = QStringLiteral("watchdog-port")},
      .maximum_tune_duration_ns = 20'000'000ULL,
  });
  expect(watchdog_engine.openSafe() && watchdog_engine.startTune(true),
         "short deterministic TUNE-watchdog fixture starts safely");
  expect(waitUntil([&watchdog_engine] { return watchdog_engine.fault(); }, 1'000) &&
             !watchdog_engine.busy() && !watchdog_engine.openSafeState() &&
             !watchdog_engine.ptt() && !watchdog_engine.key(),
         "TUNE watchdog is non-extendable and releases both lines on expiry");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
