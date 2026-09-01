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

  QSGNode* node = item.updatePaintNode(nullptr, nullptr);
  if (node == nullptr) return 7;

  node = item.updatePaintNode(node, nullptr);
  delete node;

  cwassistant::desktop::ReplayController decoder_controller;
  cwassistant::desktop::SpectrumFrame decoder_frame{
      .bins_dbfs = QVector<float>(10, -100.0F),
      .sequence = 0,
      .timestamp_ns = 0,
      .lower_frequency_hz = 100.0,
      .upper_frequency_hz = 1'000.0,
  };
  const auto feed_decoder = [&](const bool down, const int duration_ms) {
    for (int elapsed = 0; elapsed < duration_ms; elapsed += 10) {
      decoder_frame.bins_dbfs.fill(-100.0F);
      if (down) decoder_frame.bins_dbfs[6] = -70.0F;
      decoder_frame.timestamp_ns += 10'000'000;
      ++decoder_frame.sequence;
      decoder_controller.frameReady(decoder_frame);
    }
  };
  feed_decoder(false, 100);
  feed_decoder(true, 60);
  feed_decoder(false, 200);
  const auto channels = decoder_controller.decoderChannels();
  if (channels.size() != 1 ||
      !channels.front().toMap().value(QStringLiteral("text"))
          .toString().contains(QLatin1Char('E')) ||
      channels.front().toMap().value(QStringLiteral("snrDb")).toDouble() >
          0.1) {
    return 8;
  }
  return 0;
}
