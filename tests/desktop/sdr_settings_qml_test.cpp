#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

bool contains(const std::string_view source, const std::string_view text) {
  return source.find(text) != std::string_view::npos;
}

}  // namespace

int main() {
  const std::string main_qml = readFile(CWA_MAIN_QML_PATH);
  const std::string qml = readFile(CWA_SETTINGS_QML_PATH);
  const std::string header = readFile(CWA_APP_SETTINGS_HPP_PATH);
  const std::string implementation = readFile(CWA_APP_SETTINGS_CPP_PATH);
  const std::string controller_header = readFile(CWA_REPLAY_CONTROLLER_HPP_PATH);
  const std::string controller_implementation =
      readFile(CWA_REPLAY_CONTROLLER_CPP_PATH);
  if (main_qml.empty() || qml.empty() || header.empty() ||
      implementation.empty() || controller_header.empty() ||
      controller_implementation.empty()) {
    return 1;
  }

  // The user can see and configure a distinct wide-passband source without
  // losing the conventional sound-card path.
  if (!contains(qml, "TabButton { text: \"SDR\" }") ||
      !contains(qml, "objectName: \"receiverInputTypeCombo\"") ||
      !contains(qml, "objectName: \"sdrDeviceCombo\"") ||
      !contains(qml, "objectName: \"sdrCenterFrequencyField\"") ||
      !contains(qml, "objectName: \"sdrSampleRateCombo\"") ||
      !contains(qml, "objectName: \"sdrWidePassbandLabel\"") ||
      !contains(qml, "No external SDR application is required")) {
    return 2;
  }

  // An unavailable backend is explicit and actionable, and every hardware
  // control remains inert while normal audio remains selected.
  if (!contains(qml, "enabled: appSettings.sdrBackendAvailable") ||
      !contains(qml, "SDR is unavailable in this build") ||
      !contains(qml, "sound-card audio remains fully operational") ||
      !contains(implementation, "receiver_input_type_index_ = 0") ||
      !contains(implementation, "This build has no SoapySDR support")) {
    return 3;
  }

  // The persisted UI contract includes only receive configuration. It must
  // not grow a transmit, PTT, or KEY entry point under an SDR name.
  for (const std::string_view forbidden : {
           "setSdrTransmit", "sdrPtt", "sdrKey", "startSdrTransmit"}) {
    if (contains(header, forbidden) || contains(qml, forbidden)) return 4;
  }
  if (!contains(header, "Q_INVOKABLE void refreshSdrDevices()") ||
      !contains(header, "Q_INVOKABLE void selectSdrDevice(int index)")) {
    return 5;
  }

  // Wide acquisition and bounded decoding are separate operator controls.
  // Standard capability-driven settings remain provider-neutral, and the
  // radio-follow route consumes authoritative readback rather than UI intent.
  if (!contains(qml, "objectName: \"sdrBandwidthCombo\"") ||
      !contains(qml, "objectName: \"sdrAntennaCombo\"") ||
      !contains(qml, "objectName: \"sdrDecoderBandwidthCombo\"") ||
      !contains(qml, "objectName: \"sdrFollowRadioVfoCheck\"") ||
      !contains(qml, "RSPduo entries are operating modes") ||
      !contains(main_qml, "objectName: \"sdrDecoderWindowOverlay\"") ||
      !contains(main_qml, "spectrumDisplay.zoomAt(") ||
      !contains(main_qml, "spectrumDisplay.panBy(") ||
      !contains(controller_header, "liveSdrDecoderWindowRequested") ||
      !contains(header, "sdrFollowRadioVfo")) {
    return 10;
  }
  // Opening the SDR page requests discovery once, after the page can render.
  // Application/profile construction must still never probe hardware.
  if (!contains(qml, "property bool sdrDiscoveryRequested: false") ||
      !contains(qml, "function requestInitialSdrDiscovery()") ||
      !contains(qml, "if (currentIndex === 1)") ||
      !contains(qml,
                "Qt.callLater(function() { appSettings.refreshSdrDevices() })") ||
      contains(implementation, "  refreshSdrDevices();") ||
      !contains(implementation, "canonicalFilePath()") ||
      !contains(implementation, "Qt::CaseInsensitive") ||
      !contains(header, "Open the SDR settings page or press Refresh devices")) {
    return 9;
  }

  // The configured device reaches an explicit live receiver mode and worker;
  // it is not a settings-only mock. Source selection remains operator-driven.
  if (!contains(main_qml, "model: [\"Live audio\", \"WAV replay\", \"Live SDR\"]") ||
      !contains(main_qml, "objectName: \"startLiveSdrButton\"") ||
      !contains(main_qml, "replayController.startLiveSdr()") ||
      !contains(controller_header, "Q_INVOKABLE void startLiveSdr()") ||
      !contains(controller_implementation, "new SdrCaptureWorker(pipe)") ||
      !contains(controller_implementation, "emit sdrStartRequested(")) {
    return 6;
  }

  // Wide IQ is never presented as full-receiver audio, and the label hover is
  // deliberately modest so dense markers remain readable.
  if (!contains(main_qml, "replayController.sourceMode !== 2") ||
      !contains(main_qml,
                "font.pixelSize: channelMarker.pointerHovered ? 22 : 18")) {
    return 7;
  }

  // LiveAudioPipe is SPSC. The mutually exclusive audio and SDR capture
  // workers must share one serialized producer thread during source changes.
  if (!contains(controller_implementation,
                "sdr_worker->moveToThread(&audio_capture_thread_)") ||
      contains(controller_header, "QThread sdr_capture_thread_") ||
      !contains(controller_implementation,
                "source_mode_ == 2 && monitor_mode_ == 1")) {
    return 8;
  }

  return 0;
}
