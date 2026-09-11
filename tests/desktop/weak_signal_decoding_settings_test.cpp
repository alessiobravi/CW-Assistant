// Source contract: the operator's weak-signal decoding preference must reach
// the live decoder without costing the operator anything already on screen.
//
// Three properties are worth pinning down. The preference has to persist per
// profile like its neighbours, or it silently reverts on the next launch. The
// threshold control has to go inert while every signal is decoded, because a
// number that still looks editable while deciding nothing misleads the
// operator about what the decoder is doing. And the worker slot must not reset
// the decoder: this setting changes only which tracked signals are decoded,
// never the audio the detector receives, so resetting would throw away every
// track, transcript and confirmed callsign for a preference the channel bank
// honours from its next update.
//
// This is a text-level contract because the wiring lives in Qt objects and a
// QML document that the dependency-free test suite cannot instantiate.

#include <algorithm>
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

bool readSource(const char* path, std::string& contents) {
  std::ifstream source(path, std::ios::binary);
  if (!source) return false;
  contents.assign(std::istreambuf_iterator<char>{source},
                  std::istreambuf_iterator<char>{});
  normalizeLineEndings(contents);
  return true;
}

// Both values are stored under the profile's own decoder keys, restored with
// the documented defaults, and bounded before they are written.
bool persistsBothValuesPerProfile(const std::string& header,
                                  const std::string& implementation) {
  return contains(header, "bool decode_weak_signals_{false};") &&
         contains(header, "double minimum_decode_snr_db_{12.0};") &&
         contains(implementation,
                  "storageKey(QStringLiteral(\"decoder/decodeWeakSignals\"))") &&
         contains(implementation,
                  "storageKey(QStringLiteral(\"decoder/minimumDecodeSnrDb\"))") &&
         contains(implementation, "\"decoder/minimumDecodeSnrDb\")), 12.0)") &&
         contains(implementation,
                  "std::clamp(minimum_decode_snr_db_, 0.0, 40.0)");
}

// The controls live together in the decoder settings section, between the
// keying model and the local model, so the operator meets them as one choice.
std::string weakSignalSection(const std::string& qml) {
  const auto begin = qml.find("Label { text: \"Weak signals\" }");
  const auto end = qml.find("Label { text: \"Local model\" }", begin);
  if (begin == std::string::npos || end == std::string::npos) return {};
  return qml.substr(begin, end - begin);
}

// The threshold does not apply while every tracked signal is decoded, so its
// control says so by going inert, and its unit is stated in decibels.
bool thresholdControlGoesInertWhenEveryTrackIsDecoded(
    const std::string& section) {
  return contains(section, "objectName: \"decodeWeakSignalsCheck\"") &&
         contains(section, "objectName: \"minimumDecodeSnrDbSpin\"") &&
         contains(section, "enabled: !appSettings.decodeWeakSignals") &&
         contains(section, "dB above the noise floor");
}

// The controller forwards the pair to the live worker over the same
// signal/slot route its neighbouring decoder settings use.
bool forwardsThePairToTheLiveWorker(const std::string& controller_header,
                                    const std::string& controller_source) {
  return contains(controller_header,
                  "void liveWeakSignalDecodingRequested(bool enabled,") &&
         contains(controller_header,
                  "void setWeakSignalDecoding(bool enabled, double "
                  "minimum_decode_snr_db);") &&
         contains(controller_source,
                  "emit liveWeakSignalDecodingRequested(decode_weak_signals_,") &&
         contains(controller_source,
                  "connect(this, &ReplayController::"
                  "liveWeakSignalDecodingRequested, dsp_worker,\n"
                  "          &LiveAudioDspWorker::setWeakSignalDecoding);");
}

std::string weakSignalSlot(const std::string& worker) {
  const auto begin =
      worker.find("void LiveAudioDspWorker::setWeakSignalDecoding(");
  if (begin == std::string::npos) return {};
  const auto end = worker.find("\n}\n", begin);
  if (end == std::string::npos) return {};
  return worker.substr(begin, end - begin);
}

// The worker slot calls the bank and does nothing else. A reset here would
// destroy live tracks and transcripts for a setting outside the signal path.
bool workerSlotDoesNotResetTheDecoder(const std::string& slot) {
  return contains(slot, "decoder_.setWeakSignalDecoding(enabled,") &&
         !contains(slot, "decoder_.reset()") &&
         !contains(slot, "character_frontends_.reset()") &&
         !contains(slot, "decoder_analyzer_.reset()");
}

// An operator preference about which signals are decoded may never acquire a
// transmit or keying side effect, in the settings section or in the slot.
bool staysReceiveOnly(const std::string& section, const std::string& slot) {
  for (const std::string& forbidden :
       {std::string{"Ptt"}, std::string{"ptt"}, std::string{"transmit"},
        std::string{"Transmit"}, std::string{"keyDown"}}) {
    if (contains(section, forbidden) || contains(slot, forbidden))
      return false;
  }
  return true;
}

}  // namespace

int main() {
  std::string settings_header;
  if (!readSource(CWA_APP_SETTINGS_HPP_PATH, settings_header)) return 1;
  std::string settings_source;
  if (!readSource(CWA_APP_SETTINGS_CPP_PATH, settings_source)) return 2;
  std::string settings_qml;
  if (!readSource(CWA_SETTINGS_QML_PATH, settings_qml)) return 3;
  std::string controller_header;
  if (!readSource(CWA_REPLAY_CONTROLLER_HPP_PATH, controller_header)) return 4;
  std::string controller_source;
  if (!readSource(CWA_REPLAY_CONTROLLER_CPP_PATH, controller_source)) return 5;
  std::string live_worker;
  if (!readSource(CWA_LIVE_AUDIO_WORKER_CPP_PATH, live_worker)) return 6;

  const std::string section = weakSignalSection(settings_qml);
  const std::string slot = weakSignalSlot(live_worker);
  if (section.empty()) return 7;
  if (slot.empty()) return 8;

  if (!persistsBothValuesPerProfile(settings_header, settings_source))
    return 9;
  if (!thresholdControlGoesInertWhenEveryTrackIsDecoded(section)) return 10;
  if (!forwardsThePairToTheLiveWorker(controller_header, controller_source))
    return 11;
  if (!workerSlotDoesNotResetTheDecoder(slot)) return 12;
  if (!staysReceiveOnly(section, slot)) return 13;

  return 0;
}
