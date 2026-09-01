#include "cwassistant/core/frequency_plan.hpp"

#include <array>
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
