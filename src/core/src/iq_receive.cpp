#include "cwassistant/core/iq_receive.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace cwassistant::core {
namespace {

template <typename Value>
void saturatingAdd(Value& destination, const Value increment = 1) noexcept {
  const auto maximum = std::numeric_limits<Value>::max();
  destination = increment > maximum - destination ? maximum
                                                   : destination + increment;
}

bool finite(const double value) noexcept { return std::isfinite(value); }

bool validLimits(const IqReceiveLimits& limits) noexcept {
  return finite(limits.minimum_sample_rate_hz) &&
         finite(limits.maximum_sample_rate_hz) &&
         finite(limits.maximum_center_frequency_hz) &&
         std::isfinite(limits.maximum_sample_magnitude) &&
         limits.minimum_sample_rate_hz > 0.0 &&
         limits.maximum_sample_rate_hz >= limits.minimum_sample_rate_hz &&
         limits.maximum_center_frequency_hz > 0.0 &&
         limits.maximum_sample_magnitude > 0.0F;
}

bool sameStream(const StreamDescriptor& left,
                const StreamDescriptor& right) noexcept {
  return left.kind == right.kind &&
         left.sample_rate_hz == right.sample_rate_hz &&
         left.center_frequency_hz == right.center_frequency_hz &&
         left.channel_count == right.channel_count;
}

void normalize(std::complex<double>& oscillator) noexcept {
  const double magnitude = std::abs(oscillator);
  if (magnitude > 0.0 && finite(magnitude)) {
    oscillator /= magnitude;
  } else {
    oscillator = {1.0, 0.0};
  }
}

}  // namespace

IqReceiveValidator::IqReceiveValidator(const IqReceiveLimits limits) {
  if (validLimits(limits)) {
    limits_ = limits;
  }
}

bool IqReceiveValidator::configure(const IqReceiveLimits limits) noexcept {
  if (!validLimits(limits)) {
    return false;
  }
  limits_ = limits;
  reset();
  return true;
}

void IqReceiveValidator::reset() noexcept {
  telemetry_ = {};
  previous_stream_ = {};
  expected_sequence_ = 0;
  expected_timestamp_ns_ = 0;
  have_previous_block_ = false;
}

IqBlockStatus IqReceiveValidator::validate(
    const RealtimeSampleBlock& block) noexcept {
  const auto reject = [this](const IqBlockStatus status) {
    saturatingAdd(telemetry_.rejected_blocks);
    return status;
  };
  if (block.stream.kind != StreamKind::ComplexIq) {
    return reject(IqBlockStatus::RejectedStreamKind);
  }
  if (!finite(block.stream.sample_rate_hz) ||
      !finite(block.stream.center_frequency_hz) ||
      block.stream.sample_rate_hz < limits_.minimum_sample_rate_hz ||
      block.stream.sample_rate_hz > limits_.maximum_sample_rate_hz ||
      block.stream.center_frequency_hz < 0.0 ||
      block.stream.center_frequency_hz > limits_.maximum_center_frequency_hz ||
      block.stream.channel_count != 1) {
    return reject(IqBlockStatus::RejectedDescriptor);
  }
  if (block.sample_count == 0 || block.sample_count > block.samples.size()) {
    return reject(IqBlockStatus::RejectedSampleCount);
  }
  for (std::size_t index = 0; index < block.sample_count; ++index) {
    const auto& sample = block.samples[index];
    if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
      saturatingAdd(telemetry_.non_finite_samples);
      return reject(IqBlockStatus::RejectedNonFiniteSample);
    }
    if (std::abs(sample) > limits_.maximum_sample_magnitude) {
      saturatingAdd(telemetry_.excessive_magnitude_samples);
      return reject(IqBlockStatus::RejectedSampleMagnitude);
    }
  }

  bool discontinuity = false;
  if (have_previous_block_) {
    if (!sameStream(previous_stream_, block.stream) ||
        block.sequence != expected_sequence_) {
      discontinuity = true;
    }
    if (block.sequence > expected_sequence_) {
      saturatingAdd(telemetry_.missing_block_sequences,
                    block.sequence - expected_sequence_);
    }
    const auto timing_tolerance_ns = static_cast<std::uint64_t>(std::ceil(
        2.0 * 1'000'000'000.0 / block.stream.sample_rate_hz));
    const auto timing_difference_ns =
        block.timestamp_ns > expected_timestamp_ns_
            ? block.timestamp_ns - expected_timestamp_ns_
            : expected_timestamp_ns_ - block.timestamp_ns;
    discontinuity = discontinuity || timing_difference_ns > timing_tolerance_ns;
  }

  previous_stream_ = block.stream;
  expected_sequence_ = block.sequence + 1;
  expected_timestamp_ns_ =
      block.timestamp_ns + static_cast<std::uint64_t>(
                               static_cast<long double>(block.sample_count) *
                               1'000'000'000.0L /
                               block.stream.sample_rate_hz);
  have_previous_block_ = true;
  saturatingAdd(telemetry_.accepted_blocks);
  saturatingAdd(telemetry_.accepted_samples,
                static_cast<std::uint64_t>(block.sample_count));
  if (discontinuity) {
    saturatingAdd(telemetry_.discontinuities);
    return IqBlockStatus::AcceptedAfterDiscontinuity;
  }
  return IqBlockStatus::Accepted;
}

const IqReceiveLimits& IqReceiveValidator::limits() const noexcept {
  return limits_;
}

const IqReceiveTelemetry& IqReceiveValidator::telemetry() const noexcept {
  return telemetry_;
}

IqToAudioChannelizer::IqToAudioChannelizer(
    const IqChannelizerConfig config, const IqReceiveLimits limits)
    : validator_(limits) {
  static_cast<void>(configure(config));
}

bool IqToAudioChannelizer::configure(
    const IqChannelizerConfig config) noexcept {
  if (!finite(config.target_frequency_hz) ||
      !finite(config.output_sample_rate_hz) ||
      !finite(config.output_tone_hz) ||
      !finite(config.channel_bandwidth_hz) ||
      config.target_frequency_hz < 0.0 ||
      config.target_frequency_hz > 99'000'000'000.0 ||
      config.output_sample_rate_hz < 8'000.0 ||
      config.output_sample_rate_hz > 192'000.0 ||
      config.output_tone_hz < 100.0 ||
      config.channel_bandwidth_hz < 40.0 ||
      config.channel_bandwidth_hz > 2'000.0 ||
      config.output_tone_hz + config.channel_bandwidth_hz / 2.0 >=
          config.output_sample_rate_hz / 2.0) {
    return false;
  }
  config_ = config;
  resetSignalState();
  stream_initialized_ = false;
  return true;
}

void IqToAudioChannelizer::resetSignalState() noexcept {
  channel_filter_ = {};
  tuning_oscillator_ = {1.0, 0.0};
  tone_oscillator_ = {1.0, 0.0};
  decimation_sum_ = {};
  decimation_phase_ = 0.0;
  decimation_count_ = 0;
  saturatingAdd(state_resets_);
}

void IqToAudioChannelizer::reset() noexcept {
  validator_.reset();
  input_stream_ = {};
  output_sequence_ = 0;
  state_resets_ = 0;
  output_blocks_ = 0;
  output_samples_ = 0;
  stream_initialized_ = false;
  resetSignalState();
  state_resets_ = 0;
}

IqBlockStatus IqToAudioChannelizer::process(
    const RealtimeSampleBlock& input,
    RealtimeSampleBlock& output) noexcept {
  output = {};
  const IqBlockStatus status = validator_.validate(input);
  if (status != IqBlockStatus::Accepted &&
      status != IqBlockStatus::AcceptedAfterDiscontinuity) {
    return status;
  }

  const double offset_hz =
      config_.target_frequency_hz - input.stream.center_frequency_hz;
  if (config_.output_sample_rate_hz > input.stream.sample_rate_hz ||
      std::abs(offset_hz) + config_.channel_bandwidth_hz / 2.0 >=
          input.stream.sample_rate_hz / 2.0) {
    return IqBlockStatus::RejectedDescriptor;
  }

  const bool changed_stream =
      !stream_initialized_ || !sameStream(input_stream_, input.stream);
  if (changed_stream || status == IqBlockStatus::AcceptedAfterDiscontinuity) {
    resetSignalState();
    input_stream_ = input.stream;
    stream_initialized_ = true;
    const double tuning_angle =
        -2.0 * std::numbers::pi * offset_hz / input.stream.sample_rate_hz;
    tuning_step_ = {std::cos(tuning_angle), std::sin(tuning_angle)};
    const double tone_angle = 2.0 * std::numbers::pi * config_.output_tone_hz /
                              config_.output_sample_rate_hz;
    tone_step_ = {std::cos(tone_angle), std::sin(tone_angle)};
    filter_alpha_ = 1.0 - std::exp(-2.0 * std::numbers::pi *
                                   (config_.channel_bandwidth_hz / 2.0) /
                                   input.stream.sample_rate_hz);
  }

  output.stream = {.kind = StreamKind::Audio,
                   .sample_rate_hz = config_.output_sample_rate_hz,
                   .center_frequency_hz = 0.0,
                   .channel_count = 1};
  output.sequence = output_sequence_;
  output.timestamp_ns = input.timestamp_ns;

  for (std::size_t index = 0; index < input.sample_count; ++index) {
    const std::complex<double> mixed =
        static_cast<std::complex<double>>(input.samples[index]) *
        tuning_oscillator_;
    tuning_oscillator_ *= tuning_step_;
    std::complex<double> stage_input = mixed;
    for (auto& stage : channel_filter_) {
      stage += filter_alpha_ * (stage_input - stage);
      stage_input = stage;
    }
    decimation_sum_ += channel_filter_.back();
    ++decimation_count_;
    decimation_phase_ += config_.output_sample_rate_hz;
    if (decimation_phase_ + 1.0e-9 >= input.stream.sample_rate_hz) {
      decimation_phase_ -= input.stream.sample_rate_hz;
      const auto baseband =
          decimation_sum_ / static_cast<double>(decimation_count_);
      const double audio = (baseband * tone_oscillator_).real();
      output.samples[output.sample_count++] = {
          static_cast<float>(std::clamp(audio, -1.0, 1.0)), 0.0F};
      tone_oscillator_ *= tone_step_;
      decimation_sum_ = {};
      decimation_count_ = 0;
    }
  }
  normalize(tuning_oscillator_);
  normalize(tone_oscillator_);

  if (output.sample_count > 0) {
    output.sequence = output_sequence_++;
    saturatingAdd(output_blocks_);
    saturatingAdd(output_samples_,
                  static_cast<std::uint64_t>(output.sample_count));
  }
  return status;
}

const IqChannelizerConfig& IqToAudioChannelizer::config() const noexcept {
  return config_;
}

IqChannelizerTelemetry IqToAudioChannelizer::telemetry() const noexcept {
  return {.input = validator_.telemetry(),
          .state_resets = state_resets_,
          .output_blocks = output_blocks_,
          .output_samples = output_samples_};
}

}  // namespace cwassistant::core
