#include "waterfall_conditioner.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cwassistant::desktop {

void WaterfallConditioner::reset() noexcept { baseline_db_.clear(); }

QVector<float> WaterfallConditioner::process(
    const QVector<float>& bins, const bool symbols_mode,
    const bool noise_suppression, const double noise_margin_db,
    const double lower_display_db, const double upper_display_db,
    const double bin_width_hz, const double fallback_noise_db) {
  if (bins.isEmpty()) return {};
  if (baseline_db_.size() != bins.size()) {
    baseline_db_ = bins;
    for (float& value : baseline_db_) {
      if (!std::isfinite(value))
        value = static_cast<float>(fallback_noise_db);
    }
  }

  const double usable_bin_width = std::max(0.25, bin_width_hz);
  const qsizetype inner = std::max<qsizetype>(
      2, static_cast<qsizetype>(std::ceil(55.0 / usable_bin_width)));
  const qsizetype outer = std::max<qsizetype>(
      inner + 2,
      static_cast<qsizetype>(std::ceil(180.0 / usable_bin_width)));
  const double margin = std::max(1.0, noise_margin_db);
  const double muted = lower_display_db;
  const double span = std::max(12.0, upper_display_db - lower_display_db);
  QVector<float> result(bins.size(), static_cast<float>(muted));
  std::vector<float> sides;
  sides.reserve(static_cast<std::size_t>(2 * (outer - inner + 1)));

  for (qsizetype index = 0; index < bins.size(); ++index) {
    const double observed = std::isfinite(bins[index])
        ? static_cast<double>(bins[index]) : fallback_noise_db;
    sides.clear();
    for (qsizetype offset = inner; offset <= outer; ++offset) {
      if (index >= offset && std::isfinite(bins[index - offset]))
        sides.push_back(bins[index - offset]);
      if (index + offset < bins.size() &&
          std::isfinite(bins[index + offset]))
        sides.push_back(bins[index + offset]);
    }
    double side_reference = fallback_noise_db;
    if (!sides.empty()) {
      const auto middle = sides.begin() +
          static_cast<std::ptrdiff_t>(sides.size() / 2U);
      std::nth_element(sides.begin(), middle, sides.end());
      side_reference = static_cast<double>(*middle);
    }
    // Do not let a continuously keyed carrier teach its own bin that it is
    // noise. The side reference caps the slow per-bin baseline a few dB above
    // the surrounding local floor.
    const double temporal_reference = std::min(
        static_cast<double>(baseline_db_[index]), side_reference + 3.0);
    const double reference = 0.65 * side_reference +
                             0.35 * temporal_reference;
    const double excess = observed - reference;

    if (!symbols_mode && !noise_suppression) {
      result[index] = static_cast<float>(observed);
    } else if (excess >= margin) {
      const double strength = std::clamp((excess - margin) / 18.0,
                                         0.0, 1.0);
      if (symbols_mode) {
        result[index] = static_cast<float>(
            muted + span * (0.62 + 0.38 * strength));
      } else {
        const double softened = muted + span *
            std::clamp(0.20 + 0.80 * strength, 0.0, 1.0);
        result[index] = static_cast<float>(std::max(observed, softened));
      }
    } else if (!symbols_mode) {
      const double knee = std::clamp(excess / margin, 0.0, 1.0);
      result[index] = static_cast<float>(muted * (1.0 - knee) +
                                         observed * knee);
    }
  }

  for (qsizetype index = 0; index < bins.size(); ++index) {
    if (!std::isfinite(bins[index])) continue;
    const float observed = bins[index];
    const float smoothing = observed < baseline_db_[index] ? 0.12F : 0.003F;
    baseline_db_[index] += smoothing * (observed - baseline_db_[index]);
  }
  return result;
}

}  // namespace cwassistant::desktop
