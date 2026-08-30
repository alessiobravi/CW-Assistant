#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace cwassistant::core {

enum class StreamKind { Audio, ComplexIq };

struct StreamDescriptor {
  StreamKind kind{StreamKind::Audio};
  double sample_rate_hz{48'000.0};
  double center_frequency_hz{0.0};
  std::uint32_t channel_count{1};
};

// Fixed storage makes blocks safe to copy through the real-time ring without
// allocation. Audio samples use the real component; IQ uses both components.
template <std::size_t Capacity>
struct SampleBlock {
  static_assert(Capacity > 0);

  StreamDescriptor stream{};
  std::uint64_t sequence{0};
  std::uint64_t timestamp_ns{0};
  std::size_t sample_count{0};
  std::array<std::complex<float>, Capacity> samples{};
};

using RealtimeSampleBlock = SampleBlock<4096>;

}  // namespace cwassistant::core
