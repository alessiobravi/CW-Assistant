#include "spectrum_waterfall_item.hpp"

#include <QColor>
#include <QImage>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGTexture>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "../replay/replay_controller.hpp"

namespace cwassistant::desktop {
namespace {

constexpr int kMaximumWaterfallRows = 3'600;

class DisplayNode final : public QSGNode {
 public:
  DisplayNode() {
    grid = makeGeometryNode(QSGGeometry::DrawLines, QColor("#2a3a49"));
    appendChildNode(grid);
    spectrum = makeGeometryNode(QSGGeometry::DrawLineStrip,
                                QColor("#64e6d2"));
    appendChildNode(spectrum);
  }

  static QSGGeometryNode* makeGeometryNode(
      const QSGGeometry::DrawingMode mode, const QColor& color) {
    auto* node = new QSGGeometryNode;
    auto* geometry =
        new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(mode);
    geometry->setLineWidth(1.0F);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto* material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
  }

  QSGImageNode* waterfall{nullptr};
  QSGGeometryNode* grid{nullptr};
  QSGGeometryNode* spectrum{nullptr};
};

QRgb waterfallColor(const float value) {
  const float t = std::clamp(value, 0.0F, 1.0F);
  if (t < 0.35F) {
    const float u = t / 0.35F;
    return qRgb(static_cast<int>(4.0F + 8.0F * u),
                static_cast<int>(12.0F + 48.0F * u),
                static_cast<int>(26.0F + 94.0F * u));
  }
  if (t < 0.72F) {
    const float u = (t - 0.35F) / 0.37F;
    return qRgb(static_cast<int>(12.0F + 46.0F * u),
                static_cast<int>(60.0F + 170.0F * u),
                static_cast<int>(120.0F + 75.0F * u));
  }
  const float u = (t - 0.72F) / 0.28F;
  return qRgb(static_cast<int>(58.0F + 197.0F * u),
              static_cast<int>(230.0F + 25.0F * u),
              static_cast<int>(195.0F - 95.0F * u));
}

}  // namespace

SpectrumWaterfallItem::SpectrumWaterfallItem(QQuickItem* parent)
    : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  render_clock_.start();
}

QObject* SpectrumWaterfallItem::source() const noexcept { return source_; }

void SpectrumWaterfallItem::setSource(QObject* source) {
  if (source_ == source) {
    return;
  }
  if (source_ != nullptr) {
    disconnect(source_, nullptr, this, nullptr);
  }
  source_ = source;
  if (auto* replay = qobject_cast<ReplayController*>(source_)) {
    connect(replay, &ReplayController::frameReady, this,
            &SpectrumWaterfallItem::acceptFrame);
    connect(replay, &ReplayController::sourceReset, this,
            &SpectrumWaterfallItem::resetFrames);
  }
  resetFrames();
  emit sourceChanged();
}

double SpectrumWaterfallItem::lowerBoundDb() const noexcept {
  return lower_bound_db_;
}
void SpectrumWaterfallItem::setLowerBoundDb(const double value) {
  const double clamped = std::clamp(value, -200.0, 40.0);
  if (qFuzzyCompare(lower_bound_db_, clamped)) return;
  lower_bound_db_ = clamped;
  if (!automatic_range_) {
    effective_lower_bound_db_ = lower_bound_db_;
    emit rangeChanged();
  }
  emit displayChanged();
  update();
}
double SpectrumWaterfallItem::upperBoundDb() const noexcept {
  return upper_bound_db_;
}
void SpectrumWaterfallItem::setUpperBoundDb(const double value) {
  const double clamped = std::clamp(value, -190.0, 50.0);
  if (qFuzzyCompare(upper_bound_db_, clamped)) return;
  upper_bound_db_ = clamped;
  if (!automatic_range_) {
    effective_upper_bound_db_ = upper_bound_db_;
    emit rangeChanged();
  }
  emit displayChanged();
  update();
}
bool SpectrumWaterfallItem::automaticRange() const noexcept {
  return automatic_range_;
}
void SpectrumWaterfallItem::setAutomaticRange(const bool value) {
  if (automatic_range_ == value) return;
  automatic_range_ = value;
  if (!automatic_range_) {
    automatic_range_initialized_ = false;
    effective_lower_bound_db_ = lower_bound_db_;
    effective_upper_bound_db_ = upper_bound_db_;
    emit rangeChanged();
  } else if (!latest_bins_.isEmpty()) {
    updateAutomaticRange(latest_bins_);
  }
  emit displayChanged();
  update();
}
double SpectrumWaterfallItem::automaticRangeSpanDb() const noexcept {
  return automatic_range_span_db_;
}
void SpectrumWaterfallItem::setAutomaticRangeSpanDb(const double value) {
  const double clamped = std::clamp(value, 30.0, 100.0);
  if (qFuzzyCompare(automatic_range_span_db_, clamped)) return;
  automatic_range_span_db_ = clamped;
  if (automatic_range_ && !latest_bins_.isEmpty()) {
    automatic_range_initialized_ = false;
    updateAutomaticRange(latest_bins_);
  }
  emit displayChanged();
  update();
}
bool SpectrumWaterfallItem::noiseSuppression() const noexcept {
  return noise_suppression_;
}
void SpectrumWaterfallItem::setNoiseSuppression(const bool value) {
  if (noise_suppression_ == value) return;
  noise_suppression_ = value;
  waterfall_rows_.clear();
  last_row_timestamp_ns_ = 0;
  has_row_timestamp_ = false;
  emit displayChanged();
  update();
}
double SpectrumWaterfallItem::noiseMarginDb() const noexcept {
  return noise_margin_db_;
}
void SpectrumWaterfallItem::setNoiseMarginDb(const double value) {
  const double clamped = std::clamp(value, 0.0, 30.0);
  if (qFuzzyCompare(noise_margin_db_, clamped)) return;
  noise_margin_db_ = clamped;
  waterfall_rows_.clear();
  last_row_timestamp_ns_ = 0;
  has_row_timestamp_ = false;
  emit displayChanged();
  update();
}
int SpectrumWaterfallItem::targetFps() const noexcept { return target_fps_; }
void SpectrumWaterfallItem::setTargetFps(const int value) {
  const int clamped = std::clamp(value, 10, 120);
  if (target_fps_ == clamped) return;
  target_fps_ = clamped;
  emit displayChanged();
}
int SpectrumWaterfallItem::waterfallRate() const noexcept {
  return waterfall_rate_;
}
void SpectrumWaterfallItem::setWaterfallRate(const int value) {
  const int clamped = std::clamp(value, 1, 120);
  if (waterfall_rate_ == clamped) return;
  waterfall_rate_ = clamped;
  waterfall_rows_.clear();
  has_row_timestamp_ = false;
  emit displayChanged();
  update();
}
int SpectrumWaterfallItem::waterfallTimeSpanSeconds() const noexcept {
  return waterfall_time_span_seconds_;
}
int SpectrumWaterfallItem::displayMode() const noexcept {
  return display_mode_;
}
void SpectrumWaterfallItem::setDisplayMode(const int value) {
  const int clamped = std::clamp(value, 0, 1);
  if (display_mode_ == clamped) return;
  display_mode_ = clamped;
  waterfall_rows_.clear();
  has_row_timestamp_ = false;
  emit displayChanged();
  update();
}
void SpectrumWaterfallItem::setWaterfallTimeSpanSeconds(const int value) {
  const int clamped = std::clamp(value, 5, 30);
  if (waterfall_time_span_seconds_ == clamped) return;
  waterfall_time_span_seconds_ = clamped;
  while (waterfall_rows_.size() >
         static_cast<std::size_t>(waterfallRowCapacity())) {
    waterfall_rows_.pop_back();
  }
  emit displayChanged();
  update();
}
int SpectrumWaterfallItem::waterfallRowCapacity() const noexcept {
  return std::clamp(waterfall_rate_ * waterfall_time_span_seconds_, 1,
                    kMaximumWaterfallRows);
}
int SpectrumWaterfallItem::storedWaterfallRows() const noexcept {
  return static_cast<int>(waterfall_rows_.size());
}
bool SpectrumWaterfallItem::showGrid() const noexcept { return show_grid_; }
void SpectrumWaterfallItem::setShowGrid(const bool value) {
  if (show_grid_ == value) return;
  show_grid_ = value;
  emit displayChanged();
  update();
}
double SpectrumWaterfallItem::effectiveLowerBoundDb() const noexcept {
  return effective_lower_bound_db_;
}
double SpectrumWaterfallItem::effectiveUpperBoundDb() const noexcept {
  return effective_upper_bound_db_;
}
double SpectrumWaterfallItem::lowerFrequencyHz() const noexcept {
  return lower_frequency_hz_;
}
double SpectrumWaterfallItem::upperFrequencyHz() const noexcept {
  return upper_frequency_hz_;
}
qulonglong SpectrumWaterfallItem::droppedRows() const noexcept {
  return dropped_rows_;
}
double SpectrumWaterfallItem::estimatedNoiseFloorDb() const noexcept {
  return estimated_noise_floor_db_;
}

void SpectrumWaterfallItem::acceptFrame(const SpectrumFrame& frame) {
  if (frame.bins_dbfs.isEmpty()) return;
  if (!latest_bins_.isEmpty() && latest_bins_.size() != frame.bins_dbfs.size()) {
    waterfall_rows_.clear();
    has_row_timestamp_ = false;
  }
  latest_bins_ = frame.bins_dbfs;
  const QVector<float>& waterfall_bins =
      display_mode_ == 1 &&
              frame.instantaneous_bins_dbfs.size() == frame.bins_dbfs.size()
          ? frame.instantaneous_bins_dbfs
          : frame.bins_dbfs;
  if (!qFuzzyCompare(lower_frequency_hz_, frame.lower_frequency_hz) ||
      !qFuzzyCompare(upper_frequency_hz_, frame.upper_frequency_hz)) {
    lower_frequency_hz_ = frame.lower_frequency_hz;
    upper_frequency_hz_ = frame.upper_frequency_hz;
    emit frequencyRangeChanged();
  }
  if (has_sequence_ && frame.sequence > last_sequence_ + 1) {
    dropped_rows_ += frame.sequence - last_sequence_ - 1;
    emit droppedRowsChanged();
  }
  last_sequence_ = frame.sequence;
  has_sequence_ = true;
  updateNoiseFloor(latest_bins_);
  if (automatic_range_) updateAutomaticRange(latest_bins_);

  const std::uint64_t row_interval =
      1'000'000'000ULL / static_cast<std::uint64_t>(waterfall_rate_);
  if (!has_row_timestamp_) {
    appendWaterfallRow(conditionedWaterfallRow(waterfall_bins));
    last_row_timestamp_ns_ = frame.timestamp_ns;
    has_row_timestamp_ = true;
  } else if (frame.timestamp_ns >= last_row_timestamp_ns_ + row_interval) {
    const std::uint64_t elapsed_intervals =
        (frame.timestamp_ns - last_row_timestamp_ns_) / row_interval;
    const std::uint64_t missing_intervals = elapsed_intervals - 1;
    const std::uint64_t retained_missing = std::min<std::uint64_t>(
        missing_intervals,
        static_cast<std::uint64_t>(waterfallRowCapacity() - 1));
    const QVector<float> blank = blankWaterfallRow(waterfall_bins.size());
    for (std::uint64_t i = 0; i < retained_missing; ++i) {
      appendWaterfallRow(blank);
    }
    appendWaterfallRow(conditionedWaterfallRow(waterfall_bins));
    last_row_timestamp_ns_ += elapsed_intervals * row_interval;
  }
  scheduleRender();
}

void SpectrumWaterfallItem::resetFrames() {
  latest_bins_.clear();
  waterfall_rows_.clear();
  last_row_timestamp_ns_ = 0;
  has_row_timestamp_ = false;
  last_sequence_ = 0;
  has_sequence_ = false;
  automatic_range_initialized_ = false;
  noise_floor_initialized_ = false;
  estimated_noise_floor_db_ = -120.0;
  if (automatic_range_) {
    effective_lower_bound_db_ = -120.0;
    effective_upper_bound_db_ = -20.0;
    emit rangeChanged();
  }
  dropped_rows_ = 0;
  lower_frequency_hz_ = 0.0;
  upper_frequency_hz_ = 0.0;
  emit droppedRowsChanged();
  emit noiseFloorChanged();
  emit frequencyRangeChanged();
  update();
}

void SpectrumWaterfallItem::updateAutomaticRange(const QVector<float>& bins) {
  QVector<float> finite;
  finite.reserve(bins.size());
  for (const float bin : bins) {
    if (std::isfinite(bin)) finite.push_back(bin);
  }
  if (finite.isEmpty()) return;
  std::sort(finite.begin(), finite.end());
  const qsizetype high_index = static_cast<qsizetype>(
      (static_cast<quint64>(finite.size() - 1) * 99ULL) / 100ULL);
  double low = std::clamp(estimated_noise_floor_db_ - 8.0, -200.0, 20.0);
  double high = std::max(static_cast<double>(finite[high_index]) + 3.0,
                         low + automatic_range_span_db_);
  high = std::clamp(high, -190.0, 50.0);
  if (high - low < automatic_range_span_db_) {
    low = std::max(-200.0, high - automatic_range_span_db_);
  }
  if (!automatic_range_initialized_) {
    effective_lower_bound_db_ = low;
    effective_upper_bound_db_ = high;
    automatic_range_initialized_ = true;
    emit rangeChanged();
    return;
  }
  constexpr double floor_smoothing = 0.04;
  const double ceiling_smoothing =
      high > effective_upper_bound_db_ ? 0.25 : 0.02;
  const double next_low =
      effective_lower_bound_db_ * (1.0 - floor_smoothing) +
      low * floor_smoothing;
  const double next_high =
      effective_upper_bound_db_ * (1.0 - ceiling_smoothing) +
      high * ceiling_smoothing;
  if (std::abs(next_low - effective_lower_bound_db_) > 0.02 ||
      std::abs(next_high - effective_upper_bound_db_) > 0.02) {
    effective_lower_bound_db_ = next_low;
    effective_upper_bound_db_ = next_high;
    emit rangeChanged();
  }
}

void SpectrumWaterfallItem::updateNoiseFloor(const QVector<float>& bins) {
  QVector<float> finite;
  finite.reserve(bins.size());
  for (const float bin : bins) {
    if (std::isfinite(bin)) finite.push_back(bin);
  }
  if (finite.isEmpty()) return;
  const qsizetype index = static_cast<qsizetype>(finite.size() / 5);
  std::nth_element(finite.begin(), finite.begin() + index, finite.end());
  const double observed = static_cast<double>(finite[index]);
  const double previous = estimated_noise_floor_db_;
  if (!noise_floor_initialized_) {
    estimated_noise_floor_db_ = observed;
    noise_floor_initialized_ = true;
  } else {
    // Follow an AGC-driven rise promptly so the waterfall gate does not flash
    // yellow, but release slowly enough that momentary quiet does not pump the
    // palette in the opposite direction.
    const double smoothing = observed > estimated_noise_floor_db_ ? 0.15 : 0.03;
    estimated_noise_floor_db_ +=
        smoothing * (observed - estimated_noise_floor_db_);
  }
  if (std::abs(previous - estimated_noise_floor_db_) > 0.02) {
    emit noiseFloorChanged();
  }
}

QVector<float> SpectrumWaterfallItem::conditionedWaterfallRow(
    const QVector<float>& bins) const {
  if ((!noise_suppression_ && display_mode_ == 0) ||
      !noise_floor_initialized_) return bins;
  QVector<float> conditioned = bins;
  const double cutoff = estimated_noise_floor_db_ + noise_margin_db_;
  const double muted = estimated_noise_floor_db_ - 18.0;
  const double knee = std::max(1.0, noise_margin_db_);
  for (float& bin : conditioned) {
    if (!std::isfinite(bin)) {
      bin = static_cast<float>(muted);
      continue;
    }
    if (static_cast<double>(bin) < cutoff) {
      const double mix = std::clamp(
          (static_cast<double>(bin) - estimated_noise_floor_db_) / knee,
          0.0, 1.0);
      bin = static_cast<float>(muted * (1.0 - mix) +
                               static_cast<double>(bin) * mix);
    } else if (display_mode_ == 1) {
      // The CW-symbol view is an acoustic keying raster, not decoded text:
      // preserve the instantaneous time edge and give every confidently
      // above-floor mark a consistent visible floor.
      bin = std::max(bin, static_cast<float>(cutoff + 12.0));
    }
  }
  return conditioned;
}

QVector<float> SpectrumWaterfallItem::blankWaterfallRow(
    const qsizetype width) const {
  const float level = static_cast<float>(
      noise_floor_initialized_ ? estimated_noise_floor_db_ - 18.0
                               : effective_lower_bound_db_);
  return QVector<float>(width, level);
}

void SpectrumWaterfallItem::appendWaterfallRow(QVector<float> row) {
  waterfall_rows_.push_front(std::move(row));
  while (waterfall_rows_.size() >
         static_cast<std::size_t>(waterfallRowCapacity())) {
    waterfall_rows_.pop_back();
  }
}

void SpectrumWaterfallItem::scheduleRender() {
  const qint64 minimum_interval = 1'000 / target_fps_;
  if (!render_clock_.isValid() || render_clock_.elapsed() >= minimum_interval) {
    render_clock_.restart();
    update();
  }
}

QSGNode* SpectrumWaterfallItem::updatePaintNode(
    QSGNode* old_node, UpdatePaintNodeData*) {
  auto* root = static_cast<DisplayNode*>(old_node);
  if (root == nullptr) root = new DisplayNode;

  const float width = static_cast<float>(this->width());
  const float height = static_cast<float>(this->height());
  const float spectrum_height = height * 0.36F;
  const float waterfall_top = spectrum_height + 8.0F;
  const float waterfall_height = std::max(0.0F, height - waterfall_top);

  auto* spectrum_geometry = root->spectrum->geometry();
  spectrum_geometry->allocate(latest_bins_.size());
  auto* vertices = spectrum_geometry->vertexDataAsPoint2D();
  const double span = std::max(1.0, effective_upper_bound_db_ -
                                       effective_lower_bound_db_);
  for (qsizetype i = 0; i < latest_bins_.size(); ++i) {
    const float x = latest_bins_.size() > 1
                        ? width * static_cast<float>(i) /
                              static_cast<float>(latest_bins_.size() - 1)
                        : 0.0F;
    const double normalized =
        std::clamp((static_cast<double>(latest_bins_[i]) -
                    effective_lower_bound_db_) /
                       span,
                   0.0, 1.0);
    vertices[i].set(x, spectrum_height *
                           static_cast<float>(1.0 - normalized));
  }
  root->spectrum->markDirty(QSGNode::DirtyGeometry);

  auto* grid_geometry = root->grid->geometry();
  const int grid_lines = show_grid_ ? 8 : 0;
  grid_geometry->allocate(grid_lines * 2);
  auto* grid_vertices = grid_geometry->vertexDataAsPoint2D();
  if (show_grid_) {
    int vertex = 0;
    for (int i = 1; i < 5; ++i) {
      const float x = width * static_cast<float>(i) / 5.0F;
      grid_vertices[vertex++].set(x, 0.0F);
      grid_vertices[vertex++].set(x, height);
    }
    for (int i = 1; i < 4; ++i) {
      const float y = spectrum_height * static_cast<float>(i) / 4.0F;
      grid_vertices[vertex++].set(0.0F, y);
      grid_vertices[vertex++].set(width, y);
    }
    grid_vertices[vertex++].set(0.0F, waterfall_top);
    grid_vertices[vertex].set(width, waterfall_top);
  }
  root->grid->markDirty(QSGNode::DirtyGeometry);

  if (!latest_bins_.isEmpty() && window() != nullptr) {
    const int image_width = latest_bins_.size();
    const int image_height = waterfallRowCapacity();
    QImage image(image_width, image_height, QImage::Format_RGB32);
    const QRgb blank_color = waterfallColor(0.0F);
    for (int y = 0; y < image_height; ++y) {
      auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
      if (y >= storedWaterfallRows()) {
        std::fill_n(scanline, image_width, blank_color);
        continue;
      }
      const auto& row = waterfall_rows_[static_cast<std::size_t>(y)];
      for (int x = 0; x < image_width; ++x) {
        const float normalized = static_cast<float>(std::clamp(
            (static_cast<double>(row[x]) - effective_lower_bound_db_) / span,
            0.0, 1.0));
        scanline[x] = waterfallColor(normalized);
      }
    }
    auto* texture = window()->createTextureFromImage(image);
    if (texture == nullptr) {
      if (root->waterfall != nullptr) root->waterfall->setRect(QRectF{});
      return root;
    }
    if (root->waterfall == nullptr) {
      root->waterfall = window()->createImageNode();
      if (root->waterfall == nullptr) {
        delete texture;
        return root;
      }
      root->waterfall->setTexture(texture);
      root->waterfall->setOwnsTexture(true);
      root->prependChildNode(root->waterfall);
    } else {
      // The image node owns and releases the previous render-thread texture.
      root->waterfall->setTexture(texture);
    }
    root->waterfall->setRect(0.0F, waterfall_top, width, waterfall_height);
    root->waterfall->setFiltering(
        display_mode_ == 1 ? QSGTexture::Nearest : QSGTexture::Linear);
  } else if (root->waterfall != nullptr) {
    root->waterfall->setRect(QRectF{});
  }
  return root;
}

}  // namespace cwassistant::desktop
