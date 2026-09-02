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
};

}  // namespace cwassistant::desktop

Q_DECLARE_METATYPE(cwassistant::desktop::SpectrumFrame)
