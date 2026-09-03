#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

bool contains(const std::string& value, const std::string& expected) {
  return value.find(expected) != std::string::npos;
}

}  // namespace

int main() {
  std::ifstream source(CWA_MAIN_QML_PATH, std::ios::binary);
  if (!source) return 1;
  std::string qml{std::istreambuf_iterator<char>{source},
                  std::istreambuf_iterator<char>{}};
  // Git may materialize text files with CRLF on Windows. The QML contract is
  // line-ending independent, so normalize before matching its bounded block.
  qml.erase(std::remove(qml.begin(), qml.end(), '\r'), qml.end());
  const std::size_t start = qml.find("id: transcriptScroll");
  const std::size_t end = qml.find("Layout.fillWidth: true\n                                text: modelData.wpm", start);
  if (start == std::string::npos || end == std::string::npos) return 2;
  const std::string transcript = qml.substr(start, end - start);
  const std::size_t callsign_start = qml.find(
      "property string callsignEvidenceText:");
  const std::size_t callsign_end = qml.find(
      "property string localModelState:", callsign_start);

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
      !contains(qml, "property string displayedDecodedText: rawDecodedText") ||
      contains(qml, "Acoustic correction:") ||
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
      !contains(qml, "objectName: \"localModelTranscriptPanel\"") ||
      !contains(qml, "objectName: \"localModelStateLabel\"") ||
      !contains(qml, "objectName: \"localModelStatusLabel\"") ||
      !contains(qml, "objectName: \"localModelTranscriptText\"") ||
      !contains(qml, "id: localModelTranscriptScroll") ||
      !contains(qml, "property string localModelStableText:") ||
      !contains(qml, "property string localModelCallsign:") ||
      !contains(qml, "text: \"MODEL\"") ||
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
      !contains(transcript, "height: Math.max(") ||
      !contains(transcript, "transcriptScroll.availableHeight") ||
      !contains(transcript, "implicitHeight)") ||
      !contains(transcript, "ScrollBar.horizontal: ScrollBar") ||
      !contains(transcript, "policy: ScrollBar.AlwaysOff") ||
      !contains(transcript, "ScrollBar.vertical: ScrollBar") ||
      !contains(transcript, "policy: ScrollBar.AlwaysOn")) {
    return 4;
  }

  const std::size_t manual_start = qml.find("id: manualSliceHitArea");
  const std::size_t marker_start = qml.find("id: channelMarker");
  const std::size_t marker_end = qml.find("ToolTip.text:", marker_start);
  if (manual_start == std::string::npos || marker_start == std::string::npos ||
      marker_end == std::string::npos || manual_start >= marker_start) {
    return 5;
  }
  const std::string manual = qml.substr(manual_start,
                                        marker_start - manual_start);
  const std::string marker = qml.substr(marker_start,
                                        marker_end - marker_start);
  if (!contains(manual, "objectName: \"manualSliceHitArea\"") ||
      !contains(manual, "z: 6") ||
      !contains(manual, "acceptedButtons: Qt.LeftButton | Qt.RightButton") ||
      !contains(manual, "function streamIdAtX(positionX)") ||
      !contains(manual, "replayController.openDecoderSession(streamId)") ||
      !contains(manual, "mouse.button === Qt.LeftButton") ||
      !contains(manual, "onClicked: function(mouse)") ||
      !contains(manual, "spectrumDisplay.lowerFrequencyHz") ||
      !contains(manual, "appSettings.cwGuideCenterHz = frequencyHz") ||
      !contains(manual, "replayController.openManualDecoderSession(frequencyHz)") ||
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
  if (!contains(toolbar, "z: 20") ||
      !contains(toolbar, "objectName: \"startLiveAudioButton\"") ||
      !contains(toolbar, "z: 21") ||
      !contains(spectrum_panel, "clip: true") ||
      !contains(spectrum_panel, "z: 0") ||
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
      !contains(settings_qml, "replayController.localCharacterStatus") ||
      !contains(settings_qml, "replayController.localCharacterState")) {
    return 10;
  }
  return 0;
}
