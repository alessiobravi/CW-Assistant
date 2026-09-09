#pragma once

#include "cwassistant/core/iq_receive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cwassistant::desktop {

struct SdrDeviceDescriptor {
  std::string id;
  std::string label;
  std::string driver;
  std::string serial;
};

struct SdrDiscoveryReport {
  bool backend_available{false};
  std::string backend_version;
  std::vector<std::string> modules;
  // Factory names whose modules loaded and registered without an error.
  // This distinguishes a module file being present from a usable driver.
  std::vector<std::string> loaded_drivers;
  std::vector<std::string> module_load_errors;
  std::vector<SdrDeviceDescriptor> devices;
  std::string diagnostic;
};

struct SdrReceiveConfiguration {
  std::string device_id;
  double center_frequency_hz{0.0};
  double sample_rate_hz{250'000.0};
  bool automatic_gain{true};
  double gain_db{0.0};
};

struct SdrActualConfiguration {
  double center_frequency_hz{0.0};
  double sample_rate_hz{0.0};
  bool automatic_gain{false};
  double gain_db{0.0};
};

struct SdrReadResult {
  std::size_t sample_count{0};
  std::uint64_t timestamp_ns{0};
  bool timestamp_valid{false};
  bool overflow{false};
  bool timeout{false};
  std::string error;
};

// Device APIs stay behind this RX-only contract. There is deliberately no TX,
// PTT, or KEY operation at this boundary.
class SdrReceiveBackend {
 public:
  virtual ~SdrReceiveBackend() = default;

  [[nodiscard]] virtual SdrDiscoveryReport discover() = 0;
  [[nodiscard]] virtual bool open(const SdrReceiveConfiguration& configuration,
                                  SdrActualConfiguration& actual,
                                  std::string& error) = 0;
  [[nodiscard]] virtual SdrReadResult read(
      std::span<std::complex<float>> samples, long timeout_microseconds) = 0;
  virtual void close() noexcept = 0;
};

struct SdrReceiverDiagnostics {
  bool running{false};
  std::uint64_t blocks{0};
  std::uint64_t samples{0};
  std::uint64_t overflows{0};
  std::uint64_t timeouts{0};
  std::uint64_t read_errors{0};
  std::uint64_t invalid_blocks{0};
  std::uint64_t discontinuities{0};
  std::string last_error;
};

// Converts backend reads into the same fixed, timestamped IQ block contract
// consumed by the receiver DSP. The owner decides which thread calls pump().
class SdrReceiver final {
 public:
  explicit SdrReceiver(std::unique_ptr<SdrReceiveBackend> backend);
  ~SdrReceiver();

  SdrReceiver(const SdrReceiver&) = delete;
  SdrReceiver& operator=(const SdrReceiver&) = delete;

  [[nodiscard]] SdrDiscoveryReport discover();
  [[nodiscard]] bool start(const SdrReceiveConfiguration& configuration,
                           std::string& error);
  void stop() noexcept;
  [[nodiscard]] bool pump(core::RealtimeSampleBlock& block,
                          long timeout_microseconds = 100'000);
  [[nodiscard]] const SdrReceiverDiagnostics& diagnostics() const noexcept;
  [[nodiscard]] const SdrActualConfiguration& actualConfiguration() const noexcept;

 private:
  std::unique_ptr<SdrReceiveBackend> backend_;
  SdrReceiveConfiguration configuration_{};
  SdrActualConfiguration actual_{};
  SdrReceiverDiagnostics diagnostics_{};
  core::IqReceiveValidator validator_{};
  std::uint64_t sequence_{0};
  std::uint64_t synthesized_timestamp_ns_{0};
};

// Always available. Without SoapySDR it returns a diagnostic-only backend so
// packaged default builds remain usable and can explain how to enable SDR.
[[nodiscard]] std::unique_ptr<SdrReceiveBackend> makeSoapySdrReceiveBackend();

}  // namespace cwassistant::desktop
