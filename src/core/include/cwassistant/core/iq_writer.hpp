#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

// On-disk sample encoding, named exactly as SigMF's `core:datatype` spells it.
//
// `Ci16Le` is the default because it is lossless for the receivers this
// application actually records: the RSPduo digitizes at 14 bits and the
// RTL-SDR at 8, so nothing below the noise floor is discarded by rounding a
// normalized float to a 16-bit integer, while the file is half the size of
// `cf32_le`. At multi-megasample rates that halving is the difference between
// a usable forensic capture and filling the disk. `Cf32Le` stays available for
// a bit-exact copy of what the provider handed us, for the rare case where the
// scaling itself is under investigation.
enum class IqSampleFormat { Ci16Le, Cf32Le };

// Why a capture ended. Reported to the operator so a short file is never
// mistaken for a hardware fault or a silent failure.
enum class IqCaptureStopReason {
  None,
  ByteBudget,
  DurationBudget,
  SampleRateChanged,
  WriteError,
};

// Both bounds are passed in rather than baked into the writer: the only
// sensible limit depends entirely on the sample rate, and a constant shaped
// for 96 kHz audio (as the WAV path's is) becomes a few seconds at 8 MS/s.
struct IqWriterLimits {
  // Payload bytes, excluding the sidecar. The default is the largest single
  // file a FAT32-formatted removable drive can hold, which is still ~134 s of
  // ci16 at 8 MS/s -- a generous forensic window. Raise it deliberately when
  // the capture target is a filesystem without that limit.
  std::uint64_t maximum_data_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
  // Derived from samples written and the declared rate, not from a wall clock,
  // so a capture is bounded identically on a fast disk, a slow disk and in a
  // deterministic test.
  double maximum_seconds{300.0};
};

struct IqCaptureMetadata {
  IqSampleFormat format{IqSampleFormat::Ci16Le};
  double sample_rate_hz{0.0};
  double center_frequency_hz{0.0};
  // SigMF `core:hw` / `core:description`: free text describing the receiver
  // and why the recording was made.
  std::string hardware;
  std::string description;
  // SigMF `core:datetime` for the first capture segment. Filled from the
  // system clock when empty; supplied explicitly by tests so the sidecar is
  // byte-for-byte reproducible.
  std::string datetime_utc;
  // Gain state at the moment the capture started. A recording without it
  // documents the symptom and not the cause: an overloaded front end and a
  // starved one produce very different spectra from the same antenna.
  bool automatic_gain_known{false};
  bool automatic_gain{false};
  double gain_db{0.0};
};

// Cheap level statistics computed while the samples are already in cache.
// Together with the gain state these answer the first question any post-hoc
// analysis asks: was the ADC clipping, or was the signal down in the dither?
struct IqLevelSummary {
  std::uint64_t samples{0};
  // Largest |I + jQ| seen. Magnitude rather than per-component peak because a
  // complex signal can clip the ADC while neither component is at full scale.
  double peak_magnitude{0.0};
  // Samples with either component at or above kNearFullScale. A non-zero and
  // growing count is the ADC-overload indicator the application otherwise
  // lacks entirely.
  std::uint64_t near_full_scale_samples{0};
  // Complex mean, i.e. the DC/LO leakage offset. A large value explains a
  // centre-bin spike that is not a signal.
  double mean_real{0.0};
  double mean_imaginary{0.0};
};

// One SigMF capture segment. A new one is started whenever the provider
// retunes mid-stream, so a recording that spans a frequency change stays
// self-describing instead of silently mislabelling half its samples.
struct IqCaptureSegment {
  std::uint64_t sample_start{0};
  double frequency_hz{0.0};
  std::string datetime_utc;
};

// Dependency-free streaming writer for a SigMF recording: interleaved complex
// samples in a `.sigmf-data` file plus a `.sigmf-meta` JSON sidecar.
//
// Unlike the audio WavWriter this preserves both components. Discarding Q
// destroys the sideband distinction, which is exactly the information an RF
// post-analysis of a complex receiver needs, so an I-only "IQ" capture is
// worse than none. Never invoked implicitly; always operator-started and
// always bounded.
class IqWriter final {
 public:
  // A sample counts as near full scale at or above this normalized level.
  // Chosen just under 1.0: providers scale the last ADC code to slightly less
  // than unity, so requiring an exact 1.0 would report overload as never
  // happening.
  static constexpr double kNearFullScale = 0.98;

  IqWriter() = default;
  ~IqWriter();

  IqWriter(const IqWriter&) = delete;
  IqWriter& operator=(const IqWriter&) = delete;

  // `data_path` should end in `.sigmf-data`; the sidecar replaces that
  // extension with `.sigmf-meta`, as the format requires.
  [[nodiscard]] bool open(std::string_view data_path,
                          const IqCaptureMetadata& metadata,
                          IqWriterLimits limits = {});
  // Writes every sample in the block, both components. Returns false once the
  // capture has stopped -- consult stopReason() before treating that as an
  // error, because reaching a configured bound is a normal ending.
  [[nodiscard]] bool writeBlock(const RealtimeSampleBlock& block);
  // Rewrites the sidecar with the final sample count, segments and telemetry.
  // Safe to call repeatedly; the destructor calls it if still open.
  void close() noexcept;

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] std::uint64_t samplesWritten() const noexcept;
  [[nodiscard]] std::uint64_t bytesWritten() const noexcept;
  [[nodiscard]] double secondsWritten() const noexcept;
  [[nodiscard]] IqCaptureStopReason stopReason() const noexcept;
  [[nodiscard]] std::string_view stopReasonText() const noexcept;
  // Level statistics for the most recently written block, for per-block
  // diagnostic logging, and for the capture as a whole, for the sidecar.
  [[nodiscard]] const IqLevelSummary& lastBlockLevels() const noexcept;
  [[nodiscard]] IqLevelSummary captureLevels() const noexcept;
  [[nodiscard]] const std::string& dataPath() const noexcept;
  [[nodiscard]] const std::string& metadataPath() const noexcept;
  [[nodiscard]] const std::string& lastError() const noexcept;

  [[nodiscard]] static std::string_view datatypeName(
      IqSampleFormat format) noexcept;
  [[nodiscard]] static std::size_t bytesPerSample(
      IqSampleFormat format) noexcept;
  // Exposed so the desktop layer and tests can name the sidecar without
  // duplicating the extension rule.
  [[nodiscard]] static std::string metadataPathFor(std::string_view data_path);

 private:
  void stop(IqCaptureStopReason reason);
  [[nodiscard]] bool writeSidecar();

  static constexpr std::size_t kBlockCapacity =
      sizeof(RealtimeSampleBlock::samples) / sizeof(std::complex<float>);
  // One block, worst case (cf32: two 4-byte components per sample). A fixed
  // member buffer keeps the write path allocation-free at megasample rates.
  static constexpr std::size_t kStagingBytes =
      kBlockCapacity * 2U * sizeof(float);
  // A retune costs one segment. Bounded so a provider that reports a jittering
  // centre frequency cannot grow the sidecar without limit; beyond this the
  // samples keep being recorded under the last recorded segment.
  static constexpr std::size_t kMaximumSegments = 4'096;

  std::ofstream file_;
  std::string data_path_;
  std::string metadata_path_;
  std::string last_error_;
  IqCaptureMetadata metadata_{};
  IqWriterLimits limits_{};
  std::vector<IqCaptureSegment> segments_;
  std::array<char, kStagingBytes> staging_{};
  IqLevelSummary last_block_levels_{};
  IqLevelSummary capture_levels_{};
  double capture_sum_real_{0.0};
  double capture_sum_imaginary_{0.0};
  std::uint64_t samples_written_{0};
  std::uint64_t maximum_samples_{0};
  // Which of the two configured bounds is the one the capture will actually
  // hit, decided once at open() so the reported reason is never a guess.
  IqCaptureStopReason budget_reason_{IqCaptureStopReason::DurationBudget};
  IqCaptureStopReason stop_reason_{IqCaptureStopReason::None};
  bool stopped_{false};
};

}  // namespace cwassistant::core
