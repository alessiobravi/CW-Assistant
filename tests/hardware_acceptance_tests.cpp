#include "cwassistant/core/hardware_acceptance.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace cwassistant::core;
int failures = 0;
void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}
std::string fingerprint(const char digit = 'a') { return std::string(64, digit); }
DisconnectedLineEvidence disconnectedEvidence() {
  return {.radio_disconnected = true, .ptt_inactive_observed = true,
          .key_inactive_observed = true, .independent_release_available = true};
}
PhysicalLoopbackEvidence loopbackEvidence() {
  return {.radio_disconnected = true, .ptt_transition_observed = true,
          .key_transition_observed = true, .both_lines_returned_inactive = true};
}
DummyLoadEvidence dummyLoadEvidence() {
  return {.dummy_load_connected = true, .minimum_power_selected = true,
          .message_keying_observed = true,
          .cancellation_release_observed = true,
          .watchdog_release_observed = true,
          .emergency_release_observed = true};
}
void testOrderedEvidenceAndMilestones() {
  HardwareAcceptanceWorkflow workflow;
  expect(workflow.status() == HardwareAcceptanceStatus::ConfigurationRequired,
         "a fresh workflow requires an explicit configuration");
  expect(!workflow.setConfiguration("serial-device", HardwareAcceptancePlatform::Linux),
         "raw device identity is rejected instead of being persisted");
  expect(workflow.setConfiguration(fingerprint(), HardwareAcceptancePlatform::Linux),
         "a lowercase SHA-256 configuration identity is accepted");
  expect(!workflow.confirmPhysicalLoopback(loopbackEvidence(), 101),
         "physical loopback cannot skip disconnected-line observation");
  expect(!workflow.confirmDisconnectedLine({}, 100) &&
             workflow.lastFailure() == HardwareAcceptanceFailure::RadioMustBeDisconnected,
         "the first step requires explicit radio-disconnected evidence");
  expect(workflow.confirmDisconnectedLine(disconnectedEvidence(), 100),
         "complete disconnected-line evidence advances the workflow");
  expect(!workflow.readyForSafeHardwareOpen(),
         "manual inactive observation alone cannot authorize port use");
  expect(workflow.confirmPhysicalLoopback(loopbackEvidence(), 101),
         "complete physical-loopback evidence advances the workflow");
  expect(workflow.readyForSafeHardwareOpen() && !workflow.acceptedForOnAirUse(),
         "loopback permits later safe open but not on-air acceptance");
  expect(workflow.confirmDummyLoad(dummyLoadEvidence(), 102) &&
             workflow.acceptedForOnAirUse(),
         "only all three ordered steps produce accepted status");
}
void testEveryDummyLoadGateIsRequired() {
  for (int omitted = 0; omitted < 6; ++omitted) {
    HardwareAcceptanceWorkflow workflow;
    expect(workflow.setConfiguration(fingerprint(), HardwareAcceptancePlatform::MacOS) &&
               workflow.confirmDisconnectedLine(disconnectedEvidence(), 200) &&
               workflow.confirmPhysicalLoopback(loopbackEvidence(), 201),
           "dummy-load fixture reaches the final step");
    auto evidence = dummyLoadEvidence();
    switch (omitted) {
      case 0: evidence.dummy_load_connected = false; break;
      case 1: evidence.minimum_power_selected = false; break;
      case 2: evidence.message_keying_observed = false; break;
      case 3: evidence.cancellation_release_observed = false; break;
      case 4: evidence.watchdog_release_observed = false; break;
      case 5: evidence.emergency_release_observed = false; break;
    }
    expect(!workflow.confirmDummyLoad(evidence, 202) &&
               !workflow.acceptedForOnAirUse(),
           "no dummy-load safety observation is optional");
  }
}
void testConfigurationChangeInvalidatesEvidence() {
  HardwareAcceptanceWorkflow workflow;
  expect(workflow.setConfiguration(fingerprint('a'), HardwareAcceptancePlatform::Windows) &&
             workflow.confirmDisconnectedLine(disconnectedEvidence(), 300) &&
             workflow.confirmPhysicalLoopback(loopbackEvidence(), 301) &&
             workflow.confirmDummyLoad(dummyLoadEvidence(), 302),
         "invalidation fixture begins accepted");
  expect(workflow.setConfiguration(fingerprint('b'), HardwareAcceptancePlatform::Windows),
         "a new valid keying configuration can be selected");
  expect(workflow.status() == HardwareAcceptanceStatus::DisconnectedLineRequired &&
             !workflow.readyForSafeHardwareOpen() &&
             workflow.record().disconnected_line_utc_seconds == 0,
         "changing the configuration fingerprint clears all evidence");
  expect(workflow.setConfiguration(fingerprint('b'), HardwareAcceptancePlatform::Linux) &&
             workflow.status() == HardwareAcceptanceStatus::DisconnectedLineRequired,
         "acceptance is platform-specific");
}
void testRestoreIsStrictAndFailClosed() {
  HardwareAcceptanceWorkflow source;
  expect(source.setConfiguration(fingerprint(), HardwareAcceptancePlatform::Linux) &&
             source.confirmDisconnectedLine(disconnectedEvidence(), 400) &&
             source.confirmPhysicalLoopback(loopbackEvidence(), 401),
         "restore source has an ordered record prefix");
  HardwareAcceptanceWorkflow restored;
  expect(restored.setConfiguration(fingerprint(), HardwareAcceptancePlatform::Linux) &&
             restored.restore(source.record()) && restored.readyForSafeHardwareOpen() &&
             !restored.acceptedForOnAirUse(),
         "matching metadata restores only its attained milestone");
  auto malformed = source.record();
  malformed.dummy_load_utc_seconds = 399;
  expect(!restored.restore(malformed) && !restored.readyForSafeHardwareOpen(),
         "malformed persisted metadata clears prior evidence and fails closed");
  HardwareAcceptanceWorkflow mismatched;
  expect(mismatched.setConfiguration(fingerprint('c'), HardwareAcceptancePlatform::Linux) &&
             !mismatched.restore(source.record()) &&
             !mismatched.readyForSafeHardwareOpen(),
         "evidence for another configuration cannot be restored");
}
}  // namespace
int main() {
  testOrderedEvidenceAndMilestones();
  testEveryDummyLoadGateIsRequired();
  testConfigurationChangeInvalidatesEvidence();
  testRestoreIsStrictAndFailClosed();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
