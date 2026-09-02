#pragma once

#include <QVector>

namespace cwassistant::desktop {

// Stateful, display-only spectral conditioning. It estimates a slow baseline
// for each FFT bin and compares every bin with side references far enough from
// a normal CW filter to reject the receiver passband shape. It never feeds the
// decoder or changes signal verification.
class WaterfallConditioner final {
 public:
  void reset() noexcept;
  [[nodiscard]] QVector<float> process(const QVector<float>& bins,
                                       bool symbols_mode,
                                       bool noise_suppression,
                                       double noise_margin_db,
                                       double lower_display_db,
                                       double upper_display_db,
                                       double bin_width_hz,
                                       double fallback_noise_db);

 private:
  QVector<float> baseline_db_;
};

}  // namespace cwassistant::desktop
