#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

void test_timestamped_callsign_publication() {
  using cwassistant::test::TimestampedPublication;
  const std::vector<TimestampedPublication> points{
      {100U, true, {"W1AW"}},
      {220U, true, {"EA1EYL", "W1AW"}},
      {360U, false, {}},
  };
  expect(cwassistant::test::callsignsAtOrBefore(points, 99U).empty(),
         "an event before first publication sees no callsign");
  expect(cwassistant::test::callsignsAtOrBefore(points, 100U) ==
             std::vector<std::string>{"W1AW"},
         "publication is visible at its exact sample");
  expect(cwassistant::test::callsignsAtOrBefore(points, 300U) ==
             (std::vector<std::string>{"EA1EYL", "W1AW"}),
         "the most recent publication set is selected");
  expect(cwassistant::test::callsignsAtOrBefore(points, 400U).empty(),
         "an explicit later withdrawal clears the published set");
}

void test_publication_episodes() {
  using cwassistant::test::SampleEpisode;
  using cwassistant::test::TimestampedPublication;
  const std::vector<TimestampedPublication> points{
      {100U, true, {"W1AW"}},
      {220U, true, {"EA1EYL", "W1AW"}},
      {300U, false, {}},
      {400U, true, {"K1ABC"}},
  };
  const auto publications =
      cwassistant::test::publicationEpisodes(points, 500U);
  expect(publications.size() == 2U &&
             publications[0].start_sample == 100U &&
             publications[0].end_sample == 300U &&
             publications[1].start_sample == 400U &&
             publications[1].end_sample == 500U,
         "published-to-withdrawn transitions form independent episodes");

  const auto callsigns = cwassistant::test::callsignEpisodes(points, 500U);
  expect(callsigns.size() == 3U && callsigns[0].callsign == "W1AW" &&
             callsigns[0].start_sample == 100U &&
             callsigns[0].end_sample == 300U &&
             callsigns[1].callsign == "EA1EYL" &&
             callsigns[1].start_sample == 220U &&
             callsigns[1].end_sample == 300U &&
             callsigns[2].callsign == "K1ABC" &&
             callsigns[2].start_sample == 400U &&
             callsigns[2].end_sample == 500U,
         "every transient callsign has its own lifetime");

  expect(!cwassistant::test::halfOpenOverlaps({100U, 200U},
                                               {200U, 300U}) &&
             cwassistant::test::halfOpenOverlaps({100U, 201U},
                                                  {200U, 300U}),
         "adjacent half-open intervals do not overlap");
  expect(cwassistant::test::callsignEpisodeIsProtected(true, true, false) &&
             cwassistant::test::callsignEpisodeIsProtected(false, false,
                                                            true) &&
             !cwassistant::test::callsignEpisodeIsProtected(false, true,
                                                             true) &&
             !cwassistant::test::callsignEpisodeIsProtected(false, false,
                                                             false),
         "certain calls protect exact labels and override uncertainty");
}

}  // namespace

int main() {
  test_text_metrics();
  test_callsign_metrics();
  test_timestamped_callsign_publication();
  test_publication_episodes();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "Decoder evaluation metric tests passed\n";
  return EXIT_SUCCESS;
}
