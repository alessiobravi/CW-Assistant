#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "support/decoder_evaluation.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_text_metrics() {
  const auto cer = cwassistant::test::characterErrorRate(
      "cq  test", "CQ TAST");
  const auto wer = cwassistant::test::wordErrorRate(
      "cq  test", "CQ TAST");
  expect(cer.edits == 1U && cer.references == 7U,
         "CER is case-insensitive and canonicalizes whitespace");
  expect(wer.edits == 1U && wer.references == 2U,
         "WER counts a substituted word once");

  const auto insertion = cwassistant::test::wordErrorRate("CQ", "CQ DX");
  expect(insertion.edits == 1U && std::abs(insertion.rate() - 1.0) < 1e-9,
         "insertions remain visible when normalizing by reference words");
}

void test_callsign_metrics() {
  const auto counts = cwassistant::test::callsignCounts(
      {"W1AW", "EA1EYL", "EA1EYL"}, {"ea1eyl", "K1ABC"});
  expect(counts.true_positives == 1U && counts.false_positives == 1U &&
             counts.false_negatives == 1U,
         "callsign matching is exact, case-insensitive, and set-based");
  expect(std::abs(counts.precision() - 0.5) < 1e-9 &&
             std::abs(counts.recall() - 0.5) < 1e-9,
         "callsign precision and recall use independent denominators");

  const auto quiet = cwassistant::test::callsignCounts({}, {});
  expect(quiet.precision() == 1.0 && quiet.recall() == 1.0,
         "an empty negative fixture is a perfect abstention");
}

}  // namespace

int main() {
  test_text_metrics();
  test_callsign_metrics();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "Decoder evaluation metric tests passed\n";
  return EXIT_SUCCESS;
}
