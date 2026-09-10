#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

bool contains(const std::string_view source, const std::string_view text) {
  return source.find(text) != std::string_view::npos;
}

} // namespace

int main() {
  const std::string main_qml = readFile(CWA_MAIN_QML_PATH);
  const std::string qml = readFile(CWA_SETTINGS_QML_PATH);
  const std::string header = readFile(CWA_APP_SETTINGS_HPP_PATH);
  const std::string implementation = readFile(CWA_APP_SETTINGS_CPP_PATH);
  const std::string controller_header =
      readFile(CWA_REPLAY_CONTROLLER_HPP_PATH);
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
  for (const std::string_view forbidden :
       {"setSdrTransmit", "sdrPtt", "sdrKey", "startSdrTransmit"}) {
    if (contains(header, forbidden) || contains(qml, forbidden))
      return 4;
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
      !contains(qml, "function formatFrequencyKhz(frequencyHz)") ||
      !contains(qml, "function parseFrequencyKhz(value)") ||
      !contains(qml, "SDR center frequency (kHz)") ||
      !contains(qml, "placeholderText: \"7021.43\"") ||
      !contains(qml, "objectName: \"sdrOperatingModeCombo\"") ||
      !contains(qml, "appSettings.sdrOperatingModeNames") ||
      !contains(qml, "appSettings.selectSdrOperatingMode(currentIndex)") ||
      !contains(qml, "two synchronized RX channels") ||
      !contains(qml, "alternative 8 MHz master sample clock") ||
      !contains(main_qml, "objectName: \"sdrDecoderWindowOverlay\"") ||
      !contains(main_qml, "spectrumDisplay.zoomAt(") ||
      !contains(main_qml, "spectrumDisplay.panBy(") ||
      !contains(main_qml, "id: pointerHintLifetime") ||
      !contains(main_qml, "interval: 10000") ||
      !contains(main_qml, "id: pointerHintCooldown") ||
      !contains(main_qml, "interval: 300000") ||
      !contains(qml, "objectName: \"showSpectrumGestureHintsCheck\"") ||
      !contains(header, "showSpectrumGestureHints READ") ||
      !contains(implementation, "display/showSpectrumGestureHints") ||
      !contains(controller_header, "liveSdrDecoderWindowRequested") ||
      !contains(header, "sdrFollowRadioVfo")) {
    return 10;
  }

  // Alternative RSPduo operating configurations sharing one serial are
  // selected separately from the physical receiver. Stable physical/mode
  // keys supplement the legacy exact variant ID for profile migration.
  if (!contains(header, "sdrOperatingModeNames READ") ||
      !contains(header, "sdrOperatingModeIndex READ") ||
      !contains(header, "selectSdrOperatingMode(int index)") ||
      !contains(implementation, "groupSdrDevices(report.devices)") ||
      !contains(implementation, "sdr/physicalDeviceId") ||
      !contains(implementation, "sdr/deviceMode") ||
      !contains(implementation, "previous_variant_id.startsWith")) {
    return 12;
  }

  // Frequently used receive controls live beside the spectrum. The compact
  // panel remains RX-only, exposes only driver capabilities, and precedes the
  // independent CAT radio/TX panel.
  const std::size_t sdr_panel = main_qml.find("id: sdrRadioDisplay");
  const std::size_t cat_panel = main_qml.find("id: vfoDisplay");
  if (sdr_panel == std::string::npos || cat_panel == std::string::npos ||
      sdr_panel >= cat_panel ||
      !contains(main_qml, "objectName: \"sdrRxFrequencyLabel\"") ||
      !contains(main_qml, "objectName: \"sdrFrequencyDownButton\"") ||
      !contains(main_qml, "objectName: \"sdrFrequencyUpButton\"") ||
      !contains(main_qml, "objectName: \"sdrOperatingModeCombo\"") ||
      !contains(main_qml, "objectName: \"sdrControlAntennaCombo\"") ||
      !contains(main_qml, "objectName: \"sdrControlSampleRateCombo\"") ||
      !contains(main_qml, "objectName: \"sdrControlBandwidthCombo\"") ||
      !contains(main_qml, "objectName: \"sdrTuningStepCombo\"") ||
      !contains(main_qml, "objectName: \"sdrCatSyncButton\"") ||
      contains(qml, "objectName: \"sdrFollowRadioVfoCheck\"") ||
      !contains(main_qml, "appSettings.requestSdrRxFrequencyHz(") ||
      !contains(main_qml, "appSettings.stepSdrRxFrequency(-1)") ||
      !contains(main_qml, "appSettings.stepSdrRxFrequency(1)")) {
    return 12;
  }

  // The same edge arrows must tune whichever receiver currently owns the
  // displayed passband; direct SDR operation must not depend on CAT support.
  if (!contains(main_qml, "replayController.sourceMode === 2") ||
      !contains(main_qml, "? appSettings.stepSdrRxFrequency(-1)") ||
      !contains(main_qml, "? appSettings.stepSdrRxFrequency(1)")) {
    return 13;
  }

  // The SDR faceplate is styled like the CAT Radio Control faceplate: Radio
  // Sync is a square hand-drawn tile whose colour carries its state, not a
  // Material pill whose caption had to bake in an ellipsis to fit. Nothing on
  // the faceplate may be sized below its own implicit width, which is what
  // truncated the combo boxes, and the DEC RATE badge is gone because it had
  // no value binding and could never populate; its explanation now belongs to
  // the effective-IQ-rate control that actually determines decimation.
  if (!contains(main_qml, "id: sdrSyncTile") ||
      !contains(main_qml,
                "Layout.preferredWidth: sdrRadioDisplay.controlButtonSize") ||
      !contains(main_qml, "text: \"SYNC\"") ||
      contains(main_qml, "SYNC OFF") ||
      contains(main_qml, "objectName: \"sdrDecimationBadge\"") ||
      contains(main_qml, "DEC RATE") ||
      !contains(main_qml,
                "SoapySDR exposes no independent decimation control") ||
      contains(main_qml, "Layout.preferredWidth: 105") ||
      contains(main_qml, "Layout.preferredWidth: 95")) {
    return 14;
  }

  // Zoom and the decode window are independent controls, so the operator must
  // still be told where decoding is happening after zooming away from it.
  // Decoded streams report their frequency the way the VFO readout does
  // instead of as raw hertz.
  if (!contains(main_qml, "objectName: \"sdrDecoderWindowEdgeIndicator\"") ||
      !contains(main_qml, "function formatStreamFrequency(channel)") ||
      contains(main_qml, "modelData.frequencyLabel")) {
    return 15;
  }

  // Radio control remains visible with direct SDR reception so an independent
  // CAT radio can own the TX endpoint in a full-duplex profile.
  const std::size_t vfo_start = main_qml.find("id: vfoDisplay");
  const std::size_t vfo_end = main_qml.find("id: onAirIndicator", vfo_start);
  if (vfo_start == std::string::npos || vfo_end == std::string::npos ||
      contains(
          std::string_view(main_qml).substr(vfo_start, vfo_end - vfo_start),
          "sourceMode === 0") ||
      !contains(
          std::string_view(main_qml).substr(vfo_start, vfo_end - vfo_start),
          "visible: replayController.radioFrequencyAvailable")) {
    return 11;
  }
  // Opening the SDR page requests discovery once, after the page can render.
  // Application/profile construction must still never probe hardware.
  if (!contains(qml, "property bool sdrDiscoveryRequested: false") ||
      !contains(qml, "function requestInitialSdrDiscovery()") ||
      !contains(qml, "if (currentIndex === 1)") ||
      !contains(
          qml,
          "Qt.callLater(function() { appSettings.refreshSdrDevices() })") ||
      contains(implementation, "  refreshSdrDevices();") ||
      !contains(implementation, "canonicalFilePath()") ||
      !contains(implementation, "Qt::CaseInsensitive") ||
      !contains(header, "Open the SDR settings page or ") ||
      !contains(header,
                "press Refresh devices; live audio remains available.")) {
    return 9;
  }

  // The configured device reaches an explicit live receiver mode and worker;
  // it is not a settings-only mock. Source selection remains operator-driven.
  if (!contains(main_qml,
                "model: [\"Live audio\", \"WAV replay\", \"Live SDR\"]") ||
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
