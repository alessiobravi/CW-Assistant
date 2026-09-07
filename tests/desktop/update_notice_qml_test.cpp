// Source contract for the startup update notice. A pending application or
// callsign-list update must be surfaced once per launch, after any first-run
// setup, without downloading anything before the operator asks.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  std::ifstream stream(CWA_MAIN_QML_PATH);
  if (!stream) return 1;
  std::stringstream buffer;
  buffer << stream.rdbuf();
  const std::string qml = buffer.str();
  if (qml.empty()) return 2;

  // Both update sources reach the notice, and it offers the action for each.
  if (!contains(qml, "objectName: \"updateNoticeDialog\"") ||
      !contains(qml, "updateChecker.updateAvailable") ||
      !contains(qml, "callsignDatabaseUpdater.updateAvailable") ||
      !contains(qml, "objectName: \"updateNoticeDownloadAppButton\"") ||
      !contains(qml, "objectName: \"updateNoticeOpenAppButton\"") ||
      !contains(qml, "objectName: \"updateNoticeRevealAppButton\"") ||
      !contains(qml, "objectName: \"updateNoticeAppStatusLabel\"") ||
      !contains(qml, "objectName: \"updateNoticeUpdateListButton\"") ||
      !contains(qml, "updateChecker.downloadUpdate()") ||
      !contains(qml, "updateChecker.openDownloadedFile()") ||
      !contains(qml, "updateChecker.revealDownloadFolder()") ||
      !contains(qml, "callsignDatabaseUpdater.updateDatabase()")) {
    return 3;
  }

  // Match the state transition in Settings -> About: after checksum
  // verification, replace Download with open/reveal actions and retain a
  // visible verification result.
  if (!contains(qml, "visible: updateChecker.downloadActionVisible") ||
      !contains(qml,
                "visible: updateChecker.verifiedDownloadActionsVisible") ||
      !contains(qml, "property bool appDownloadStarted: false") ||
      !contains(qml, "updateNotice.appDownloadStarted = true") ||
      !contains(qml, "updateChecker.downloadVerified ?") ||
      !contains(qml, "text: updateChecker.statusMessage")) {
    return 6;
  }

  // Shown once per launch, and never over first-run setup: a profile chooser
  // or setup wizard owns the screen until the operator finishes with it.
  if (!contains(qml, "shownThisLaunch") ||
      !contains(qml, "if (appSettings.profileSelectionRequired) return") ||
      !contains(qml, "if (!appSettings.setupComplete) return") ||
      !contains(qml, "function considerShowing()")) {
    return 4;
  }

  // A later arriving check still surfaces, so the notice does not depend on
  // the state already being known when the window is constructed.
  if (!contains(qml, "function onStateChanged() { updateNotice.considerShowing() }"))
    return 5;
  return 0;
}
