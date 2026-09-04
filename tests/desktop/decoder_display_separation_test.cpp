// Source contract: display settings must not reach the decoder.
//
// Two operator-visible defects motivated this check. The receive workers used
// to hand CwChannelBank the display-averaged spectrum, so the Avg slider
// changed candidate discovery and the decoded text. They also reset the
// decoder unconditionally on every configure() call, so moving any display
// control discarded every track, transcript and confirmed callsign while the
// user-interface model kept showing the previous contents.
//
// This is a text-level contract because the wiring lives in Qt worker objects
// that the dependency-free test suite cannot instantiate. The behavioural
// guarantee itself is covered by the core suite's display-invariance case.

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

// Detection must consume the unaveraged bins so that the analyzer's display
// averaging cannot change which signals are found. The detector applies its
// own fixed-time smoothing internally.
bool feedsDetectorUnaveragedBins(const std::string& source) {
  return contains(source,
                  "snapshot.upper_frequency_hz, "
                  "snapshot.instantaneous_bins_dbfs))") &&
         !contains(source,
                   "snapshot.upper_frequency_hz, snapshot.bins_dbfs))");
}

// Only a change to the audio actually presented to the detector may discard
// decoder state. Averaging and the display line rate must not.
bool resetsOnlyOnSignalPathChange(const std::string& source) {
  return contains(source, "const bool signal_path_changed =") &&
         contains(source, "config.audio_lower_frequency_hz != "
                          "previous.audio_lower_frequency_hz") &&
         contains(source, "if (signal_path_changed)") &&
         !contains(source,
                   "static_cast<void>(analyzer_.configure(config));\n"
                   "  decoder_.reset();") &&
         !contains(source,
                   "static_cast<void>(analyzer_.configure(config));\n"
                   "    decoder_.reset();");
}

}  // namespace

int main() {
  std::string live_worker;
  if (!readSource(CWA_LIVE_AUDIO_WORKER_PATH, live_worker)) return 1;
  std::string replay_controller;
  if (!readSource(CWA_REPLAY_CONTROLLER_PATH, replay_controller)) return 2;

  std::string crlf_probe{"guard\r\ncheck\r\n"};
  normalizeLineEndings(crlf_probe);
  if (crlf_probe != "guard\ncheck\n") return 3;

  if (!feedsDetectorUnaveragedBins(live_worker)) return 4;
  if (!feedsDetectorUnaveragedBins(replay_controller)) return 5;
  if (!resetsOnlyOnSignalPathChange(live_worker)) return 6;
  if (!resetsOnlyOnSignalPathChange(replay_controller)) return 7;

  return 0;
}
