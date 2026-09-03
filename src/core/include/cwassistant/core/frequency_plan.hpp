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

enum class OmniRigRxFrequencyTarget {
  None,
  Frequency,
  FrequencyA,
  FrequencyB,
};

[[nodiscard]] std::optional<ResolvedFrequencies> resolve_frequencies(
    const VfoFrequencyPlan& plan,
    const TransverterOffsets& offsets) noexcept;

// Converts an operator-entered actual RF frequency back to the dial frequency
// expected by a CAT provider. The inverse is checked so an offset can never
// underflow, overflow, or be sent to the radio as though it were a dial value.
[[nodiscard]] std::optional<std::uint64_t> resolve_dial_frequency(
    std::uint64_t actual_rf_hz, std::int64_t transverter_offset_hz) noexcept;

// Parses an exact value in a decimal unit such as kHz (1,000) or MHz
// (1,000,000). Both '.' and ',' are accepted as decimal separators;
// grouping separators and sub-hertz precision are rejected.
[[nodiscard]] std::optional<std::uint64_t> parse_frequency_value(
    std::string_view text, std::uint64_t unit_hz) noexcept;

// Chooses the property that represents the active receive VFO from OmniRig's
// published parameter masks. Offline or transmitting radios are never
// writable through the RX-only operating control.
[[nodiscard]] OmniRigRxFrequencyTarget select_omnirig_rx_frequency_target(
    bool online, bool receiving, std::uint32_t writable_parameters,
    std::uint32_t vfo) noexcept;

// Accumulates operator steps against the most recently accepted request while
// asynchronous provider readback is pending.
[[nodiscard]] std::optional<std::uint64_t> step_rx_frequency(
    std::uint64_t reported_rf_hz,
    std::optional<std::uint64_t> pending_rf_hz, std::uint64_t step_hz,
    int direction) noexcept;

// Maps a decoded audio tone to actual RF around the configured receiver CW
// pitch. Upper-sideband mapping raises RF as the audio tone rises; CW-L/LSB
// mapping does the opposite. All arithmetic is checked in integer hertz.
[[nodiscard]] std::optional<std::uint64_t> resolve_audio_tone_rf(
    std::uint64_t reference_rf_hz, double audio_tone_hz,
    double reference_tone_hz, bool upper_sideband) noexcept;

[[nodiscard]] std::string_view adif_band_from_frequency(
    std::uint64_t frequency_hz) noexcept;

// Six decimal MHz digits preserve exact integer-Hz values in ADIF.
[[nodiscard]] std::string format_frequency_mhz(std::uint64_t frequency_hz);

}  // namespace cwassistant::core
