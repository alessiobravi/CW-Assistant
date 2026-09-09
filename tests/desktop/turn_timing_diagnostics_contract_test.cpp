#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readSource(const char* path) {
  std::ifstream source(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{source},
          std::istreambuf_iterator<char>{}};
}

bool containsEvery(const std::string& source,
                   const std::initializer_list<std::string_view> fields) {
  for (const auto field : fields) {
    if (source.find(field) == std::string::npos) return false;
  }
  return true;
}

}  // namespace

int main() {
  const std::string capture = readSource(CWA_LIVE_AUDIO_WORKER_PATH);
  if (capture.empty() || !containsEvery(capture, {
          "timingFingerprint", "firstObservationId", "lastObservationId",
          "evidenceStartedNs", "evidenceEndedNs", "markCount", "gapCount",
          "ditCount", "dahCount", "elementGapCount", "characterGapCount",
          "wordGapCount", "ditMedianMs", "dahMedianMs",
          "elementGapMedianMs", "characterGapMedianMs", "wordGapMedianMs",
          "keyingWeight", "normalizedMarkResidual", "evidenceConfidence",
          "QJsonValue::Null"})) {
    return 1;
  }

  const std::string replay = readSource(CWA_CAPTURE_REPLAY_PATH);
  if (replay.empty() || !containsEvery(replay, {
          "timing_fingerprint=unavailable", "timing_first_observation_id=",
          "timing_last_observation_id=", "timing_started_ns=",
          "timing_ended_ns=", "timing_mark_count=", "timing_gap_count=",
          "timing_dit_count=", "timing_dah_count=",
          "timing_element_gap_count=", "timing_character_gap_count=",
          "timing_word_gap_count=", "timing_dit_median_ms=",
          "timing_dah_median_ms=", "timing_element_gap_median_ms=",
          "timing_character_gap_median_ms=", "timing_word_gap_median_ms=",
          "timing_keying_weight=", "timing_normalized_mark_residual=",
          "timing_evidence_confidence="})) {
    return 2;
  }
  return 0;
}
