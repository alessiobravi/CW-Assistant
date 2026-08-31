#include <QGuiApplication>
#include <QSGNode>

#include <cmath>

#include "visualization/spectrum_waterfall_item.hpp"

namespace {

class TestableSpectrumWaterfallItem final
    : public cwassistant::desktop::SpectrumWaterfallItem {
 public:
  using SpectrumWaterfallItem::updatePaintNode;
};

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  TestableSpectrumWaterfallItem item;
  item.setWidth(960.0);
  item.setHeight(540.0);
  item.setAutomaticRangeSpanDb(60.0);
  item.setNoiseSuppression(true);
  item.setNoiseMarginDb(6.0);

  cwassistant::desktop::SpectrumFrame noise_frame{
      .bins_dbfs = QVector<float>(128, -90.0F),
      .sequence = 1,
      .timestamp_ns = 1'000'000'000,
      .lower_frequency_hz = 100.0,
      .upper_frequency_hz = 3'000.0,
  };
  item.acceptFrame(noise_frame);
  if (item.effectiveUpperBoundDb() - item.effectiveLowerBoundDb() < 59.9 ||
      std::abs(item.estimatedNoiseFloorDb() + 90.0) > 0.1) {
    return 2;
  }

  const double first_ceiling = item.effectiveUpperBoundDb();
  noise_frame.bins_dbfs.fill(-80.0F);
  noise_frame.sequence = 2;
  noise_frame.timestamp_ns = 2'000'000'000;
  item.acceptFrame(noise_frame);
  if (item.effectiveUpperBoundDb() - first_ceiling > 3.0) {
    return 3;
  }

  QSGNode* node = item.updatePaintNode(nullptr, nullptr);
  if (node == nullptr) return 4;

  node = item.updatePaintNode(node, nullptr);
  delete node;
  return 0;
}
