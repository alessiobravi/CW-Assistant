#include "cwassistant/core/wav_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cwassistant::core {
namespace {

void write_u16(std::ostream& stream, const std::uint16_t value) {
  const char bytes[2]{static_cast<char>(value & 0xFFU),
                      static_cast<char>((value >> 8U) & 0xFFU)};
  stream.write(bytes, sizeof(bytes));
}

void write_u32(std::ostream& stream, const std::uint32_t value) {
  const char bytes[4]{
      static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
      static_cast<char>((value >> 16U) & 0xFFU),
      static_cast<char>((value >> 24U) & 0xFFU)};
  stream.write(bytes, sizeof(bytes));
}

constexpr std::uint64_t kMaximumFrames = 30ULL * 60ULL * 96'000ULL;  // ~30 min at 96 kHz

}  // namespace

WavWriter::~WavWriter() { close(); }

bool WavWriter::open(const std::string_view path, const double sample_rate_hz) {
  close();
  last_error_.clear();
  if (!(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
    last_error_ = "Invalid sample rate";
    return false;
  }
  file_.open(std::string(path), std::ios::binary | std::ios::trunc);
  if (!file_) {
    last_error_ = "Could not create capture file";
    return false;
  }
  sample_rate_hz_ = sample_rate_hz;
  frames_written_ = 0;

  const auto rounded_rate = static_cast<std::uint32_t>(std::llround(sample_rate_hz));
  constexpr std::uint16_t kBitsPerSample = 16;
  constexpr std::uint16_t kChannels = 1;
  const std::uint32_t byte_rate = rounded_rate * kChannels * (kBitsPerSample / 8U);
  const std::uint16_t block_align = kChannels * (kBitsPerSample / 8U);

  file_.write("RIFF", 4);
  write_u32(file_, 0);  // placeholder, patched on close()
  file_.write("WAVE", 4);
  file_.write("fmt ", 4);
  write_u32(file_, 16);
  write_u16(file_, 1);  // PCM
  write_u16(file_, kChannels);
  write_u32(file_, rounded_rate);
  write_u32(file_, byte_rate);
  write_u16(file_, block_align);
  write_u16(file_, kBitsPerSample);
  file_.write("data", 4);
  write_u32(file_, 0);  // placeholder, patched on close()

  if (!file_) {
    last_error_ = "Could not write capture header";
    close();
    return false;
  }
  return true;
}

bool WavWriter::writeBlock(const RealtimeSampleBlock& block) {
  if (!file_.is_open()) {
    last_error_ = "Capture file is not open";
    return false;
  }
  if (block.sample_count == 0 || block.sample_count > block.samples.size()) {
    return true;
  }
  if (frames_written_ >= kMaximumFrames) {
    last_error_ = "Capture reached its maximum bounded duration";
    return false;
  }
  const auto remaining = kMaximumFrames - frames_written_;
  const auto to_write =
      std::min<std::uint64_t>(remaining, block.sample_count);
  for (std::uint64_t index = 0; index < to_write; ++index) {
    const float sample =
        std::clamp(block.samples[static_cast<std::size_t>(index)].real(),
                   -1.0F, 1.0F);
    const auto pcm = static_cast<std::int16_t>(std::lround(sample * 32'767.0F));
    write_u16(file_, static_cast<std::uint16_t>(pcm));
  }
  frames_written_ += to_write;
  return to_write == block.sample_count;
}

void WavWriter::close() noexcept {
  if (!file_.is_open()) {
    return;
  }
  const std::uint64_t data_bytes = frames_written_ * 2ULL;
  file_.seekp(4, std::ios::beg);
  write_u32(file_, static_cast<std::uint32_t>(std::min<std::uint64_t>(
                        data_bytes + 36ULL, 0xFFFF'FFFFULL)));
  file_.seekp(40, std::ios::beg);
  write_u32(file_, static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(data_bytes, 0xFFFF'FFFFULL)));
  file_.flush();
  file_.close();
}

bool WavWriter::isOpen() const noexcept { return file_.is_open(); }

std::uint64_t WavWriter::framesWritten() const noexcept {
  return frames_written_;
}

const std::string& WavWriter::lastError() const noexcept {
  return last_error_;
}

}  // namespace cwassistant::core
