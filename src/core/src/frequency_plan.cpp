#include "cwassistant/core/frequency_plan.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cwassistant::core {
namespace {

struct BandRange {
  std::uint64_t lower_hz;
  std::uint64_t upper_hz;
  std::string_view name;
};

constexpr std::array kAdifBands{
    BandRange{135'700, 137'800, "2190M"},
    BandRange{472'000, 479'000, "630M"},
    BandRange{501'000, 504'000, "560M"},
    BandRange{1'800'000, 2'000'000, "160M"},
    BandRange{3'500'000, 4'000'000, "80M"},
    BandRange{5'060'000, 5'450'000, "60M"},
    BandRange{7'000'000, 7'300'000, "40M"},
    BandRange{10'100'000, 10'150'000, "30M"},
    BandRange{14'000'000, 14'350'000, "20M"},
    BandRange{18'068'000, 18'168'000, "17M"},
    BandRange{21'000'000, 21'450'000, "15M"},
    BandRange{24'890'000, 24'990'000, "12M"},
    BandRange{28'000'000, 29'700'000, "10M"},
    BandRange{40'000'000, 45'000'000, "8M"},
    BandRange{50'000'000, 54'000'000, "6M"},
    BandRange{54'000'001, 69'900'000, "5M"},
    BandRange{70'000'000, 71'000'000, "4M"},
    BandRange{144'000'000, 148'000'000, "2M"},
    BandRange{222'000'000, 225'000'000, "1.25M"},
    BandRange{420'000'000, 450'000'000, "70CM"},
    BandRange{902'000'000, 928'000'000, "33CM"},
    BandRange{1'240'000'000, 1'300'000'000, "23CM"},
    BandRange{2'300'000'000, 2'450'000'000, "13CM"},
    BandRange{3'300'000'000, 3'500'000'000, "9CM"},
    BandRange{5'650'000'000, 5'925'000'000, "6CM"},
    BandRange{10'000'000'000, 10'500'000'000, "3CM"},
    BandRange{24'000'000'000, 24'250'000'000, "1.25CM"},
    BandRange{47'000'000'000, 47'200'000'000, "6MM"},
    BandRange{75'500'000'000, 81'000'000'000, "4MM"},
    BandRange{119'980'000'000, 123'000'000'000, "2.5MM"},
    BandRange{134'000'000'000, 149'000'000'000, "2MM"},
    BandRange{241'000'000'000, 250'000'000'000, "1MM"},
    BandRange{300'000'000'000, 7'500'000'000'000, "SUBMM"},
};

std::optional<std::uint64_t> add_signed_offset(
    const std::uint64_t frequency,
    const std::int64_t offset) noexcept {
  if (offset >= 0) {
    const auto positive = static_cast<std::uint64_t>(offset);
    if (frequency > std::numeric_limits<std::uint64_t>::max() - positive) {
      return std::nullopt;
    }
    const auto result = frequency + positive;
    return result == 0 ? std::nullopt : std::optional{result};
  }
  const auto magnitude = static_cast<std::uint64_t>(-(offset + 1)) + 1;
  if (frequency <= magnitude) {
    return std::nullopt;
  }
  return frequency - magnitude;
}

}  // namespace

std::optional<ResolvedFrequencies> resolve_frequencies(
    const VfoFrequencyPlan& plan,
    const TransverterOffsets& offsets) noexcept {
  if (plan.rx_dial_hz == 0) {
    return std::nullopt;
  }
  const auto tx_dial = plan.split_enabled ? plan.tx_dial_hz : plan.rx_dial_hz;
  if (tx_dial == 0) {
    return std::nullopt;
  }
  const auto rx_rf = add_signed_offset(plan.rx_dial_hz, offsets.rx_offset_hz);
  const auto tx_rf = add_signed_offset(tx_dial, offsets.tx_offset_hz);
  if (!rx_rf || !tx_rf) {
    return std::nullopt;
  }
  return ResolvedFrequencies{
      .rx_dial_hz = plan.rx_dial_hz,
      .tx_dial_hz = tx_dial,
      .rx_rf_hz = *rx_rf,
      .tx_rf_hz = *tx_rf,
      .split_enabled = plan.split_enabled,
  };
}

std::optional<std::uint64_t> resolve_dial_frequency(
    const std::uint64_t actual_rf_hz,
    const std::int64_t transverter_offset_hz) noexcept {
  if (actual_rf_hz == 0) {
    return std::nullopt;
  }
  if (transverter_offset_hz >= 0) {
    const auto offset = static_cast<std::uint64_t>(transverter_offset_hz);
    if (actual_rf_hz <= offset) {
      return std::nullopt;
    }
    return actual_rf_hz - offset;
  }

  const auto magnitude =
      static_cast<std::uint64_t>(-(transverter_offset_hz + 1)) + 1;
  if (actual_rf_hz > std::numeric_limits<std::uint64_t>::max() - magnitude) {
    return std::nullopt;
  }
  return actual_rf_hz + magnitude;
}

std::optional<std::uint64_t> parse_frequency_value(
    std::string_view text, const std::uint64_t unit_hz) noexcept {
  if (unit_hz == 0) {
    return std::nullopt;
  }
  auto precision_scale = unit_hz;
  unsigned int maximum_fractional_digits = 0;
  while (precision_scale > 1 && precision_scale % 10 == 0) {
    precision_scale /= 10;
    ++maximum_fractional_digits;
  }
  if (precision_scale != 1) {
    return std::nullopt;
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return std::nullopt;
  }

  std::uint64_t whole_units = 0;
  std::uint64_t fractional_hz = 0;
  unsigned int fractional_digits = 0;
  bool saw_digit = false;
  bool saw_separator = false;
  for (const char character : text) {
    if (character >= '0' && character <= '9') {
      saw_digit = true;
      const auto digit = static_cast<std::uint64_t>(character - '0');
      if (!saw_separator) {
        if (whole_units >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
          return std::nullopt;
        }
        whole_units = whole_units * 10 + digit;
      } else {
        if (fractional_digits >= maximum_fractional_digits) {
          return std::nullopt;
        }
        fractional_hz = fractional_hz * 10 + digit;
        ++fractional_digits;
      }
      continue;
    }
    if ((character == '.' || character == ',') && !saw_separator &&
        saw_digit) {
      saw_separator = true;
      continue;
    }
    return std::nullopt;
  }
  if (!saw_digit || (saw_separator && fractional_digits == 0)) {
    return std::nullopt;
  }
  while (fractional_digits < maximum_fractional_digits) {
    fractional_hz *= 10;
    ++fractional_digits;
  }
  if (whole_units >
      (std::numeric_limits<std::uint64_t>::max() - fractional_hz) / unit_hz) {
    return std::nullopt;
  }
  const auto frequency_hz = whole_units * unit_hz + fractional_hz;
  return frequency_hz == 0 ? std::nullopt
                           : std::optional<std::uint64_t>{frequency_hz};
}

OmniRigRxFrequencyTarget select_omnirig_rx_frequency_target(
    const bool online, const bool receiving,
    const std::uint32_t writable_parameters, const std::uint32_t vfo) noexcept {
  constexpr std::uint32_t kFrequency = 0x00000002;
  constexpr std::uint32_t kFrequencyA = 0x00000004;
  constexpr std::uint32_t kFrequencyB = 0x00000008;
  constexpr std::uint32_t kVfoAa = 0x00000080;
  constexpr std::uint32_t kVfoAb = 0x00000100;
  constexpr std::uint32_t kVfoBa = 0x00000200;
  constexpr std::uint32_t kVfoBb = 0x00000400;
  constexpr std::uint32_t kVfoA = 0x00000800;
  constexpr std::uint32_t kVfoB = 0x00001000;
  if (!online || !receiving) {
    return OmniRigRxFrequencyTarget::None;
  }
  const bool receives_on_a =
      vfo == kVfoAa || vfo == kVfoAb || vfo == kVfoA;
  const bool receives_on_b =
      vfo == kVfoBa || vfo == kVfoBb || vfo == kVfoB;
  if (receives_on_a && (writable_parameters & kFrequencyA) != 0) {
    return OmniRigRxFrequencyTarget::FrequencyA;
  }
  if (receives_on_b && (writable_parameters & kFrequencyB) != 0) {
    return OmniRigRxFrequencyTarget::FrequencyB;
  }
  if ((writable_parameters & kFrequency) != 0) {
    return OmniRigRxFrequencyTarget::Frequency;
  }
  return OmniRigRxFrequencyTarget::None;
}

std::optional<std::uint64_t> step_rx_frequency(
    const std::uint64_t reported_rf_hz,
    const std::optional<std::uint64_t> pending_rf_hz,
    const std::uint64_t step_hz, const int direction) noexcept {
  if (reported_rf_hz == 0 || step_hz == 0 ||
      (direction != -1 && direction != 1)) {
    return std::nullopt;
  }
  const auto base = pending_rf_hz.value_or(reported_rf_hz);
  if (direction < 0) {
    return base > step_hz ? std::optional<std::uint64_t>(base - step_hz)
                          : std::nullopt;
  }
  return base <= std::numeric_limits<std::uint64_t>::max() - step_hz
             ? std::optional<std::uint64_t>(base + step_hz)
             : std::nullopt;
}

std::optional<std::uint64_t> resolve_audio_tone_rf(
    const std::uint64_t reference_rf_hz, const double audio_tone_hz,
    const double reference_tone_hz, const bool upper_sideband) noexcept {
  if (reference_rf_hz == 0 || !std::isfinite(audio_tone_hz) ||
      !std::isfinite(reference_tone_hz)) {
    return std::nullopt;
  }
  const double difference = audio_tone_hz - reference_tone_hz;
  if (difference <=
          static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
      difference >=
          static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  const auto audio_offset = static_cast<std::int64_t>(std::llround(difference));
  return add_signed_offset(reference_rf_hz,
                           upper_sideband ? audio_offset : -audio_offset);
}

std::string_view adif_band_from_frequency(
    const std::uint64_t frequency_hz) noexcept {
  for (const auto& band : kAdifBands) {
    if (frequency_hz >= band.lower_hz && frequency_hz <= band.upper_hz) {
      return band.name;
    }
  }
  return {};
}

std::string format_frequency_mhz(const std::uint64_t frequency_hz) {
  std::ostringstream output;
  output << frequency_hz / 1'000'000 << '.' << std::setw(6)
         << std::setfill('0') << frequency_hz % 1'000'000;
  return output.str();
}

}  // namespace cwassistant::core
