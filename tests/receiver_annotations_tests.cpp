#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
  expect(manifest.sample_rate_hz == 8'000U && manifest.events.size() == 3U,
         "sample rate and all events are retained");
  expect(manifest.events[1].callsigns.size() == 2U &&
             manifest.events[2].uncertain,
         "multiple exact callsigns and uncertainty are represented");
  expect(cwassistant::test::fileSha256(CWA_RECEIVER_ANNOTATION_FIXTURE) ==
             "1b97efc9d0fce580a5810d2679ef4ca00a86af5d6ad15795358382bff3f0553f",
         "dependency-free file SHA-256 matches a fixed external digest");
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
  testMalformedInputFailsClosed();
  if (failures != 0) return EXIT_FAILURE;
  std::cout << "Receiver annotation tests passed\n";
  return EXIT_SUCCESS;
}
