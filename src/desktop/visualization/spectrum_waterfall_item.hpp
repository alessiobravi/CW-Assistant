#pragma once

#include <QElapsedTimer>
#include <QQuickItem>
#include <QVector>

#include <deque>

#include "spectrum_frame.hpp"

namespace cwassistant::desktop {

class SpectrumWaterfallItem : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(double lowerBoundDb READ lowerBoundDb WRITE setLowerBoundDb NOTIFY displayChanged)
  Q_PROPERTY(double upperBoundDb READ upperBoundDb WRITE setUpperBoundDb NOTIFY displayChanged)
  Q_PROPERTY(bool automaticRange READ automaticRange WRITE setAutomaticRange NOTIFY displayChanged)
  Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps NOTIFY displayChanged)
  Q_PROPERTY(int waterfallRate READ waterfallRate WRITE setWaterfallRate NOTIFY displayChanged)
  Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY displayChanged)
  Q_PROPERTY(double effectiveLowerBoundDb READ effectiveLowerBoundDb NOTIFY rangeChanged)
  Q_PROPERTY(double effectiveUpperBoundDb READ effectiveUpperBoundDb NOTIFY rangeChanged)
  Q_PROPERTY(double lowerFrequencyHz READ lowerFrequencyHz NOTIFY frequencyRangeChanged)
  Q_PROPERTY(double upperFrequencyHz READ upperFrequencyHz NOTIFY frequencyRangeChanged)
  Q_PROPERTY(qulonglong droppedRows READ droppedRows NOTIFY droppedRowsChanged)

 public:
  explicit SpectrumWaterfallItem(QQuickItem* parent = nullptr);

  [[nodiscard]] QObject* source() const noexcept;
  void setSource(QObject* source);
  [[nodiscard]] double lowerBoundDb() const noexcept;
  void setLowerBoundDb(double value);
  [[nodiscard]] double upperBoundDb() const noexcept;
  void setUpperBoundDb(double value);
  [[nodiscard]] bool automaticRange() const noexcept;
  void setAutomaticRange(bool value);
  [[nodiscard]] int targetFps() const noexcept;
  void setTargetFps(int value);
  [[nodiscard]] int waterfallRate() const noexcept;
  void setWaterfallRate(int value);
  [[nodiscard]] bool showGrid() const noexcept;
  void setShowGrid(bool value);
  [[nodiscard]] double effectiveLowerBoundDb() const noexcept;
  [[nodiscard]] double effectiveUpperBoundDb() const noexcept;
  [[nodiscard]] double lowerFrequencyHz() const noexcept;
  [[nodiscard]] double upperFrequencyHz() const noexcept;
  [[nodiscard]] qulonglong droppedRows() const noexcept;

 signals:
  void sourceChanged();
  void displayChanged();
  void rangeChanged();
  void frequencyRangeChanged();
  void droppedRowsChanged();

 protected:
  QSGNode* updatePaintNode(QSGNode* old_node,
                           UpdatePaintNodeData*) override;

 private slots:
  void acceptFrame(const cwassistant::desktop::SpectrumFrame& frame);
  void resetFrames();

 private:
  void updateAutomaticRange(const QVector<float>& bins);
  void scheduleRender();

  QObject* source_{nullptr};
  QVector<float> latest_bins_;
  std::deque<QVector<float>> waterfall_rows_;
  double lower_bound_db_{-120.0};
  double upper_bound_db_{-20.0};
  double effective_lower_bound_db_{-120.0};
  double effective_upper_bound_db_{-20.0};
  double lower_frequency_hz_{0.0};
  double upper_frequency_hz_{0.0};
  bool automatic_range_{true};
  bool show_grid_{true};
  int target_fps_{60};
  int waterfall_rate_{30};
  std::uint64_t last_row_timestamp_ns_{0};
  std::uint64_t last_sequence_{0};
  bool has_sequence_{false};
  qulonglong dropped_rows_{0};
  QElapsedTimer render_clock_;
};

}  // namespace cwassistant::desktop
