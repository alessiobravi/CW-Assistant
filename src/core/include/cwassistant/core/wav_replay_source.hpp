#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/interfaces.hpp"

namespace cwassistant::core {

// Deterministic, non-realtime WAV source for UI development and regression
// replay. Multichannel PCM is downmixed to one normalized floating-point audio
// channel; timestamps derive from the frame index, never wall-clock time.
class WavReplaySource final : public ISampleSource {
 public:
  [[nodiscard]] std::vector<DeviceInfo> enumerate() const override;
  bool open(std::string_view device_id,
            const StreamDescriptor& requested) override;
  bool start() override;
  void stop() noexcept override;
  [[nodiscard]] bool read(RealtimeSampleBlock& destination,
                          std::chrono::milliseconds timeout) override;

  [[nodiscard]] const StreamDescriptor& stream_descriptor() const noexcept;
  [[nodiscard]] std::uint64_t total_frames() const noexcept;
  [[nodiscard]] std::uint64_t position_frames() const noexcept;
  [[nodiscard]] double duration_seconds() const noexcept;
  [[nodiscard]] const std::string& last_error() const noexcept;

 private:
  enum class Encoding { PcmUnsigned8, PcmSigned, Float32 };

  void close() noexcept;
  [[nodiscard]] float decode_sample(const std::byte* bytes) const noexcept;

  std::ifstream file_;
  StreamDescriptor stream_{};
  std::string last_error_;
  std::uint64_t data_offset_{0};
  std::uint64_t data_bytes_{0};
  std::uint64_t total_frames_{0};
  std::uint64_t position_frames_{0};
  std::uint64_t sequence_{0};
  std::uint16_t input_channels_{0};
  std::uint16_t bits_per_sample_{0};
  std::uint16_t block_align_{0};
  Encoding encoding_{Encoding::PcmSigned};
  bool running_{false};
  std::vector<std::byte> byte_buffer_;
};

}  // namespace cwassistant::core
