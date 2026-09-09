#pragma once

#include "cwassistant/core/sample_block.hpp"
#include "cwassistant/core/spsc_ring_buffer.hpp"

#include <QtGlobal>

#include <atomic>

namespace cwassistant::desktop {

struct LiveAudioPipe {
  cwassistant::core::SpscRingBuffer<cwassistant::core::RealtimeSampleBlock, 32>
      blocks;
  std::atomic<qulonglong> overruns{0};
};

}  // namespace cwassistant::desktop
