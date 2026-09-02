#include <QGuiApplication>
#include <QSGNode>

#include <cmath>

#include "visualization/spectrum_waterfall_item.hpp"
#include "replay/replay_controller.hpp"

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
  item.setWaterfallRate(30);
  item.setWaterfallTimeSpanSeconds(15);
  if (item.waterfallRowCapacity() != 450 ||
      item.waterfallTimeSpanSeconds() != 15) {
    return 2;
  }

  cwassistant::desktop::SpectrumFrame noise_frame{
      .bins_dbfs = QVector<float>(128, -90.0F),
      .sequence = 1,
      .timestamp_ns = 1'000'000'000,
      .lower_frequency_hz = 100.0,
      .upper_frequency_hz = 3'000.0,
      .instantaneous_bins_dbfs = QVector<float>(128, -95.0F),
  };
  item.acceptFrame(noise_frame);
  if (item.effectiveUpperBoundDb() - item.effectiveLowerBoundDb() < 59.9 ||
      std::abs(item.estimatedNoiseFloorDb() + 90.0) > 0.1) {
    return 3;
  }
  if (item.storedWaterfallRows() != 1) return 4;

  const double first_ceiling = item.effectiveUpperBoundDb();
  noise_frame.bins_dbfs.fill(-80.0F);
  noise_frame.sequence = 2;
  noise_frame.timestamp_ns = 2'000'000'000;
  item.acceptFrame(noise_frame);
  if (item.effectiveUpperBoundDb() - first_ceiling > 3.0) {
    return 5;
  }
  if (item.storedWaterfallRows() != 31) return 6;

  item.setDisplayMode(1);
  if (item.displayMode() != 1 || item.storedWaterfallRows() != 0) return 8;
  noise_frame.sequence = 3;
  noise_frame.timestamp_ns = 2'100'000'000;
  noise_frame.instantaneous_bins_dbfs.fill(-70.0F);
  item.acceptFrame(noise_frame);
  if (item.storedWaterfallRows() != 1) return 9;
  item.setDisplayMode(99);
  if (item.displayMode() != 1) return 10;

  QSGNode* node = item.updatePaintNode(nullptr, nullptr);
  if (node == nullptr) return 7;

  node = item.updatePaintNode(node, nullptr);
  delete node;

  return 0;
}
