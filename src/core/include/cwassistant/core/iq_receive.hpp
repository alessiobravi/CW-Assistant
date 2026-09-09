#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>

#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

enum class IqBlockStatus {
  Accepted,
  AcceptedAfterDiscontinuity,
  RejectedStreamKind,
  RejectedDescriptor,
  RejectedSampleCount,
  RejectedNonFiniteSample,
  RejectedSampleMagnitude,
};

struct IqReceiveLimits {
  double minimum_sample_rate_hz{8'000.0};
  double maximum_sample_rate_hz{64'000'000.0};
  double maximum_center_frequency_hz{99'000'000'000.0};
  float maximum_sample_magnitude{8.0F};
};

struct IqReceiveTelemetry {
  std::uint64_t accepted_blocks{0};
  std::uint64_t accepted_samples{0};
  std::uint64_t rejected_blocks{0};
  std::uint64_t discontinuities{0};
  std::uint64_t missing_block_sequences{0};
  std::uint64_t non_finite_samples{0};
  std::uint64_t excessive_magnitude_samples{0};
};

// Validates provider-produced IQ at the dependency-free boundary. The class
// never allocates, throws, sleeps, or calls hardware and keeps all counters
// saturating so malformed or long-running streams cannot wrap diagnostics.
class IqReceiveValidator {
 public:
  explicit IqReceiveValidator(IqReceiveLimits limits = {});

  [[nodiscard]] bool configure(IqReceiveLimits limits) noexcept;
  void reset() noexcept;
  [[nodiscard]] IqBlockStatus validate(
      const RealtimeSampleBlock& block) noexcept;
  [[nodiscard]] const IqReceiveLimits& limits() const noexcept;
  [[nodiscard]] const IqReceiveTelemetry& telemetry() const noexcept;

 private:
  IqReceiveLimits limits_{};
  IqReceiveTelemetry telemetry_{};
  StreamDescriptor previous_stream_{};
  std::uint64_t expected_sequence_{0};
  std::uint64_t expected_timestamp_ns_{0};
  bool have_previous_block_{false};
};

struct IqChannelizerConfig {
  double target_frequency_hz{0.0};
  double output_sample_rate_hz{48'000.0};
  double output_tone_hz{700.0};
  double channel_bandwidth_hz{500.0};
};

struct IqChannelizerTelemetry {
  IqReceiveTelemetry input{};
  std::uint64_t state_resets{0};
  std::uint64_t output_blocks{0};
  std::uint64_t output_samples{0};
};

// Converts a selected RF channel from complex IQ into a conventional real
// audio CW tone. The unmodified IQ block remains suitable for the shared
// SpectrumAnalyzer, while this bounded bridge feeds the existing audio
// decoder. It is receive-only and exposes no radio-control or TX operation.
class IqToAudioChannelizer {
 public:
  explicit IqToAudioChannelizer(IqChannelizerConfig config = {},
                                IqReceiveLimits limits = {});

  [[nodiscard]] bool configure(IqChannelizerConfig config) noexcept;
  void reset() noexcept;
  [[nodiscard]] IqBlockStatus process(const RealtimeSampleBlock& input,
                                      RealtimeSampleBlock& output) noexcept;
  [[nodiscard]] const IqChannelizerConfig& config() const noexcept;
  [[nodiscard]] IqChannelizerTelemetry telemetry() const noexcept;

 private:
  void resetSignalState() noexcept;

  IqChannelizerConfig config_{};
  IqReceiveValidator validator_{};
  StreamDescriptor input_stream_{};
  std::array<std::complex<double>, 3> channel_filter_{};
  std::complex<double> tuning_oscillator_{1.0, 0.0};
  std::complex<double> tuning_step_{1.0, 0.0};
  std::complex<double> tone_oscillator_{1.0, 0.0};
  std::complex<double> tone_step_{1.0, 0.0};
  std::complex<double> decimation_sum_{};
  double decimation_phase_{0.0};
  double filter_alpha_{0.0};
  std::size_t decimation_count_{0};
  std::uint64_t output_sequence_{0};
  std::uint64_t state_resets_{0};
  std::uint64_t output_blocks_{0};
  std::uint64_t output_samples_{0};
  bool stream_initialized_{false};
};

}  // namespace cwassistant::core
