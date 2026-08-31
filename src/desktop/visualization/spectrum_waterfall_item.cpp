#include "spectrum_waterfall_item.hpp"

#include <QColor>
#include <QImage>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <algorithm>
#include <cmath>
#include <limits>

#include "../replay/replay_controller.hpp"

namespace cwassistant::desktop {
namespace {

constexpr std::size_t kMaximumWaterfallRows = 512;

class DisplayNode final : public QSGNode {
 public:
  DisplayNode() {
    waterfall = new QSGSimpleTextureNode;
    appendChildNode(waterfall);

    grid = makeGeometryNode(QSGGeometry::DrawLines, QColor("#2a3a49"));
    appendChildNode(grid);
    spectrum = makeGeometryNode(QSGGeometry::DrawLineStrip,
                                QColor("#64e6d2"));
    appendChildNode(spectrum);
  }

  ~DisplayNode() override { delete texture; }

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

  QSGSimpleTextureNode* waterfall{nullptr};
  QSGGeometryNode* grid{nullptr};
  QSGGeometryNode* spectrum{nullptr};
  QSGTexture* texture{nullptr};
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
    effective_lower_bound_db_ = lower_bound_db_;
    effective_upper_bound_db_ = upper_bound_db_;
    emit rangeChanged();
  } else if (!latest_bins_.isEmpty()) {
    updateAutomaticRange(latest_bins_);
  }
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
  emit displayChanged();
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

void SpectrumWaterfallItem::acceptFrame(const SpectrumFrame& frame) {
  if (frame.bins_dbfs.isEmpty()) return;
  latest_bins_ = frame.bins_dbfs;
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
  if (automatic_range_) updateAutomaticRange(latest_bins_);

  const std::uint64_t row_interval =
      1'000'000'000ULL / static_cast<std::uint64_t>(waterfall_rate_);
  if (last_row_timestamp_ns_ == 0 ||
      frame.timestamp_ns >= last_row_timestamp_ns_ + row_interval) {
    waterfall_rows_.push_front(latest_bins_);
    if (waterfall_rows_.size() > kMaximumWaterfallRows) {
      waterfall_rows_.pop_back();
    }
    last_row_timestamp_ns_ = frame.timestamp_ns;
  }
  scheduleRender();
}

void SpectrumWaterfallItem::resetFrames() {
  latest_bins_.clear();
  waterfall_rows_.clear();
  last_row_timestamp_ns_ = 0;
  last_sequence_ = 0;
  has_sequence_ = false;
  dropped_rows_ = 0;
  lower_frequency_hz_ = 0.0;
  upper_frequency_hz_ = 0.0;
  emit droppedRowsChanged();
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
  const qsizetype low_index = static_cast<qsizetype>(finite.size() / 10);
  const qsizetype high_index = static_cast<qsizetype>(
      (static_cast<quint64>(finite.size() - 1) * 99ULL) / 100ULL);
  double low = std::clamp(static_cast<double>(finite[low_index]) - 8.0,
                          -200.0, 20.0);
  double high = std::clamp(static_cast<double>(finite[high_index]) + 3.0,
                           -190.0, 50.0);
  if (high - low < 30.0) low = high - 30.0;
  constexpr double smoothing = 0.16;
  const double next_low = effective_lower_bound_db_ * (1.0 - smoothing) +
                          low * smoothing;
  const double next_high = effective_upper_bound_db_ * (1.0 - smoothing) +
                           high * smoothing;
  if (std::abs(next_low - effective_lower_bound_db_) > 0.02 ||
      std::abs(next_high - effective_upper_bound_db_) > 0.02) {
    effective_lower_bound_db_ = next_low;
    effective_upper_bound_db_ = next_high;
    emit rangeChanged();
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

  root->waterfall->setTexture(nullptr);
  delete root->texture;
  root->texture = nullptr;
  if (!waterfall_rows_.empty() && window() != nullptr) {
    const int image_width = waterfall_rows_.front().size();
    const int image_height = static_cast<int>(waterfall_rows_.size());
    QImage image(image_width, image_height, QImage::Format_RGB32);
    for (int y = 0; y < image_height; ++y) {
      const auto& row = waterfall_rows_[static_cast<std::size_t>(y)];
      auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image_width; ++x) {
        const float normalized = static_cast<float>(std::clamp(
            (static_cast<double>(row[x]) - effective_lower_bound_db_) / span,
            0.0, 1.0));
        scanline[x] = waterfallColor(normalized);
      }
    }
    root->texture = window()->createTextureFromImage(image);
    root->waterfall->setTexture(root->texture);
    root->waterfall->setRect(0.0F, waterfall_top, width, waterfall_height);
    root->waterfall->setFiltering(QSGTexture::Linear);
  } else {
    root->waterfall->setRect(QRectF{});
  }
  return root;
}

}  // namespace cwassistant::desktop
