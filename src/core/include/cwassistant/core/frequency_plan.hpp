#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cwassistant::core {

// CAT providers operate on dial frequencies. Actual RF frequencies are
// derived separately so a transverter offset can never be sent to a radio by
// accident.
struct VfoFrequencyPlan {
  std::uint64_t rx_dial_hz{0};
  std::uint64_t tx_dial_hz{0};
  bool split_enabled{false};
};

struct TransverterOffsets {
  std::int64_t rx_offset_hz{0};
  std::int64_t tx_offset_hz{0};
};

struct ResolvedFrequencies {
  std::uint64_t rx_dial_hz{0};
  std::uint64_t tx_dial_hz{0};
  std::uint64_t rx_rf_hz{0};
  std::uint64_t tx_rf_hz{0};
  bool split_enabled{false};
};

[[nodiscard]] std::optional<ResolvedFrequencies> resolve_frequencies(
    const VfoFrequencyPlan& plan,
    const TransverterOffsets& offsets) noexcept;

[[nodiscard]] std::string_view adif_band_from_frequency(
    std::uint64_t frequency_hz) noexcept;

// Six decimal MHz digits preserve exact integer-Hz values in ADIF.
[[nodiscard]] std::string format_frequency_mhz(std::uint64_t frequency_hz);

}  // namespace cwassistant::core
