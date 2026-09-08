#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwassistant::core {

struct CwTransmitSpan {
  bool key_down{false};
  std::uint32_t duration_units{0};
};

struct CwTransmitPlan {
  std::string text;
  std::vector<CwTransmitSpan> spans;
  std::uint64_t dot_duration_ns{0};
  std::uint64_t total_duration_ns{0};
};

// Converts already operator-confirmed text into the provider-neutral timing
// consumed by a future local keying adapter. It never opens a device or
// asserts PTT/KEY.
class CwTransmitEncoder final {
 public:
  [[nodiscard]] static std::optional<CwTransmitPlan> encode(
      std::string_view normalized_text, std::uint16_t words_per_minute);
};

}  // namespace cwassistant::core
