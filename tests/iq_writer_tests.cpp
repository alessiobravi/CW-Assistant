#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/iq_writer.hpp"
#include "cwassistant/core/sample_block.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path scratch_path(const std::string_view name) {
  return std::filesystem::temp_directory_path() / std::string(name);
}

void remove_recording(const std::filesystem::path& data_path) {
  std::error_code error;
  std::filesystem::remove(data_path, error);
  std::filesystem::remove(
      cwassistant::core::IqWriter::metadataPathFor(data_path.string()), error);
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
}

std::int16_t decode_i16_le(const std::vector<unsigned char>& bytes,
                           const std::size_t offset) {
  const auto raw = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1])
                                 << 8U));
  return static_cast<std::int16_t>(raw);
}

float decode_f32_le(const std::vector<unsigned char>& bytes,
                    const std::size_t offset) {
  const std::uint32_t raw =
      static_cast<std::uint32_t>(bytes[offset]) |
      (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
      (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
  float value = 0.0F;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

bool contains(const std::string& haystack, const std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

// Every field is filled explicitly: a partial designated initializer trips
// -Wmissing-field-initializers in the strict GCC build.
cwassistant::core::IqCaptureMetadata metadataFor(
    const cwassistant::core::IqSampleFormat format, const double sample_rate_hz,
    const double center_frequency_hz) {
  cwassistant::core::IqCaptureMetadata metadata;
  metadata.format = format;
  metadata.sample_rate_hz = sample_rate_hz;
  metadata.center_frequency_hz = center_frequency_hz;
  metadata.datetime_utc = "2026-01-02T03:04:05.000Z";
  return metadata;
}

cwassistant::core::RealtimeSampleBlock iqBlock(
    const double sample_rate_hz, const double center_frequency_hz,
    const std::size_t sample_count) {
  cwassistant::core::RealtimeSampleBlock block;
  block.stream = {.kind = cwassistant::core::StreamKind::ComplexIq,
                  .sample_rate_hz = sample_rate_hz,
                  .center_frequency_hz = center_frequency_hz,
                  .channel_count = 1};
  block.sample_count = sample_count;
  return block;
}

// The whole reason this writer exists: the audio WavWriter records only the
// real component, which destroys the sideband distinction of a complex
// receiver. Assert that a known complex sequence comes back with its
// imaginary parts intact and in the right interleaved order.
void testComplexRoundTrip() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_roundtrip.sigmf-data");
  remove_recording(path);

  auto metadata =
      metadataFor(IqSampleFormat::Ci16Le, 250'000.0, 14'050'000.0);
  metadata.hardware = "Test receiver";
  metadata.description = "Round-trip fixture";
  metadata.automatic_gain_known = true;
  metadata.automatic_gain = true;

  IqWriter writer;
  expect(writer.open(path.string(), metadata), "a SigMF recording opens");

  auto block = iqBlock(250'000.0, 14'050'000.0, 5);
  const std::complex<float> expected[5] = {
      {0.0F, 0.5F}, {0.25F, -0.25F}, {-0.5F, 0.0F},
      {0.125F, 0.75F}, {-0.75F, -0.125F}};
  for (std::size_t index = 0; index < 5; ++index) {
    block.samples[index] = expected[index];
  }
  expect(writer.writeBlock(block), "a complex block is written");
  expect(writer.samplesWritten() == 5, "every sample of the block is recorded");
  expect(writer.bytesWritten() == 5 * 4,
         "ci16 costs four bytes per complex sample");
  writer.close();

  const auto bytes = read_bytes(path);
  expect(bytes.size() == 20, "the data file holds exactly the interleaved pairs");
  bool interleaved_match = bytes.size() == 20;
  bool imaginary_survived = false;
  for (std::size_t index = 0; index < 5 && bytes.size() == 20; ++index) {
    const auto real = decode_i16_le(bytes, index * 4);
    const auto imaginary = decode_i16_le(bytes, index * 4 + 2);
    const auto want_real =
        static_cast<std::int16_t>(std::lround(expected[index].real() * 32'767.0F));
    const auto want_imaginary =
        static_cast<std::int16_t>(std::lround(expected[index].imag() * 32'767.0F));
    if (real != want_real || imaginary != want_imaginary) interleaved_match = false;
    if (imaginary != 0) imaginary_survived = true;
  }
  expect(interleaved_match,
         "both components round-trip in interleaved I,Q order");
  expect(imaginary_survived,
         "the imaginary component is preserved, unlike the audio WAV path");

  remove_recording(path);
}

void testComplexFloatRoundTripIsBitExact() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_cf32.sigmf-data");
  remove_recording(path);

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Cf32Le, 48'000.0, 7'030'000.0)),
         "a cf32 recording opens");
  auto block = iqBlock(48'000.0, 7'030'000.0, 3);
  const std::complex<float> expected[3] = {
      {0.123456789F, -0.987654321F}, {1.0F, -1.0F}, {-3.5e-7F, 2.25e-3F}};
  for (std::size_t index = 0; index < 3; ++index) {
    block.samples[index] = expected[index];
  }
  expect(writer.writeBlock(block), "a cf32 block is written");
  expect(writer.bytesWritten() == 3 * 8,
         "cf32 costs eight bytes per complex sample");
  writer.close();

  const auto bytes = read_bytes(path);
  bool exact = bytes.size() == 24;
  for (std::size_t index = 0; index < 3 && bytes.size() == 24; ++index) {
    if (decode_f32_le(bytes, index * 8) != expected[index].real() ||
        decode_f32_le(bytes, index * 8 + 4) != expected[index].imag()) {
      exact = false;
    }
  }
  expect(exact, "cf32 preserves the provider's samples bit for bit");
  expect(contains(read_text(IqWriter::metadataPathFor(path.string())),
                  "\"core:datatype\": \"cf32_le\""),
         "the sidecar names the cf32 datatype");

  remove_recording(path);
}

void testSidecarDescribesTheRecording() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_sidecar.sigmf-data");
  remove_recording(path);

  auto metadata =
      metadataFor(IqSampleFormat::Ci16Le, 8'000'000.0, 14'050'000.0);
  metadata.hardware = "SDRplay RSPduo \"Single Tuner\"";
  metadata.description = "Line one\nline two";
  metadata.datetime_utc = "2026-03-04T05:06:07.008Z";
  metadata.automatic_gain_known = true;
  metadata.automatic_gain = true;
  metadata.gain_db = 42.5;

  IqWriter writer;
  expect(writer.open(path.string(), metadata), "a high-rate recording opens");
  const auto metadata_path = IqWriter::metadataPathFor(path.string());
  expect(writer.metadataPath() == metadata_path,
         "the sidecar path replaces the .sigmf-data extension");
  expect(std::filesystem::exists(metadata_path),
         "the sidecar exists from the moment the capture starts, so an "
         "interrupted recording still describes itself");

  auto block = iqBlock(8'000'000.0, 14'050'000.0, 8);
  for (std::size_t index = 0; index < 8; ++index) {
    block.samples[index] = {0.5F, -0.25F};
  }
  expect(writer.writeBlock(block), "the block is written");

  // A mid-capture retune must become a new SigMF capture segment rather than
  // silently mislabelling the samples that follow it.
  auto retuned = iqBlock(8'000'000.0, 14'060'000.0, 4);
  for (std::size_t index = 0; index < 4; ++index) {
    retuned.samples[index] = {0.1F, 0.1F};
  }
  expect(writer.writeBlock(retuned), "a retuned block is written");
  writer.close();

  const std::string sidecar = read_text(metadata_path);
  expect(contains(sidecar, "\"core:datatype\": \"ci16_le\""),
         "the sidecar names the default ci16_le datatype");
  expect(contains(sidecar, "\"core:sample_rate\": 8000000"),
         "the sidecar records the true sample rate");
  expect(contains(sidecar, "\"core:version\": \"1.0.0\""),
         "the sidecar declares a SigMF version");
  expect(contains(sidecar, "\"core:frequency\": 14050000"),
         "the first capture segment records the centre frequency");
  expect(contains(sidecar, "\"core:frequency\": 14060000"),
         "a retune opens a second capture segment");
  expect(contains(sidecar, "\"core:sample_start\": 8"),
         "the second segment starts at the sample where the retune landed");
  expect(contains(sidecar, "\"core:datetime\": \"2026-03-04T05:06:07.008Z\""),
         "the supplied capture time is recorded verbatim");
  expect(contains(sidecar, "SDRplay RSPduo \\\"Single Tuner\\\""),
         "quotes inside operator-visible text are escaped");
  expect(contains(sidecar, "Line one\\nline two"),
         "newlines inside operator-visible text are escaped");
  expect(contains(sidecar, "\"cwbuddy:automatic_gain\": true") &&
             contains(sidecar, "\"cwbuddy:gain_db\": 42.5"),
         "the gain state travels with the recording");
  expect(contains(sidecar, "\"cwbuddy:sample_count\": 12"),
         "the final sample count is written on close");
  expect(contains(sidecar, "\"cwbuddy:peak_magnitude\""),
         "level telemetry is written alongside the samples");

  remove_recording(path);
}

// Bounds are expressed in bytes and seconds, not in a frame count shaped for
// an audio rate: at 8 MS/s the audio path's 30-minute frame cap is ~21 s.
void testDurationBudgetStopsAndReports() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_duration.sigmf-data");
  remove_recording(path);

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Ci16Le, 1'000.0, 0.0),
                     {.maximum_data_bytes = 1'000'000'000ULL,
                      .maximum_seconds = 0.01}),
         "a duration-bounded recording opens");
  auto block = iqBlock(1'000.0, 0.0, 40);
  expect(!writer.writeBlock(block),
         "the writer stops once the duration budget is spent");
  expect(writer.samplesWritten() == 10,
         "exactly the budgeted 10 ms of samples are recorded");
  expect(writer.stopReason() == IqCaptureStopReason::DurationBudget,
         "the duration budget is reported as the reason");
  expect(!writer.writeBlock(block), "no further block is accepted");
  expect(writer.samplesWritten() == 10, "a stopped capture writes nothing more");
  writer.close();
  expect(read_bytes(path).size() == 40,
         "the data file holds only the budgeted samples");
  expect(contains(read_text(IqWriter::metadataPathFor(path.string())),
                  "\"cwbuddy:stop_reason\": \"Reached the capture duration "
                  "budget\""),
         "the sidecar explains why the recording ended");

  remove_recording(path);
}

void testByteBudgetStopsAndReports() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_bytes.sigmf-data");
  remove_recording(path);

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Ci16Le, 1'000'000.0, 0.0),
                     {.maximum_data_bytes = 24, .maximum_seconds = 3'600.0}),
         "a byte-bounded recording opens");
  auto block = iqBlock(1'000'000.0, 0.0, 32);
  expect(!writer.writeBlock(block),
         "the writer stops once the byte budget is spent");
  expect(writer.bytesWritten() == 24, "the byte budget is respected exactly");
  expect(writer.stopReason() == IqCaptureStopReason::ByteBudget,
         "the byte budget is reported as the reason");
  writer.close();
  expect(read_bytes(path).size() == 24,
         "the data file never exceeds the byte budget");

  remove_recording(path);
}

// A provider that overshoots unity must saturate, never wrap: a wrapped
// sample flips sign at full scale and reads as a violent discontinuity to any
// later analysis.
void testCi16ScalingClampsRatherThanWraps() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_clamp.sigmf-data");
  remove_recording(path);

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Ci16Le, 48'000.0, 0.0)),
         "a clamping fixture opens");
  auto block = iqBlock(48'000.0, 0.0, 4);
  block.samples[0] = {1.0F, -1.0F};
  block.samples[1] = {2.5F, -2.5F};
  block.samples[2] = {1'000.0F, -1'000.0F};
  block.samples[3] = {0.999985F, -0.999985F};
  expect(writer.writeBlock(block), "an over-range block is written");
  writer.close();

  const auto bytes = read_bytes(path);
  expect(bytes.size() == 16, "four complex samples are recorded");
  bool clamped = bytes.size() == 16;
  for (std::size_t index = 0; index < 4 && bytes.size() == 16; ++index) {
    const auto real = decode_i16_le(bytes, index * 4);
    const auto imaginary = decode_i16_le(bytes, index * 4 + 2);
    if (real != 32'767 || imaginary != -32'767) clamped = false;
  }
  expect(clamped,
         "every over-range component saturates at +/-32767 instead of wrapping");

  const auto levels = writer.captureLevels();
  expect(levels.near_full_scale_samples == 4,
         "the near-full-scale counter flags every clipping sample");
  expect(levels.peak_magnitude > 1'000.0,
         "the peak magnitude records the real input level, not the clamp");

  remove_recording(path);
}

void testLevelTelemetryIsDiagnostic() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_levels.sigmf-data");
  remove_recording(path);

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Ci16Le, 48'000.0, 0.0)),
         "a telemetry fixture opens");
  auto quiet = iqBlock(48'000.0, 0.0, 4);
  for (std::size_t index = 0; index < 4; ++index) {
    quiet.samples[index] = {0.25F, 0.75F};
  }
  expect(writer.writeBlock(quiet), "a quiet block is written");
  expect(writer.lastBlockLevels().near_full_scale_samples == 0,
         "a quiet block reports no near-full-scale samples");
  expect(std::abs(writer.lastBlockLevels().mean_real - 0.25) < 1e-6 &&
             std::abs(writer.lastBlockLevels().mean_imaginary - 0.75) < 1e-6,
         "the complex mean reports the DC offset of the block");
  expect(std::abs(writer.lastBlockLevels().peak_magnitude -
                  std::sqrt(0.25 * 0.25 + 0.75 * 0.75)) < 1e-6,
         "peak magnitude is the complex magnitude, not a per-component peak");

  auto loud = iqBlock(48'000.0, 0.0, 2);
  loud.samples[0] = {0.99F, 0.0F};
  loud.samples[1] = {0.0F, 0.0F};
  expect(writer.writeBlock(loud), "a loud block is written");
  expect(writer.lastBlockLevels().near_full_scale_samples == 1,
         "the per-block counter reports only the newest block");
  expect(writer.captureLevels().near_full_scale_samples == 1 &&
             writer.captureLevels().samples == 6,
         "the capture totals accumulate across blocks");
  writer.close();

  remove_recording(path);
}

void testMalformedInputIsRefused() {
  using namespace cwassistant::core;
  const auto path = scratch_path("cwa_iq_writer_refusal.sigmf-data");
  remove_recording(path);

  IqWriter rejected;
  expect(!rejected.open(path.string(),
                        metadataFor(IqSampleFormat::Ci16Le, 0.0, 0.0)),
         "a recording without a sample rate is refused");

  IqWriter writer;
  expect(writer.open(path.string(),
                     metadataFor(IqSampleFormat::Ci16Le, 48'000.0, 0.0)),
         "a valid recording opens");
  cwassistant::core::RealtimeSampleBlock audio;
  audio.stream = {.kind = StreamKind::Audio,
                  .sample_rate_hz = 48'000.0,
                  .center_frequency_hz = 0.0,
                  .channel_count = 1};
  audio.sample_count = 4;
  expect(!writer.writeBlock(audio),
         "an audio block is refused rather than recorded as meaningless IQ");
  expect(writer.stopReason() == IqCaptureStopReason::WriteError,
         "the refusal is reported as a write error");
  writer.close();

  IqWriter retimed;
  const auto second = scratch_path("cwa_iq_writer_retimed.sigmf-data");
  remove_recording(second);
  expect(retimed.open(second.string(),
                      metadataFor(IqSampleFormat::Ci16Le, 48'000.0, 0.0)),
         "a rate-change fixture opens");
  auto good = iqBlock(48'000.0, 0.0, 2);
  expect(retimed.writeBlock(good), "the first block is written");
  auto changed = iqBlock(96'000.0, 0.0, 2);
  expect(!retimed.writeBlock(changed),
         "a mid-stream sample-rate change ends the capture");
  expect(retimed.stopReason() == IqCaptureStopReason::SampleRateChanged,
         "the sample-rate change is reported, because SigMF has one global "
         "rate and the later samples cannot be described by this file");
  retimed.close();

  remove_recording(path);
  remove_recording(second);
}

}  // namespace

int main() {
  testComplexRoundTrip();
  testComplexFloatRoundTripIsBitExact();
  testSidecarDescribesTheRecording();
  testDurationBudgetStopsAndReports();
  testByteBudgetStopsAndReports();
  testCi16ScalingClampsRatherThanWraps();
  testLevelTelemetryIsDiagnostic();
  testMalformedInputIsRefused();
  if (failures == 0) {
    std::cout << "All IQ writer tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
