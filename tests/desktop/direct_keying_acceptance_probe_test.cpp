#include <QCoreApplication>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "transmit/direct_keying_acceptance_probe.hpp"

namespace {
using namespace cwassistant::desktop;
int failures = 0;
void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}
struct FakeState {
  std::vector<std::string> events;
  bool acquired{false};
  bool open{false};
  bool fail_acquire{false};
  bool fail_key_release{false};
  bool cross_coupled{false};
  bool rts{false};
  bool dtr{false};
};
class FakeBackend final : public DirectKeyingAcceptanceBackend {
 public:
  explicit FakeBackend(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
  bool acquire(const QString& port, QString& error) override {
    state_->events.push_back("acquire:" + port.toStdString());
    if (state_->fail_acquire) { error = QStringLiteral("busy"); return false; }
    state_->acquired = true; return true;
  }
  bool open(QString&) override { state_->events.push_back("open"); state_->open = true; return true; }
  bool setOutput(const DirectKeyingLine line, const bool high, QString& error) override {
    state_->events.push_back(std::string("set:") +
        (line == DirectKeyingLine::Rts ? "RTS=" : "DTR=") + (high ? "1" : "0"));
    if (state_->fail_key_release && line == DirectKeyingLine::Dtr && !high) {
      error = QStringLiteral("release failed"); return false;
    }
    (line == DirectKeyingLine::Rts ? state_->rts : state_->dtr) = high;
    return true;
  }
  bool readInputs(DirectKeyingSenseState& sense, QString&) override {
    state_->events.push_back("read");
    sense.cts = state_->cross_coupled ? state_->dtr : state_->rts;
    sense.dsr = state_->cross_coupled ? state_->rts : state_->dtr;
    return true;
  }
  void waitForSettling() noexcept override { state_->events.push_back("wait"); }
  void close() noexcept override { state_->events.push_back("close"); state_->open = false; }
  void releaseOwnership() noexcept override {
    state_->events.push_back("unlock"); state_->acquired = false;
  }
 private:
  std::shared_ptr<FakeState> state_;
};
DirectKeyingConfig config() {
  return {.port_name = QStringLiteral("explicit-loopback-port"),
          .ptt_line = DirectKeyingLine::Rts, .key_line = DirectKeyingLine::Dtr,
          .ptt_active_high = true, .key_active_high = true};
}
void testAuthorizationIsRequiredBeforeAnyEffect() {
  auto state = std::make_shared<FakeState>();
  DirectKeyingAcceptanceProbe probe(std::make_unique<FakeBackend>(state));
  const auto result = probe.run(config(), false);
  expect(!result.passed &&
             result.failure == DirectKeyingProbeFailure::RadioDisconnectNotConfirmed &&
             state->events.empty(),
         "no serial effect occurs before explicit radio-disconnected confirmation");
}
void testOneLineAtATimeAndFinalRelease() {
  auto state = std::make_shared<FakeState>();
  DirectKeyingAcceptanceProbe probe(std::make_unique<FakeBackend>(state));
  const auto result = probe.run(config(), true);
  expect(result.passed && result.ptt_transition_observed &&
             result.key_transition_observed && result.outputs_released &&
             result.inputs_inactive_after_release,
         "matching RTS-CTS and DTR-DSR loopback passes");
  expect(!state->open && !state->acquired && !state->rts && !state->dtr,
         "a passing probe closes ownership with both outputs inactive");
  bool both_active = false, rts = false, dtr = false;
  for (const auto& event : state->events) {
    if (event == "set:RTS=1") rts = true;
    if (event == "set:RTS=0") rts = false;
    if (event == "set:DTR=1") dtr = true;
    if (event == "set:DTR=0") dtr = false;
    both_active = both_active || (rts && dtr);
  }
  expect(!both_active, "the physical probe never asserts both outputs together");
  expect(state->events.size() >= 4 &&
             state->events[state->events.size() - 4] == "set:DTR=0" &&
             state->events[state->events.size() - 3] == "set:RTS=0" &&
             state->events[state->events.size() - 2] == "close" &&
             state->events.back() == "unlock",
         "final cleanup releases KEY before PTT, closes, then unlocks");
}
void testWrongWiringFailsAndReleases() {
  auto state = std::make_shared<FakeState>();
  state->cross_coupled = true;
  DirectKeyingAcceptanceProbe probe(std::make_unique<FakeBackend>(state));
  const auto result = probe.run(config(), true);
  expect(!result.passed && result.failure == DirectKeyingProbeFailure::PttLoopbackNotObserved,
         "cross-coupled input wiring does not pass as a valid loopback");
  expect(!state->open && !state->acquired && !state->rts && !state->dtr,
         "a failed observation still releases and closes the adapter");
}
void testOwnershipAndReleaseFailuresFailClosed() {
  auto busy = std::make_shared<FakeState>();
  busy->fail_acquire = true;
  DirectKeyingAcceptanceProbe busy_probe(std::make_unique<FakeBackend>(busy));
  const auto busy_result = busy_probe.run(config(), true);
  expect(!busy_result.passed &&
             busy_result.failure == DirectKeyingProbeFailure::OwnershipUnavailable &&
             busy->events == std::vector<std::string>{"acquire:explicit-loopback-port"},
         "a busy port is never opened or manipulated");
  auto release = std::make_shared<FakeState>();
  release->fail_key_release = true;
  DirectKeyingAcceptanceProbe release_probe(std::make_unique<FakeBackend>(release));
  const auto release_result = release_probe.run(config(), true);
  expect(!release_result.passed &&
             release_result.failure == DirectKeyingProbeFailure::InactiveInitializationFailed &&
             !release->open && !release->acquired,
         "an unconfirmed output release fails closed and cannot be accepted");
}
void testFingerprintIsStableAndSensitiveWithoutExposingPort() {
  const auto first = directKeyingConfigurationSha256(config());
  const auto second = directKeyingConfigurationSha256(config());
  auto changed = config();
  changed.port_name = QStringLiteral("another-private-device-path");
  const auto third = directKeyingConfigurationSha256(changed);
  expect(first.size() == 64 && first == second && first != third,
         "configuration digest is stable and changes with keying identity");
  expect(!first.contains(QStringLiteral("explicit")),
         "persisted digest does not expose the selected port name");
}
}  // namespace
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testAuthorizationIsRequiredBeforeAnyEffect();
  testOneLineAtATimeAndFinalRelease();
  testWrongWiringFailsAndReleases();
  testOwnershipAndReleaseFailuresFailClosed();
  testFingerprintIsStableAndSensitiveWithoutExposingPort();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
