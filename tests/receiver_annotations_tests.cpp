#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "support/receiver_annotations.hpp"
#include "support/file_sha256.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void testValidFixture() {
  std::ifstream input(CWA_RECEIVER_ANNOTATION_FIXTURE);
  cwassistant::test::ReceiverAnnotationManifest manifest;
  std::string error;
  expect(input.is_open(), "public annotation fixture opens");
  expect(cwassistant::test::parseReceiverAnnotations(input, manifest, error),
         "public annotation fixture parses: " + error);
  expect(manifest.version == 1U && manifest.sample_rate_hz == 8'000U &&
             manifest.events.size() == 3U && manifest.coverage.empty(),
         "sample rate and all events are retained");
  expect(manifest.events[1].callsigns.size() == 2U &&
             manifest.events[2].uncertain,
         "multiple exact callsigns and uncertainty are represented");
  expect(cwassistant::test::fileSha256(CWA_RECEIVER_ANNOTATION_FIXTURE) ==
             "1b97efc9d0fce580a5810d2679ef4ca00a86af5d6ad15795358382bff3f0553f",
         "dependency-free file SHA-256 matches a fixed external digest");
}

void testVersionTwoReviewedCoverage() {
  constexpr std::string_view hash =
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const std::string reviewed =
      "CWA-RECEIVER-ANNOTATIONS\t2\n"
      "sample_rate_hz\t8000\n"
      "audio_sha256\t" + std::string(hash) + "\n"
      "coverage\t8000\t24000\n"
      "event\t10000\t18000\t700\tCQ W1AW\tCQ W1AW\tW1AW\t0\n"
      "event\t19000\t22000\t710\tUNCLEAR\tUNCLEAR\t\t1\n"
      "coverage\t32000\t40000\n";
  std::istringstream input(reviewed);
  cwassistant::test::ReceiverAnnotationManifest manifest;
  std::string error;
  expect(cwassistant::test::parseReceiverAnnotations(input, manifest, error),
         "version 2 reviewed coverage parses: " + error);
  expect(manifest.version == 2U && manifest.coverage.size() == 2U &&
             manifest.events.size() == 2U,
         "version 2 retains coverage and certain/uncertain events");
  expect(cwassistant::test::reviewedCoverageSamples(manifest.coverage) ==
             24'000U,
         "reviewed duration sums disjoint coverage intervals");
  expect(cwassistant::test::overlapsReviewedCoverage(
             23'000U, 33'000U, manifest.coverage) &&
             !cwassistant::test::overlapsReviewedCoverage(
                 24'000U, 32'000U, manifest.coverage) &&
             !cwassistant::test::overlapsReviewedCoverage(
                 0U, 8'000U, manifest.coverage) &&
             !cwassistant::test::overlapsReviewedCoverage(
                 40'000U, 48'000U, manifest.coverage),
         "half-open coverage excludes gaps and adjacent intervals");
  expect(cwassistant::test::receiverAnnotationsFitAudio(manifest, 40'000U) &&
             !cwassistant::test::receiverAnnotationsFitAudio(
                 manifest, 39'999U),
         "event and coverage bounds are checked against exact audio length");

  const std::string no_cw =
      "CWA-RECEIVER-ANNOTATIONS\t2\n"
      "sample_rate_hz\t8000\n"
      "audio_sha256\t" + std::string(hash) + "\n"
      "coverage\t0\t8000\n";
  std::istringstream no_cw_input(no_cw);
  expect(cwassistant::test::parseReceiverAnnotations(
             no_cw_input, manifest, error) && manifest.events.empty(),
         "event-free reviewed coverage represents explicit no-CW audio");
}

void testInvalidCoverageFailsClosed() {
  constexpr const char* invalid_cases[]{
      "CWA-RECEIVER-ANNOTATIONS\t2\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "coverage\t100\t300\ncoverage\t200\t400\n",
      "CWA-RECEIVER-ANNOTATIONS\t2\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "coverage\t100\t300\n"
      "event\t200\t400\t700\tCQ\tCQ\t\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "coverage\t100\t300\n"
      "event\t100\t200\t700\tCQ\tCQ\t\t0\n",
  };
  for (const char* invalid : invalid_cases) {
    std::istringstream input(invalid);
    cwassistant::test::ReceiverAnnotationManifest manifest;
    std::string error;
    expect(!cwassistant::test::parseReceiverAnnotations(input, manifest, error),
           "invalid, overlapping, or version-incompatible coverage is rejected");
    expect(!error.empty(), "rejected coverage explains the failure");
  }
}

void testMalformedInputFailsClosed() {
  constexpr const char* malformed_cases[]{
      "CWA-RECEIVER-ANNOTATIONS\t2\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\txyz\nevent\t0\t1\t700\tA\tA\t\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "event\t10\t5\t700\tA\tA\t\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "event\t0\t10\t700\tcq\tcq\tW1AW\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "event\t20\t30\t700\tCQ\tCQ\tW1AW\t0\n"
      "event\t10\t15\t710\tDE\tDE\tEA1EYL\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "event\t0\t10\t700\tCQ\tCQ\tw1aw\t0\n",
      "CWA-RECEIVER-ANNOTATIONS\t1\nsample_rate_hz\t8000\n"
      "audio_sha256\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
      "event\t0\t10\t4001\tCQ\tCQ\tW1AW\t0\n",
  };
  for (const char* malformed : malformed_cases) {
    std::istringstream input(malformed);
    cwassistant::test::ReceiverAnnotationManifest manifest;
    std::string error;
    expect(!cwassistant::test::parseReceiverAnnotations(input, manifest, error),
           "malformed or incomplete sidecar is rejected");
    expect(!error.empty(), "a rejected sidecar explains the failure");
  }
}

}  // namespace

int main() {
  testValidFixture();
  testVersionTwoReviewedCoverage();
  testInvalidCoverageFailsClosed();
  testMalformedInputFailsClosed();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "Receiver annotation tests passed\n";
  return EXIT_SUCCESS;
}
