#include "sdr_receiver.hpp"

#if CWA_HAVE_SOAPY_SDR

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Version.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr std::size_t kRxChannel = 0;

std::string value_or(const SoapySDR::Kwargs& values, const std::string& key,
                     const std::string& fallback = {}) {
  const auto found = values.find(key);
  return found == values.end() ? fallback : found->second;
}

std::string device_id(const SoapySDR::Kwargs& values, const std::size_t index) {
  std::ostringstream output;
  output << value_or(values, "driver", "unknown") << ':'
         << value_or(values, "serial", value_or(values, "label", "device"))
         << ':' << index;
  return output.str();
}

double nearest_supported_rate(SoapySDR::Device& device,
                              const double requested) {
  const auto ranges = device.getSampleRateRange(SOAPY_SDR_RX, kRxChannel);
  if (ranges.empty()) return requested;
  double best = ranges.front().minimum();
  double distance = std::abs(best - requested);
  for (const auto& range : ranges) {
    const double candidate = std::clamp(requested, range.minimum(),
                                        range.maximum());
    const double candidate_distance = std::abs(candidate - requested);
    if (candidate_distance < distance) {
      best = candidate;
      distance = candidate_distance;
    }
  }
  return best;
}

double nearest_supported_bandwidth(SoapySDR::Device& device,
                                   const double requested) {
  const auto ranges = device.getBandwidthRange(SOAPY_SDR_RX, kRxChannel);
  if (ranges.empty()) return requested;
  double best = ranges.front().minimum();
  double distance = std::abs(best - requested);
  for (const auto& range : ranges) {
    const double candidate = std::clamp(requested, range.minimum(),
                                        range.maximum());
    const double candidate_distance = std::abs(candidate - requested);
    if (candidate_distance < distance) {
      best = candidate;
      distance = candidate_distance;
    }
  }
  return best;
}

class SoapySdrReceiveBackend final : public SdrReceiveBackend {
 public:
  ~SoapySdrReceiveBackend() override { close(); }

  SdrDiscoveryReport discover() override {
    SdrDiscoveryReport report;
    report.backend_available = true;
    report.backend_version = SoapySDR::getLibVersion();
    try {
      report.modules = SoapySDR::listModules();
      const auto devices = SoapySDR::Device::enumerate();
      for (const auto& module : report.modules) {
        for (const auto& [driver, load_error] :
             SoapySDR::getLoaderResult(module)) {
          if (!load_error.empty()) {
            report.module_load_errors.push_back(driver + ": " + load_error);
            continue;
          }
          if (load_error.empty() &&
              std::find(report.loaded_drivers.begin(),
                        report.loaded_drivers.end(), driver) ==
                  report.loaded_drivers.end()) {
            report.loaded_drivers.push_back(driver);
          }
        }
      }
      known_devices_.clear();
      for (std::size_t index = 0; index < devices.size(); ++index) {
        const auto& values = devices[index];
        const std::string id = device_id(values, index);
        known_devices_.emplace(id, values);
        report.devices.push_back(
            {.id = id,
             .label = value_or(values, "label", id),
             .driver = value_or(values, "driver", "unknown"),
             .serial = value_or(values, "serial")});
      }
      if (report.modules.empty()) {
        report.diagnostic =
            "SoapySDR is installed, but no device modules were found. Install "
            "the RTL-SDR or SDRplay Soapy module.";
      } else if (report.loaded_drivers.empty()) {
        report.diagnostic =
            "SoapySDR module files were found, but no receiver driver loaded. "
            "Check the module ABI and its runtime dependencies.";
      } else if (report.devices.empty()) {
        report.diagnostic =
            "SoapySDR modules are installed, but no receiver was found. Check "
            "USB access, the vendor runtime, and whether another application "
            "already owns the device.";
      } else {
        report.diagnostic = "SoapySDR receiver discovery completed.";
      }
      if (!report.module_load_errors.empty()) {
        report.diagnostic += " Module load failure: " +
                             report.module_load_errors.front();
      }
    } catch (const std::exception& exception) {
      report.diagnostic = std::string("SoapySDR discovery failed: ") +
                          exception.what();
    }
    return report;
  }

  SdrDeviceCapabilities probe(const std::string& selected_id) override {
    SdrDeviceCapabilities capabilities;
    try {
      if (known_devices_.find(selected_id) == known_devices_.end())
        (void)discover();
      const auto found = known_devices_.find(selected_id);
      if (found == known_devices_.end()) {
        capabilities.diagnostic =
            "The selected SDR is no longer available. Refresh devices.";
        return capabilities;
      }
      SoapySDR::Device* probe_device = SoapySDR::Device::make(found->second);
      if (probe_device == nullptr) {
        capabilities.diagnostic =
            "The selected SDR could not be opened for capability probing.";
        return capabilities;
      }
      const auto release = [&probe_device] {
        if (probe_device != nullptr) SoapySDR::Device::unmake(probe_device);
        probe_device = nullptr;
      };
      try {
        const auto append_ranges = [](const SoapySDR::RangeList& ranges,
                                      std::vector<double>& values) {
          constexpr std::array<double, 13> common{
              62'500.0, 96'000.0, 125'000.0, 192'000.0, 250'000.0,
              384'000.0, 500'000.0, 768'000.0, 1'000'000.0,
              2'000'000.0, 2'400'000.0, 8'000'000.0, 10'000'000.0};
          for (const auto& range : ranges) {
            values.push_back(range.minimum());
            for (const double candidate : common) {
              if (candidate >= range.minimum() &&
                  candidate <= range.maximum())
                values.push_back(candidate);
            }
            values.push_back(range.maximum());
          }
          std::ranges::sort(values);
          values.erase(std::unique(values.begin(), values.end(),
                                   [](const double left, const double right) {
                                     return std::abs(left - right) < 0.5;
                                   }),
                       values.end());
        };
        append_ranges(
            probe_device->getSampleRateRange(SOAPY_SDR_RX, kRxChannel),
            capabilities.sample_rates_hz);
        append_ranges(
            probe_device->getBandwidthRange(SOAPY_SDR_RX, kRxChannel),
            capabilities.bandwidths_hz);
        capabilities.antennas =
            probe_device->listAntennas(SOAPY_SDR_RX, kRxChannel);
        capabilities.automatic_gain_available =
            probe_device->hasGainMode(SOAPY_SDR_RX, kRxChannel);
        const auto gain =
            probe_device->getGainRange(SOAPY_SDR_RX, kRxChannel);
        capabilities.minimum_gain_db = gain.minimum();
        capabilities.maximum_gain_db = gain.maximum();
        capabilities.gain_step_db = gain.step();
        capabilities.available = true;
        capabilities.diagnostic =
            "Device capabilities loaded; reception remains stopped.";
      } catch (...) {
        release();
        throw;
      }
      release();
    } catch (const std::exception& exception) {
      capabilities.diagnostic =
          std::string("SDR capability probe failed: ") + exception.what();
    }
    return capabilities;
  }

  bool open(const SdrReceiveConfiguration& configuration,
            SdrActualConfiguration& actual, std::string& error) override {
    close();
    try {
      if (known_devices_.find(configuration.device_id) == known_devices_.end()) {
        (void)discover();
      }
      const auto found = known_devices_.find(configuration.device_id);
      if (found == known_devices_.end()) {
        error = "The selected SDR is no longer available. Refresh the device list.";
        return false;
      }

      device_ = SoapySDR::Device::make(found->second);
      if (device_ == nullptr) {
        error = "SoapySDR could not create the selected receiver.";
        return false;
      }

      const double rate = nearest_supported_rate(*device_,
                                                  configuration.sample_rate_hz);
      device_->setSampleRate(SOAPY_SDR_RX, kRxChannel, rate);
      if (configuration.bandwidth_hz > 0.0) {
        device_->setBandwidth(
            SOAPY_SDR_RX, kRxChannel,
            nearest_supported_bandwidth(*device_,
                                        configuration.bandwidth_hz));
      }
      device_->setFrequency(SOAPY_SDR_RX, kRxChannel,
                            configuration.center_frequency_hz);
      if (!configuration.antenna.empty()) {
        const auto antennas =
            device_->listAntennas(SOAPY_SDR_RX, kRxChannel);
        if (std::ranges::find(antennas, configuration.antenna) ==
            antennas.end()) {
          error = "The selected SDR antenna is no longer available.";
          close();
          return false;
        }
        device_->setAntenna(SOAPY_SDR_RX, kRxChannel,
                            configuration.antenna);
      }
      if (device_->hasGainMode(SOAPY_SDR_RX, kRxChannel)) {
        device_->setGainMode(SOAPY_SDR_RX, kRxChannel,
                             configuration.automatic_gain);
      } else if (configuration.automatic_gain) {
        error = "This SDR does not provide automatic gain. Select manual gain.";
        close();
        return false;
      }
      if (!configuration.automatic_gain) {
        device_->setGain(SOAPY_SDR_RX, kRxChannel, configuration.gain_db);
      }

      actual = {.center_frequency_hz =
                    device_->getFrequency(SOAPY_SDR_RX, kRxChannel),
                .sample_rate_hz =
                    device_->getSampleRate(SOAPY_SDR_RX, kRxChannel),
                .bandwidth_hz =
                    device_->getBandwidth(SOAPY_SDR_RX, kRxChannel),
                .automatic_gain =
                    device_->hasGainMode(SOAPY_SDR_RX, kRxChannel) &&
                    device_->getGainMode(SOAPY_SDR_RX, kRxChannel),
                .gain_db = device_->getGain(SOAPY_SDR_RX, kRxChannel)};

      constexpr double kRateTolerance = 1.0;
      constexpr double kFrequencyTolerance = 1.0;
      if (std::abs(actual.sample_rate_hz - rate) > kRateTolerance ||
          std::abs(actual.center_frequency_hz -
                   configuration.center_frequency_hz) > kFrequencyTolerance) {
        error = "The SDR did not accept the requested frequency or sample rate.";
        close();
        return false;
      }

      stream_ = device_->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {kRxChannel});
      if (stream_ == nullptr) {
        error = "SoapySDR could not create a complex-float RX stream.";
        close();
        return false;
      }
      const int activation = device_->activateStream(stream_);
      if (activation != 0) {
        error = std::string("SoapySDR could not activate RX: ") +
                SoapySDR::errToStr(activation);
        close();
        return false;
      }
      stream_active_ = true;
      return true;
    } catch (const std::exception& exception) {
      error = std::string("SoapySDR could not start reception: ") +
              exception.what();
      close();
      return false;
    }
  }

  SdrReadResult read(std::span<std::complex<float>> samples,
                     const long timeout_microseconds) override {
    if (device_ == nullptr || stream_ == nullptr || !stream_active_) {
      return {.error = "The SoapySDR RX stream is not active."};
    }
    void* buffers[] = {samples.data()};
    int flags = 0;
    long long time_ns = 0;
    try {
      const int count = device_->readStream(stream_, buffers, samples.size(),
                                            flags, time_ns,
                                            timeout_microseconds);
      if (count == SOAPY_SDR_TIMEOUT) return {.timeout = true};
      if (count == SOAPY_SDR_OVERFLOW) return {.overflow = true};
      if (count < 0) {
        return {.error = std::string("SoapySDR RX read failed: ") +
                         SoapySDR::errToStr(count)};
      }
      return {.sample_count = static_cast<std::size_t>(count),
              .timestamp_ns = time_ns < 0 ? 0U
                                          : static_cast<std::uint64_t>(time_ns),
              .timestamp_valid = (flags & SOAPY_SDR_HAS_TIME) != 0};
    } catch (const std::exception& exception) {
      return {.error = std::string("SoapySDR RX read failed: ") +
                       exception.what()};
    }
  }

  void close() noexcept override {
    if (device_ == nullptr) return;
    try {
      if (stream_ != nullptr) {
        if (stream_active_) (void)device_->deactivateStream(stream_);
        device_->closeStream(stream_);
      }
    } catch (...) {
      // Destruction and emergency shutdown must never cross the RX boundary.
    }
    try {
      SoapySDR::Device::unmake(device_);
    } catch (...) {
    }
    device_ = nullptr;
    stream_ = nullptr;
    stream_active_ = false;
  }

 private:
  std::map<std::string, SoapySDR::Kwargs> known_devices_;
  SoapySDR::Device* device_{nullptr};
  SoapySDR::Stream* stream_{nullptr};
  bool stream_active_{false};
};

}  // namespace

std::unique_ptr<SdrReceiveBackend> makeSoapySdrReceiveBackend() {
  return std::make_unique<SoapySdrReceiveBackend>();
}

}  // namespace cwassistant::desktop

#endif
