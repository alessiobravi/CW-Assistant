#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

// Dependency-free streaming writer for little-endian RIFF/WAVE PCM16 mono
// audio. Used for deterministic replay recording and operator-consented
// diagnostic capture; never invoked implicitly or silently by the core.
class WavWriter final {
 public:
  ~WavWriter();

  [[nodiscard]] bool open(std::string_view path, double sample_rate_hz);
  // Writes the real component of every sample in the block, clamped to the
  // PCM16 range. Returns false (and stops writing further blocks) once
  // max_frames set by open() would be exceeded, so a bounded capture can
  // never grow without limit.
  [[nodiscard]] bool writeBlock(const RealtimeSampleBlock& block);
  // Finalizes the RIFF/data chunk sizes. Safe to call multiple times; the
  // destructor calls this automatically if still open.
  void close() noexcept;

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] std::uint64_t framesWritten() const noexcept;
  [[nodiscard]] const std::string& lastError() const noexcept;

 private:
  std::ofstream file_;
  std::string last_error_;
  std::uint64_t frames_written_{0};
  double sample_rate_hz_{0.0};
};

}  // namespace cwassistant::core
