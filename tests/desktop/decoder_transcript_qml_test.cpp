#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

bool contains(const std::string& value, const std::string& expected) {
  return value.find(expected) != std::string::npos;
}

void normalizeLineEndings(std::string& value) {
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
}

}  // namespace

int main() {
  std::ifstream source(CWA_MAIN_QML_PATH, std::ios::binary);
  if (!source) return 1;
  std::string qml{std::istreambuf_iterator<char>{source},
                  std::istreambuf_iterator<char>{}};
  // Git may materialize text files with CRLF on Windows. The QML contract is
  // line-ending independent, so normalize before matching its bounded block.
  normalizeLineEndings(qml);
  const std::size_t start = qml.find("id: transcriptScroll");
  const std::size_t end = qml.find("Layout.fillWidth: true\n                                text: modelData.wpm", start);
  const std::size_t session_card_start = qml.find("id: sessionCard");
  const std::size_t local_panel_start = qml.find(
      "id: localModelTranscriptPanel", start);
  if (start == std::string::npos || end == std::string::npos ||
      session_card_start == std::string::npos ||
      local_panel_start == std::string::npos || session_card_start >= start ||
      local_panel_start >= end) {
    return 2;
  }
  const std::string transcript = qml.substr(start, end - start);
  const std::string session_header = qml.substr(
      session_card_start, start - session_card_start);
  const std::string local_panel = qml.substr(
      local_panel_start, end - local_panel_start);
  const std::size_t callsign_start = qml.find(
      "property string callsignEvidenceText:");
  const std::size_t callsign_end = qml.find(
      "property string ownCallEvidenceText:", callsign_start);

  // Appends may move only the viewport. Moving the TextEdit cursor caused
  // Qt to repeatedly ensure it was visible, disturbing selection and making
  // every live update jump through intermediate layouts.
  if (!contains(transcript, "textFormat: TextEdit.PlainText") ||
      contains(transcript, "TextEdit.RichText") ||
      contains(transcript, "cursorPosition") ||
      !contains(transcript, "contentItem.contentY") ||
      !contains(transcript, "decodedTextArea.selectionStart") ||
      !contains(transcript, "decodedTextArea.selectionEnd") ||
      !contains(transcript, "function applyDecodedText(nextText)") ||
      contains(qml, "Acoustic correction:") ||
      !contains(qml, "modelData.refinedText.length > 0") ||
      !contains(qml, "? modelData.refinedText : rawDecodedText") ||
      !contains(transcript, "onDisplayedDecodedTextChanged()") ||
      !contains(transcript, "select(Math.min(oldSelectionStart") ||
      !contains(transcript, "if (!followTail)") ||
      !contains(transcript, "function onMovementStarted()") ||
      !contains(transcript, "followTail = false")) {
    return 3;
  }
  if (callsign_start == std::string::npos ||
      callsign_end == std::string::npos ||
      contains(qml.substr(callsign_start, callsign_end - callsign_start),
               "localModel") ||
      contains(qml.substr(callsign_start, callsign_end - callsign_start),
               "callsignSuggestion") ||
      !contains(qml, "objectName: \"localModelTranscriptPanel\"") ||
      !contains(qml, "objectName: \"localModelStateLabel\"") ||
      !contains(qml, "objectName: \"localModelStatusLabel\"") ||
      !contains(qml, "objectName: \"localModelTranscriptText\"") ||
      !contains(qml, "id: localModelTranscriptScroll") ||
      !contains(qml, "property string localModelStableText:") ||
      !contains(qml, "property string ownCallEvidenceText:") ||
      !contains(qml,
                "callsignEvidenceText + \" \" + localModelStableText") ||
      !contains(qml, "property string localModelCallsign:") ||
      !contains(qml, "text: \"MODEL\"") ||
      !contains(qml, "objectName: \"advisoryCallsignSuggestionBadge\"") ||
      !contains(qml, "property string advisoryCallsignSuggestion:") ||
      !contains(qml, "property string callsignSuggestionSource:") ||
      !contains(qml, "\"≈ \" + sessionCard.advisoryCallsignSuggestion") ||
      !contains(qml, "Advisory acoustic consensus") ||
      !contains(qml, "sessionCard.localModelCallsign") ||
      !contains(qml, "function applyStableText(nextText)") ||
      !contains(qml, "function onLocalModelStateChanged()") ||
      !contains(qml, "localModelTranscriptScroll.followAppendedText()") ||
      !contains(qml, "localModelTranscriptScroll.availableWidth") ||
      !contains(qml, "nextText.indexOf(text)") ||
      contains(qml, "rawDecodedText + \" \" + modelData.localModelText")) {
    return 11;
  }

  // Keep wrapping width independent of scrollbar visibility, with no
  // horizontal scrollbar and a permanently reserved vertical gutter.
  if (!contains(transcript, "width: transcriptScroll.availableWidth") ||
      !contains(transcript,
                "wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere") ||
      !contains(transcript, "height: Math.max(") ||
      !contains(transcript, "transcriptScroll.availableHeight") ||
      !contains(transcript, "implicitHeight)") ||
      !contains(transcript, "ScrollBar.horizontal: ScrollBar") ||
      !contains(transcript, "policy: ScrollBar.AlwaysOff") ||
      !contains(transcript, "ScrollBar.vertical: ScrollBar") ||
      !contains(transcript, "policy: ScrollBar.AlwaysOn") ||
      !contains(session_header,
                "height: Math.ceil(sessionCardLayout.implicitHeight + 20)") ||
      !contains(session_header, "id: sessionCardLayout") ||
      !contains(session_header, "clip: true") ||
      !contains(session_header, "elide: Text.ElideRight") ||
      !contains(local_panel, "sessionCard.localModelHasText ? 88 : 50") ||
      !contains(local_panel, "clip: true")) {
    return 4;
  }

  const std::size_t manual_start = qml.find("id: manualSliceHitArea");
  const std::size_t tune_down_start = qml.find("id: tuneRxDownButton");
  const std::size_t tune_up_start = qml.find("id: tuneRxUpButton");
  const std::size_t marker_start = qml.find("id: channelMarker");
  const std::size_t marker_end = qml.find("ToolTip.text:", marker_start);
  if (manual_start == std::string::npos ||
      tune_down_start == std::string::npos ||
      tune_up_start == std::string::npos ||
      marker_start == std::string::npos ||
      marker_end == std::string::npos || manual_start >= tune_down_start ||
      tune_down_start >= tune_up_start || tune_up_start >= marker_start) {
    return 5;
  }
  const std::string manual = qml.substr(manual_start,
                                        marker_start - manual_start);
  const std::string marker = qml.substr(marker_start,
                                        marker_end - marker_start);
  const std::string tune_down = qml.substr(
      tune_down_start, tune_up_start - tune_down_start);
  const std::string tune_up = qml.substr(tune_up_start,
                                         marker_start - tune_up_start);
  if (!contains(manual, "objectName: \"manualSliceHitArea\"") ||
      !contains(manual, "z: 6") ||
      !contains(manual, "enabled: replayController.activeSource") ||
      !contains(manual, "acceptedButtons: Qt.LeftButton | Qt.RightButton") ||
      !contains(manual, "function streamIdAtX(positionX)") ||
      !contains(manual, "replayController.openDecoderSession(streamId)") ||
      !contains(manual, "mouse.button === Qt.LeftButton") ||
      !contains(manual, "onClicked: function(mouse)") ||
      !contains(manual, "spectrumDisplay.lowerFrequencyHz") ||
      !contains(manual, "appSettings.cwGuideCenterHz = frequencyHz") ||
      !contains(manual, "replayController.openManualDecoderSession(frequencyHz)") ||
      !contains(tune_down, "objectName: \"tuneRxDownButton\"") ||
      !contains(tune_down, "appSettings.radioFrequencyWritable") ||
      !contains(tune_down, "z: 8") ||
      !contains(tune_down, "appSettings.stepControlledRxFrequency(-1)") ||
      !contains(tune_down, "Accessible.name: \"Tune RX down\"") ||
      !contains(tune_up, "objectName: \"tuneRxUpButton\"") ||
      !contains(tune_up, "appSettings.radioFrequencyWritable") ||
      !contains(tune_up, "z: 8") ||
      !contains(tune_up, "appSettings.stepControlledRxFrequency(1)") ||
      !contains(tune_up, "Accessible.name: \"Tune RX up\"") ||
      !contains(marker, "z: 5") ||
      contains(marker, "id: channelHitArea") ||
      !contains(marker, "manualSliceHitArea.hoveredStreamId")) {
    return 6;
  }
  const std::size_t toolbar_start = qml.find("id: receiverToolbar");
  const std::size_t spectrum_start = qml.find("id: spectrumPanel");
  const std::size_t display_start = qml.find("id: spectrumDisplay",
                                              spectrum_start);
  if (toolbar_start == std::string::npos ||
      spectrum_start == std::string::npos ||
      display_start == std::string::npos ||
      toolbar_start >= spectrum_start || spectrum_start >= display_start) {
    return 7;
  }
  const std::string toolbar = qml.substr(toolbar_start,
                                         spectrum_start - toolbar_start);
  const std::string spectrum_panel = qml.substr(
      spectrum_start, display_start - spectrum_start);
  const std::size_t vfo_editor_start = qml.find("id: vfoRxEditor");
  const std::size_t vfo_badge_start = qml.find("objectName: \"vfoSplitBadge\"",
                                               vfo_editor_start);
  if (vfo_editor_start == std::string::npos ||
      vfo_badge_start == std::string::npos) {
    return 8;
  }
  const std::string vfo_editor = qml.substr(
      vfo_editor_start, vfo_badge_start - vfo_editor_start);
  if (!contains(toolbar, "z: 20") ||
      !contains(toolbar, "objectName: \"startLiveAudioButton\"") ||
      !contains(toolbar, "z: 21") ||
      !contains(spectrum_panel, "clip: true") ||
      !contains(spectrum_panel, "z: 0") ||
      !contains(qml, "objectName: \"emptyStateStartButton\"") ||
      !contains(vfo_editor, "objectName: \"vfoRxEditor\"") ||
      !contains(vfo_editor, "objectName: \"vfoRxEditHitArea\"") ||
      !contains(vfo_editor, "objectName: \"vfoRxFrequencyField\"") ||
      !contains(vfo_editor, "appSettings.setControlledRxFrequency(") ||
      !contains(vfo_editor, "function dismissEdit()") ||
      !contains(vfo_editor, "function cancelEdit()") ||
      !contains(vfo_editor, "function onRadioFrequencyControlChanged()") ||
      !contains(vfo_editor, "onActiveFocusChanged:") ||
      !contains(vfo_editor, "event.key === Qt.Key_Escape") ||
      !contains(vfo_editor, "event.key === Qt.Key_Return") ||
      !contains(vfo_editor, "event.accepted = true") ||
      !contains(vfo_editor, "vfoRxEditor.dismissEdit()") ||
      contains(vfo_editor, "Keys.onReturnPressed:") ||
      contains(vfo_editor, "Keys.onEnterPressed:") ||
      !contains(vfo_editor, "font.family: \"monospace\"") ||
      !contains(vfo_editor, "color: \"#06130e\"") ||
      !contains(vfo_editor, "Accessible.name: \"Edit RX frequency\"") ||
      !contains(qml, "objectName: \"vfoRxEditErrorLabel\"") ||
      !contains(qml, "objectName: \"moveDecoderSessionUpButton\"") ||
      !contains(qml, "objectName: \"moveDecoderSessionDownButton\"") ||
      !contains(qml, "onPressed: replayController.moveDecoderSession(") ||
      !contains(qml, "objectName: \"closeDecoderSessionButton\"") ||
      !contains(qml, "onPressed: replayController.closeDecoderSession(")) {
    return 8;
  }

  const auto settings_path =
      std::filesystem::path(CWA_MAIN_QML_PATH).parent_path() /
      "SettingsPane.qml";
  std::ifstream settings_source(settings_path, std::ios::binary);
  if (!settings_source) return 9;
  const std::string settings_qml{
      std::istreambuf_iterator<char>{settings_source},
      std::istreambuf_iterator<char>{}};
  if (!contains(settings_qml, "TabButton { text: \"Decoder\" }") ||
      !contains(settings_qml, "objectName: \"localDecoderEnabledCheck\"") ||
      !contains(settings_qml, "appSettings.localDecoderBackendAvailable") ||
      !contains(settings_qml, "objectName: \"localDecoderModelDialog\"") ||
      !contains(settings_qml, "appSettings.selectLocalDecoderModel(selectedFile)") ||
      !contains(settings_qml, "objectName: \"localDecoderMetadataDialog\"") ||
      !contains(settings_qml, "appSettings.selectLocalDecoderMetadata(selectedFile)") ||
      !contains(settings_qml, "objectName: \"localDecoderStatusLabel\"") ||
      !contains(settings_qml, "appSettings.localDecoderStatus") ||
      !contains(settings_qml, "objectName: \"localCallsignDatabaseEnabledCheck\"") ||
      !contains(settings_qml, "objectName: \"localCallsignDatabasePathField\"") ||
      !contains(settings_qml, "objectName: \"reloadLocalCallsignDatabaseButton\"") ||
      !contains(settings_qml, "appSettings.reloadLocalCallsignDatabase()") ||
      !contains(settings_qml, "objectName: \"localCallsignDatabaseStatusLabel\"") ||
      !contains(settings_qml, "replayController.offlineCallsignDatabaseStatus") ||
      !contains(settings_qml, "objectName: \"localCallsignDatabaseDialog\"") ||
      !contains(settings_qml, "appSettings.selectLocalCallsignDatabase(selectedFile)") ||
      !contains(settings_qml,
                "objectName: \"managedCallsignDatabaseEnabledCheck\"") ||
      !contains(settings_qml,
                "objectName: \"managedCallsignDatabaseAutoUpdateCheck\"") ||
      !contains(settings_qml,
                "objectName: \"managedCallsignDatabaseUpdateButton\"") ||
      !contains(settings_qml,
                "objectName: \"managedCallsignDatabaseStatusLabel\"") ||
      !contains(settings_qml,
                "callsignDatabaseUpdater.checkForUpdates()") ||
      !contains(settings_qml,
                "callsignDatabaseUpdater.updateDatabase()") ||
      !contains(settings_qml,
                "never confirm a stream, replace decoded text") ||
      !contains(settings_qml, "replayController.localCharacterStatus") ||
      !contains(settings_qml, "replayController.localCharacterState") ||
      !contains(settings_qml, "objectName: \"radioTuningStepSlider\"") ||
      !contains(settings_qml, "value: appSettings.radioTuningStepHz / 1000") ||
      !contains(settings_qml,
                "appSettings.radioTuningStepHz = Math.round(value * 1000)")) {
    return 10;
  }

  std::ifstream main_source(CWA_DESKTOP_MAIN_CPP_PATH, std::ios::binary);
  if (!main_source) return 12;
  std::string main_cpp{
      std::istreambuf_iterator<char>{main_source},
      std::istreambuf_iterator<char>{}};
  normalizeLineEndings(main_cpp);
  std::string crlf_probe{"guard\r\ncheck\r\n"};
  normalizeLineEndings(crlf_probe);
  if (crlf_probe != "guard\ncheck\n") return 14;
  if (!contains(main_cpp,
                "QStringLiteral(\"callsignDatabaseUpdater\")") ||
      !contains(main_cpp,
                "CallsignDatabaseUpdater::databaseInstalled") ||
      !contains(main_cpp,
                "callsign_database_updater.installedFilePath()") ||
      !contains(main_cpp,
                "smoke_test = parser.isSet(smoke_test_option)") ||
      !contains(main_cpp,
                "!parser.isSet(smoke_test_option) &&\n"
                "      callsign_database_updater.managedEnabled()") ||
      !contains(main_cpp,
                "callsign_database_updater.checkAndInstallIfDue()")) {
    return 13;
  }
  return 0;
}
