#include "sdr_receiver.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

bool valid_configuration(const SdrReceiveConfiguration& configuration,
                         std::string& error) {
  if (configuration.device_id.empty()) {
    error = "Select an SDR device before starting reception.";
    return false;
  }
  if (!std::isfinite(configuration.center_frequency_hz) ||
      configuration.center_frequency_hz <= 0.0 ||
      configuration.center_frequency_hz > 99'000'000'000.0) {
    error = "The SDR center frequency must be between 1 Hz and 99 GHz.";
    return false;
  }
  if (!std::isfinite(configuration.sample_rate_hz) ||
      configuration.sample_rate_hz < 8'000.0 ||
      configuration.sample_rate_hz > 64'000'000.0) {
    error = "The SDR sample rate must be between 8 kS/s and 64 MS/s.";
    return false;
  }
  if (!configuration.automatic_gain &&
      !std::isfinite(configuration.gain_db)) {
    error = "The manual SDR gain must be a finite value.";
    return false;
  }
  return true;
}

class UnavailableSdrBackend final : public SdrReceiveBackend {
 public:
  SdrDiscoveryReport discover() override {
    return {.backend_available = false,
            .diagnostic =
                "SoapySDR support is not present in this CW Buddy build. "
                "Install SoapySDR and the module for the receiver, then use a "
                "CW Buddy build configured with CWA_ENABLE_SOAPY_SDR=ON."};
  }

  bool open(const SdrReceiveConfiguration&, SdrActualConfiguration&,
            std::string& error) override {
    error = discover().diagnostic;
    return false;
  }

  SdrReadResult read(std::span<std::complex<float>>, long) override {
    return {.error = "SoapySDR support is unavailable."};
  }

  void close() noexcept override {}
};

}  // namespace

SdrReceiver::SdrReceiver(std::unique_ptr<SdrReceiveBackend> backend)
    : backend_(std::move(backend)) {}

SdrReceiver::~SdrReceiver() { stop(); }

SdrDiscoveryReport SdrReceiver::discover() {
  if (!backend_) {
    return {.diagnostic = "No SDR receive backend is configured."};
  }
  return backend_->discover();
}

bool SdrReceiver::start(const SdrReceiveConfiguration& configuration,
                        std::string& error) {
  stop();
  diagnostics_ = {};
  if (!backend_) {
    error = "No SDR receive backend is configured.";
    diagnostics_.last_error = error;
    return false;
  }
  if (!valid_configuration(configuration, error)) {
    diagnostics_.last_error = error;
    return false;
  }
  if (!backend_->open(configuration, actual_, error)) {
    diagnostics_.last_error = error.empty()
                                  ? "The SDR device could not be opened."
                                  : error;
    error = diagnostics_.last_error;
    return false;
  }
  configuration_ = configuration;
  if (!std::isfinite(actual_.center_frequency_hz) ||
      actual_.center_frequency_hz <= 0.0 ||
      actual_.center_frequency_hz > 99'000'000'000.0 ||
      !std::isfinite(actual_.sample_rate_hz) ||
      actual_.sample_rate_hz < 8'000.0 ||
      actual_.sample_rate_hz > 64'000'000.0 ||
      !std::isfinite(actual_.gain_db)) {
    backend_->close();
    error = "The SDR returned frequency, sample-rate, or gain settings outside the supported receive bounds.";
    diagnostics_.last_error = error;
    return false;
  }
  sequence_ = 0;
  synthesized_timestamp_ns_ = 0;
  validator_.reset();
  diagnostics_.running = true;
  return true;
}

void SdrReceiver::stop() noexcept {
  if (backend_) backend_->close();
  diagnostics_.running = false;
}

bool SdrReceiver::pump(core::RealtimeSampleBlock& block,
                       const long timeout_microseconds) {
  block = {};
  if (!diagnostics_.running || !backend_) {
    diagnostics_.last_error = "SDR reception is not running.";
    return false;
  }
  if (timeout_microseconds < 0) {
    diagnostics_.last_error = "The SDR read timeout cannot be negative.";
    ++diagnostics_.read_errors;
    return false;
  }

  const SdrReadResult result = backend_->read(block.samples,
                                               timeout_microseconds);
  if (result.overflow) {
    ++diagnostics_.overflows;
    // Preserve an explicit hole even when the backend cannot report how many
    // samples were lost. The next valid block then resets overlap/filter state
    // instead of silently joining samples across a device overflow.
    if (sequence_ < std::numeric_limits<std::uint64_t>::max()) ++sequence_;
  }
  if (result.timeout) {
    ++diagnostics_.timeouts;
    return false;
  }
  if (!result.error.empty()) {
    diagnostics_.last_error = result.error;
    ++diagnostics_.read_errors;
    return false;
  }
  if (result.sample_count == 0) return false;
  if (result.sample_count > block.samples.size()) {
    diagnostics_.last_error = "The SDR backend returned an oversized sample block.";
    ++diagnostics_.read_errors;
    return false;
  }

  block.stream = {.kind = core::StreamKind::ComplexIq,
                  .sample_rate_hz = actual_.sample_rate_hz,
                  .center_frequency_hz = actual_.center_frequency_hz,
                  .channel_count = 1};
  block.sequence = sequence_++;
  block.sample_count = result.sample_count;
  block.timestamp_ns = result.timestamp_valid ? result.timestamp_ns
                                               : synthesized_timestamp_ns_;

  const core::IqBlockStatus validation = validator_.validate(block);
  if (validation != core::IqBlockStatus::Accepted &&
      validation != core::IqBlockStatus::AcceptedAfterDiscontinuity) {
    ++diagnostics_.invalid_blocks;
    ++diagnostics_.read_errors;
    diagnostics_.last_error =
        "The SDR returned an invalid IQ block; reception was stopped safely.";
    return false;
  }
  if (validation == core::IqBlockStatus::AcceptedAfterDiscontinuity)
    ++diagnostics_.discontinuities;

  const long double elapsed =
      static_cast<long double>(result.sample_count) *
      static_cast<long double>(kNanosecondsPerSecond) /
      static_cast<long double>(actual_.sample_rate_hz);
  const auto elapsed_ns = static_cast<std::uint64_t>(elapsed);
  if (result.timestamp_valid) {
    synthesized_timestamp_ns_ =
        result.timestamp_ns <=
                std::numeric_limits<std::uint64_t>::max() - elapsed_ns
            ? result.timestamp_ns + elapsed_ns
            : std::numeric_limits<std::uint64_t>::max();
  } else if (synthesized_timestamp_ns_ <=
             std::numeric_limits<std::uint64_t>::max() - elapsed_ns) {
    synthesized_timestamp_ns_ += elapsed_ns;
  } else {
    synthesized_timestamp_ns_ = std::numeric_limits<std::uint64_t>::max();
  }

  ++diagnostics_.blocks;
  diagnostics_.samples += result.sample_count;
  diagnostics_.last_error.clear();
  return true;
}

const SdrReceiverDiagnostics& SdrReceiver::diagnostics() const noexcept {
  return diagnostics_;
}

const SdrActualConfiguration& SdrReceiver::actualConfiguration() const noexcept {
  return actual_;
}

#if !CWA_HAVE_SOAPY_SDR
std::unique_ptr<SdrReceiveBackend> makeSoapySdrReceiveBackend() {
  return std::make_unique<UnavailableSdrBackend>();
}
#endif

}  // namespace cwassistant::desktop
