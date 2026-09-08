#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <iostream>

#include "transmit/transmit_controller.hpp"

namespace {
int failures = 0;

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  ++failures;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  cwassistant::desktop::TransmitController controller;

  expect(!controller.arm(), "arming requires the operator's own callsign");
  controller.setOwnCallsign(QStringLiteral(" iu0lfq/p "));
  expect(controller.arm(), "a normalized configured callsign permits arming");
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
  expect(!controller.transmitPrepared() && !controller.hardwareAvailable() &&
             !controller.onAir(),
         "the desktop cannot claim transmission without a tested adapter");

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

  cwassistant::desktop::TransmitController uncertain_controller;
  uncertain_controller.setOwnCallsign(QStringLiteral("IU0LFQ"));
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

  controller.emergencyRelease();
  expect(controller.state() == QStringLiteral("fault") && !controller.armed() &&
             controller.preparedMessage().isEmpty(),
         "emergency release clears pending TX and latches a fault");
  expect(controller.resetFault() &&
             controller.state() == QStringLiteral("disarmed"),
         "fault reset never silently re-arms TX");

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
