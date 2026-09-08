#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "transmit/direct_keying_adapter.hpp"

namespace {

using cwassistant::desktop::DirectKeyingAdapter;
using cwassistant::desktop::DirectKeyingBackend;
using cwassistant::desktop::DirectKeyingConfig;
using cwassistant::desktop::DirectKeyingLine;
using cwassistant::desktop::DirectKeyingLineState;

int failures = 0;

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}

std::string lineName(const DirectKeyingLine line) {
  return line == DirectKeyingLine::Rts ? "RTS" : "DTR";
}

struct FakeState {
  std::vector<std::string> events;
  bool acquire_succeeds{true};
  bool open_succeeds{true};
  bool open{false};
  bool owned{false};
  int fail_writes{0};
  DirectKeyingBackend::ErrorHandler error_handler;
};

class FakeBackend final : public DirectKeyingBackend {
 public:
  explicit FakeBackend(std::shared_ptr<FakeState> state)
      : state_(std::move(state)) {}

  bool acquire(const QString& port_name, QString& error) override {
    state_->events.push_back("acquire:" + port_name.toStdString());
    if (!state_->acquire_succeeds) {
      error = QStringLiteral("lock busy");
      return false;
    }
    state_->owned = true;
    return true;
  }

  bool open(QString& error) override {
    state_->events.push_back("open");
    if (!state_->open_succeeds) {
      error = QStringLiteral("open failed");
      return false;
    }
    state_->open = true;
    return true;
  }

  bool setLine(const DirectKeyingLine line, const bool high,
               QString& error) override {
    state_->events.push_back("line:" + lineName(line) +
                             (high ? "=1" : "=0"));
    if (state_->fail_writes > 0) {
      --state_->fail_writes;
      error = QStringLiteral("line failed");
      return false;
    }
    return state_->open;
  }

  [[nodiscard]] bool isOpen() const noexcept override { return state_->open; }

  void close() noexcept override {
    state_->events.push_back("close");
    state_->open = false;
  }

  void releaseOwnership() noexcept override {
    state_->events.push_back("unlock");
    state_->owned = false;
  }

  void setErrorHandler(ErrorHandler handler) override {
    state_->error_handler = std::move(handler);
  }

 private:
  std::shared_ptr<FakeState> state_;
};

std::unique_ptr<DirectKeyingBackend> fakeBackend(
    const std::shared_ptr<FakeState>& state) {
  return std::make_unique<FakeBackend>(state);
}

DirectKeyingConfig normalConfig() {
  return {.port_name = QStringLiteral("explicit-test-port"),
          .ptt_line = DirectKeyingLine::Rts,
          .key_line = DirectKeyingLine::Dtr,
          .ptt_active_high = true,
          .key_active_high = true};
}

void expectTail(const std::vector<std::string>& events,
                const std::vector<std::string>& tail,
                const char* message) {
  if (events.size() >= tail.size() &&
      std::equal(tail.begin(), tail.end(), events.end() -
                 static_cast<std::ptrdiff_t>(tail.size()))) {
    return;
  }
  expect(false, message);
}

void testValidationAndSafeOpen() {
  auto state = std::make_shared<FakeState>();
  DirectKeyingAdapter adapter(fakeBackend(state));
  auto invalid = normalConfig();
  invalid.port_name.clear();
  expect(!adapter.open(invalid) && state->events.empty(),
         "opening requires one explicitly selected port without probing");
  invalid = normalConfig();
  invalid.key_line = invalid.ptt_line;
  expect(!adapter.open(invalid) && state->events.empty(),
         "PTT and KEY must use distinct control lines");
  invalid = normalConfig();
  invalid.key_active_high = false;
  expect(!adapter.open(invalid) && state->events.empty(),
         "the first hardware slice rejects active-low wiring");

  expect(adapter.open(normalConfig()), "a valid explicit configuration opens");
  expect(state->events == std::vector<std::string>{
             "acquire:explicit-test-port", "open", "line:DTR=0",
             "line:RTS=0"},
         "ownership precedes open and KEY is made inactive before PTT");
  const auto& snapshot = adapter.snapshot();
  expect(snapshot.open_safe && !snapshot.fault_or_unknown &&
             snapshot.key == DirectKeyingLineState::Inactive &&
             snapshot.ptt == DirectKeyingLineState::Inactive,
         "safe-open snapshot exposes two known inactive lines");
}

void testReleaseOrderingAndClose() {
  auto state = std::make_shared<FakeState>();
  DirectKeyingAdapter adapter(fakeBackend(state));
  expect(adapter.open(normalConfig()) && adapter.setPtt(true) &&
             adapter.setKey(true),
         "fixture can assert both active-high lines");
  const auto before = state->events.size();
  expect(adapter.releaseAll(), "releaseAll succeeds");
  expectTail(state->events, {"line:DTR=0", "line:RTS=0"},
             "releaseAll releases KEY before PTT");
  const auto after = state->events.size();
  expect(adapter.releaseAll() && state->events.size() == after && after > before,
         "releaseAll is idempotent after both lines are inactive");

  expect(adapter.setKey(true) && adapter.setPtt(true),
         "fixture reasserts lines before close");
  adapter.close();
  expectTail(state->events,
             {"line:DTR=0", "line:RTS=0", "close", "unlock"},
             "close releases KEY then PTT before closing and unlocking");
  expect(!adapter.snapshot().open_safe &&
             adapter.snapshot().fault_or_unknown &&
             adapter.snapshot().key == DirectKeyingLineState::Unknown &&
             adapter.snapshot().ptt == DirectKeyingLineState::Unknown,
         "closed hardware is reported unknown rather than safely inactive");
}

void testFailuresAndDeviceError() {
  auto locked = std::make_shared<FakeState>();
  locked->acquire_succeeds = false;
  DirectKeyingAdapter lock_adapter(fakeBackend(locked));
  expect(!lock_adapter.open(normalConfig()) &&
             locked->events == std::vector<std::string>{
                 "acquire:explicit-test-port"} &&
             lock_adapter.snapshot().fault_or_unknown,
         "cross-process ownership failure prevents port open");

  auto opening = std::make_shared<FakeState>();
  opening->open_succeeds = false;
  DirectKeyingAdapter open_adapter(fakeBackend(opening));
  expect(!open_adapter.open(normalConfig()), "backend open failure is reported");
  expectTail(opening->events, {"open", "unlock"},
             "an open failure releases ownership without touching lines");

  auto initializing = std::make_shared<FakeState>();
  initializing->fail_writes = 1;
  DirectKeyingAdapter init_adapter(fakeBackend(initializing));
  expect(!init_adapter.open(normalConfig()),
         "failure to initialize either inactive line fails closed");
  expectTail(initializing->events,
             {"line:DTR=0", "line:RTS=0", "close", "unlock"},
             "safe initialization attempts PTT release after KEY failure");

  auto errored = std::make_shared<FakeState>();
  DirectKeyingAdapter error_adapter(fakeBackend(errored));
  expect(error_adapter.open(normalConfig()) && error_adapter.setPtt(true) &&
             error_adapter.setKey(true),
         "device-error fixture starts with active lines");
  const auto callback = errored->error_handler;
  callback(QStringLiteral("device removed"));
  expectTail(errored->events,
             {"line:DTR=0", "line:RTS=0", "close", "unlock"},
             "device error attempts ordered release before close");
  expect(error_adapter.snapshot().fault_or_unknown &&
             error_adapter.snapshot().fault == QStringLiteral("device removed"),
         "device error leaves a visible fault/unknown snapshot");
}

void testDestructorRelease() {
  auto state = std::make_shared<FakeState>();
  {
    DirectKeyingAdapter adapter(fakeBackend(state));
    expect(adapter.open(normalConfig()) && adapter.setPtt(true) &&
               adapter.setKey(true),
           "destructor fixture asserts both lines");
  }
  expectTail(state->events,
             {"line:DTR=0", "line:RTS=0", "close", "unlock"},
             "destructor performs ordered best-effort release");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testValidationAndSafeOpen();
  testReleaseOrderingAndClose();
  testFailuresAndDeviceError();
  testDestructorRelease();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
