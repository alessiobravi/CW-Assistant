#pragma once

#include <QVariantList>
#include <QVector>

namespace cwassistant::desktop {

// Stateful, display-only spectral conditioning. It estimates a slow baseline
// for each FFT bin and compares every bin with side references far enough from
// a normal CW filter to reject the receiver passband shape. It never feeds the
// decoder or changes signal verification.
class WaterfallConditioner final {
 public:
  void reset() noexcept;
  // Translates the per-bin baseline by the same number of bins the waterfall
  // rows were slid when the receiver retuned.
  //
  // Resetting it instead threw away a calibration that takes a second or so to
  // re-converge, and every retune therefore painted a horizontal band across
  // the waterfall while it settled. An operator tuning across a band produced
  // a row of them. The baseline is per-bin and a pure translation is exactly
  // the transform that keeps each bin's history over the frequency it belongs
  // to, which is the same argument that makes sliding the rows correct.
  void shiftBins(qsizetype bins) noexcept;
  [[nodiscard]] QVector<float> process(const QVector<float>& bins,
                                       bool noise_suppression,
                                       double noise_margin_db,
                                       double lower_display_db,
                                       double upper_display_db,
                                       double bin_width_hz,
                                       double fallback_noise_db);

 private:
  QVector<float> baseline_db_;
};

// Builds the deliberately sparse CW-symbol view from verified per-channel
// keying envelopes. The result is a neutral row except at channels whose
// carrier is currently keyed; full-passband FFT texture belongs to the Audio
// spectrum view and is never copied into this raster.
[[nodiscard]] QVector<float> cwSymbolRow(
    const QVariantList& channels, qsizetype bin_count,
    double lower_frequency_hz, double upper_frequency_hz,
    double lower_display_db, double upper_display_db);

}  // namespace cwassistant::desktop
