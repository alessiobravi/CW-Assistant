#include <QGuiApplication>
#include <QSGNode>

#include <cmath>

#include "visualization/spectrum_waterfall_item.hpp"
#include "visualization/waterfall_conditioner.hpp"
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
  const QVariantMap previous_session{
      {QStringLiteral("id"), QVariant::fromValue<qulonglong>(7)},
      {QStringLiteral("color"), QStringLiteral("#4dd0e1")},
      {QStringLiteral("audioFrequencyHz"), 700.0},
      {QStringLiteral("presentationFrequencyHz"), 700.0},
  };
  const QVariantMap reacquired_channel{
      {QStringLiteral("id"), QVariant::fromValue<qulonglong>(19)},
      {QStringLiteral("color"), QStringLiteral("#4dd0e1")},
      {QStringLiteral("audioFrequencyHz"), 742.0},
      {QStringLiteral("presentationFrequencyHz"), 706.0},
  };
  const QList<qulonglong> reconciled =
      cwassistant::desktop::reconcileDecoderSessionOrder(
          QList<qulonglong>{7}, QVariantList{previous_session},
          QVariantList{reacquired_channel});
  if (reconciled != QList<qulonglong>{19}) return 15;

  cwassistant::desktop::WaterfallConditioner conditioner;
  QVector<float> shaped_noise(128);
  for (qsizetype index = 0; index < shaped_noise.size(); ++index)
    shaped_noise[index] = -92.0F + 0.08F * static_cast<float>(index);
  static_cast<void>(conditioner.process(
      shaped_noise, true, 6.0, -110.0, -40.0, 3.0, -90.0));
  QVector<float> keyed = shaped_noise;
  for (qsizetype index = 62; index <= 66; ++index)
    keyed[index] += 24.0F;
  const QVector<float> isolated = conditioner.process(
      keyed, true, 6.0, -110.0, -40.0, 3.0, -90.0);
  int bright_bins = 0;
  for (const float value : isolated) {
    if (value > -75.0F) ++bright_bins;
  }
  if (bright_bins < 5 || bright_bins > 9 || isolated[64] < -60.0F ||
      isolated[20] > -100.0F) {
    return 11;
  }
  QVariantMap keyed_channel{
      {QStringLiteral("verifiedCw"), true},
      {QStringLiteral("active"), true},
      {QStringLiteral("keyDown"), true},
      {QStringLiteral("frequencyHz"), 700.0},
  };
  const QVector<float> symbol_row = cwassistant::desktop::cwSymbolRow(
      QVariantList{keyed_channel}, 101, 200.0, 1'200.0, -110.0, -40.0);
  int symbol_bins = 0;
  for (const float value : symbol_row) {
    if (value > -50.0F) ++symbol_bins;
  }
  if (symbol_bins != 3 || symbol_row[50] < -50.0F ||
      symbol_row[20] > -100.0F) {
    return 13;
  }
  keyed_channel.insert(QStringLiteral("keyDown"), false);
  const QVector<float> gap_row = cwassistant::desktop::cwSymbolRow(
      QVariantList{keyed_channel}, 101, 200.0, 1'200.0, -110.0, -40.0);
  if (std::any_of(gap_row.cbegin(), gap_row.cend(),
                  [](const float value) { return value > -100.0F; })) {
    return 14;
  }
  keyed_channel.insert(QStringLiteral("keyDown"), true);
  keyed_channel.insert(QStringLiteral("active"), false);
  const QVector<float> retained_noise_row =
      cwassistant::desktop::cwSymbolRow(
          QVariantList{keyed_channel}, 101, 200.0, 1'200.0, -110.0, -40.0);
  if (std::any_of(retained_noise_row.cbegin(), retained_noise_row.cend(),
                  [](const float value) { return value > -100.0F; })) {
    return 16;
  }
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
