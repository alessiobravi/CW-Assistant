#pragma once

#include <QMetaType>
#include <QVector>

namespace cwassistant::desktop {

struct SpectrumFrame {
  QVector<float> bins_dbfs;
  quint64 sequence{0};
  quint64 timestamp_ns{0};
  double lower_frequency_hz{0.0};
  double upper_frequency_hz{0.0};
  QVector<float> instantaneous_bins_dbfs;
  // How many frames were refused for backpressure since the last one that was
  // delivered.
  //
  // The waterfall fills intervals it received nothing for with blank rows, so
  // that a genuine break in reception reads as a gap and the time axis stays
  // honest. Once frames began being dropped to bound memory, that padding had
  // no way to tell "nothing arrived" from "we could not draw what arrived",
  // and the display filled with black stripes. This says which it was: the
  // receiver was fine, the display simply could not keep up, so those
  // intervals are not a break in reception and must not be drawn as one.
  quint64 dropped_before{0};
};

}  // namespace cwassistant::desktop

Q_DECLARE_METATYPE(cwassistant::desktop::SpectrumFrame)
