#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/adif.hpp"
#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

struct DeviceInfo {
  std::string id;
  std::string display_name;
};

struct SerialSettings {
  std::string port;
  std::uint32_t baud_rate{9'600};
  std::uint8_t data_bits{8};
  std::uint8_t stop_bits{1};
  bool rts_active_high{true};
  bool dtr_active_high{true};
};

enum class SerialKeyLine { Rts, Dtr };

struct RigProfile {
  std::string id;
  std::string display_name;
  std::uint32_t hamlib_model_id{0};
  SerialSettings cat;
  SerialSettings keying;
  SerialKeyLine ptt_line{SerialKeyLine::Rts};
  SerialKeyLine key_line{SerialKeyLine::Dtr};
};

class ISampleSource {
 public:
  virtual ~ISampleSource() = default;
  [[nodiscard]] virtual std::vector<DeviceInfo> enumerate() const = 0;
  virtual bool open(std::string_view device_id,
                    const StreamDescriptor& requested) = 0;
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool read(RealtimeSampleBlock& destination,
                                  std::chrono::milliseconds timeout) = 0;
};

class IRigControl {
 public:
  virtual ~IRigControl() = default;
  virtual bool connect(const RigProfile& profile) = 0;
  virtual void disconnect() noexcept = 0;
  [[nodiscard]] virtual bool connected() const noexcept = 0;
  [[nodiscard]] virtual double frequency_hz() = 0;
  virtual bool set_frequency_hz(double frequency) = 0;
};

class IKeyingOutput {
 public:
  virtual ~IKeyingOutput() = default;
  virtual bool open(const RigProfile& profile) = 0;
  virtual void close() noexcept = 0;
  virtual bool set_ptt(bool active) noexcept = 0;
  virtual bool set_key(bool active) noexcept = 0;
};

class ILoggerSink {
 public:
  virtual ~ILoggerSink() = default;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  virtual bool submit(const QsoRecord& record) = 0;
};

}  // namespace cwassistant::core
