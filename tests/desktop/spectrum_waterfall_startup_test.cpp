#include <QGuiApplication>
#include <QSGNode>
#include <QTemporaryFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <numbers>
#include <span>
#include <vector>

#include "decoder/local_character_decoder.hpp"
#include "replay/decoder_channel_model.hpp"
#include "replay/replay_controller.hpp"
#include "visualization/spectrum_waterfall_item.hpp"
#include "visualization/waterfall_conditioner.hpp"

namespace {

class TestableSpectrumWaterfallItem final
    : public cwassistant::desktop::SpectrumWaterfallItem {
 public:
  using SpectrumWaterfallItem::updatePaintNode;
};

using CharacterWindow =
    cwassistant::desktop::CwCharacterFeatureWindowPtr;

std::vector<CharacterWindow> feedFrontend(
    cwassistant::desktop::LocalCharacterFrontendBank& bank,
    const std::span<const cwassistant::core::CwTrackDiagnostic> channels,
    const std::span<const double> tones_hz, const std::size_t block_count,
    std::uint64_t& sequence, std::uint64_t& timestamp_ns,
    std::uint64_t& sample_cursor) {
  constexpr double sample_rate_hz = 3'200.0;
  constexpr std::size_t samples_per_block = 3'200U;
  std::vector<CharacterWindow> windows;
  for (std::size_t block_index = 0; block_index < block_count;
       ++block_index) {
    cwassistant::core::RealtimeSampleBlock block;
    block.stream = {.kind = cwassistant::core::StreamKind::Audio,
                    .sample_rate_hz = sample_rate_hz,
                    .center_frequency_hz = 0.0,
                    .channel_count = 1};
    block.sequence = sequence++;
    block.timestamp_ns = timestamp_ns;
    block.sample_count = samples_per_block;
    for (std::size_t sample = 0; sample < samples_per_block; ++sample) {
      double value = 0.0;
      for (const double tone_hz : tones_hz) {
        value += std::sin(2.0 * std::numbers::pi * tone_hz *
                          static_cast<double>(sample_cursor + sample) /
                          sample_rate_hz);
      }
      const double scale = tones_hz.empty()
          ? 0.0 : 0.4 / static_cast<double>(tones_hz.size());
      block.samples[sample] = {
          static_cast<float>(value * scale), 0.0F};
    }
    sample_cursor += samples_per_block;
    timestamp_ns += 1'000'000'000ULL;
    auto produced = bank.process(block, channels);
    windows.insert(windows.end(),
                   std::make_move_iterator(produced.begin()),
                   std::make_move_iterator(produced.end()));
  }
  return windows;
}

bool testLocalCharacterFrontendBank() {
  std::uint64_t sequence = 1U;
  std::uint64_t timestamp_ns = 1'000'000'000ULL;
  std::uint64_t sample_cursor = 0U;
  const std::array<double, 5> tones{600.0, 700.0, 800.0, 900.0, 1'000.0};
  std::array<cwassistant::core::CwTrackDiagnostic, 5> channels{};
  channels[0] = {.id = 101, .frequency_hz = 600.0, .snr_db = 2.0F,
                 .verification_state = cwassistant::core::CwTrackState::Verified,
                 .active = true};
  channels[1] = {.id = 102, .frequency_hz = 700.0, .snr_db = 12.0F,
                 .active = true, .operator_selected = true};
  channels[2] = {.id = 103, .frequency_hz = 800.0, .snr_db = 30.0F,
                 .active = true};
  channels[3] = {.id = 104, .frequency_hz = 900.0, .snr_db = 30.0F,
                 .verification_state = cwassistant::core::CwTrackState::Verified,
                 .active = false};
  channels[4] = {.id = 105, .frequency_hz = 1'000.0, .snr_db = 5.0F,
                 .verification_state =
                     cwassistant::core::CwTrackState::MorseLikely,
                 .active = true};

  cwassistant::desktop::LocalCharacterFrontendBank bank{2U};
  if (!feedFrontend(bank, channels, tones, 9U, sequence, timestamp_ns,
                    sample_cursor).empty()) {
    return false;
  }
  bank.setEnabled(true);

  cwassistant::desktop::LocalCharacterFrontendBank ineligible_bank{2U};
  ineligible_bank.setEnabled(true);
  std::array<cwassistant::core::CwTrackDiagnostic, 2> ineligible{
      channels[2], channels[3]};
  if (!feedFrontend(ineligible_bank, ineligible, tones, 9U, sequence,
                    timestamp_ns, sample_cursor).empty()) {
    return false;
  }
  ineligible[0].operator_selected = true;
  if (!feedFrontend(ineligible_bank, ineligible, tones, 1U, sequence,
                    timestamp_ns, sample_cursor).empty()) {
    return false;
  }

  const auto first = feedFrontend(bank, channels, tones, 9U, sequence,
                                  timestamp_ns, sample_cursor);
  if (first.size() != 2U) return false;
  const auto find_track = [](const auto& windows, const std::uint64_t id) {
    return std::find_if(windows.begin(), windows.end(),
                        [id](const CharacterWindow& window) {
                          return window && window->track.track_id == id;
                        });
  };
  const auto first_verified = find_track(first, 101U);
  const auto first_selected = find_track(first, 102U);
  if (first_verified == first.end() || first_selected == first.end() ||
      find_track(first, 103U) != first.end() ||
      find_track(first, 104U) != first.end() ||
      find_track(first, 105U) != first.end()) {
    return false;
  }
  const auto first_key = (*first_verified)->track;

  auto silent_channels = channels;
  for (auto& channel : silent_channels) channel.active = false;
  const auto slow_word_gap = feedFrontend(
      bank, silent_channels, tones, 1U, sequence, timestamp_ns, sample_cursor);
  const auto gap_verified = find_track(slow_word_gap, 101U);
  if (gap_verified == slow_word_gap.end() ||
      (*gap_verified)->track != first_key) {
    return false;
  }

  cwassistant::core::RealtimeSampleBlock empty_block;
  for (std::size_t index = 0; index < 501U; ++index) {
    empty_block.sequence = sequence++;
    empty_block.timestamp_ns = timestamp_ns;
    timestamp_ns += 1'000'000ULL;
    if (!bank.process(empty_block, {}).empty()) return false;
  }
  const auto reacquired = feedFrontend(bank, channels, tones, 9U, sequence,
                                       timestamp_ns, sample_cursor);
  const auto reacquired_verified = find_track(reacquired, 101U);
  if (reacquired_verified == reacquired.end() ||
      (*reacquired_verified)->track.track_generation !=
          first_key.track_generation ||
      (*reacquired_verified)->track.frontend_generation ==
          first_key.frontend_generation) {
    return false;
  }

  bank.reset();
  const auto reset_windows = feedFrontend(bank, channels, tones, 9U, sequence,
                                          timestamp_ns, sample_cursor);
  const auto reset_verified = find_track(reset_windows, 101U);
  if (reset_verified == reset_windows.end() ||
      (*reset_verified)->track.track_generation == first_key.track_generation) {
    return false;
  }

  cwassistant::desktop::LocalCharacterFrontendBank likely_bank{1U};
  likely_bank.setEnabled(true);
  const std::array<cwassistant::core::CwTrackDiagnostic, 1> likely_track{
      channels[4]};
  const std::array<double, 1> likely_tone{1'000.0};
  const auto likely_windows = feedFrontend(
      likely_bank, likely_track, likely_tone, 9U, sequence, timestamp_ns,
      sample_cursor);
  if (likely_windows.size() != 1U || !likely_windows.front() ||
      likely_windows.front()->track.track_id != 105U) {
    return false;
  }

  cwassistant::desktop::LocalCharacterFrontendBank centered_bank{1U};
  centered_bank.setEnabled(true);
  auto centered_track = channels[2];
  centered_track.id = 106U;
  centered_track.frequency_hz = 834.0;
  centered_track.presentation_frequency_hz = 800.0;
  centered_track.verification_state =
      cwassistant::core::CwTrackState::MorseLikely;
  const std::array<cwassistant::core::CwTrackDiagnostic, 1> centered_tracks{
      centered_track};
  const std::array<double, 1> centered_tone{800.0};
  const auto centered_windows = feedFrontend(
      centered_bank, centered_tracks, centered_tone, 9U, sequence,
      timestamp_ns, sample_cursor);
  if (centered_windows.size() != 1U || !centered_windows.front()) return false;
  const auto peak = *std::max_element(centered_windows.front()->features.cbegin(),
                                      centered_windows.front()->features.cend());
  return std::isfinite(peak) && peak > -4.0F;
}

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  if (!testLocalCharacterFrontendBank()) return 21;
  cwassistant::core::OfflineCallsignDatabase callsign_database;
  const auto callsign_import =
      callsign_database.importText("EM90ZMV\nEA1EYL\n");
  QVariantMap acoustic_channel{
      {QStringLiteral("verifiedCw"), true},
      {QStringLiteral("callsign"), QString{}},
      {QStringLiteral("refinedText"), QStringLiteral("CQ DE EM?0ZMV ")},
      {QStringLiteral("text"), QString{}},
      {QStringLiteral("acousticAlternatives"),
       QVariantList{
           QVariantMap{{QStringLiteral("text"),
                       QStringLiteral("CQ DE EM90ZMV")},
                       {QStringLiteral("cost"), 1.0},
                       {QStringLiteral("confidence"), 0.68},
                       {QStringLiteral("firstObservationId"),
                        QVariant::fromValue<qulonglong>(10)},
                       {QStringLiteral("lastObservationId"),
                        QVariant::fromValue<qulonglong>(30)}},
           QVariantMap{{QStringLiteral("text"),
                        QStringLiteral("DE EM90ZMV")},
                       {QStringLiteral("cost"), 1.2},
                       {QStringLiteral("confidence"), 0.61},
                       {QStringLiteral("firstObservationId"),
                        QVariant::fromValue<qulonglong>(10)},
                       {QStringLiteral("lastObservationId"),
                        QVariant::fromValue<qulonglong>(30)}}}},
  };
  const auto offline_suggestion =
      cwassistant::desktop::offlineCallsignPresentation(
          acoustic_channel, callsign_database);
  if (!callsign_import.accepted || callsign_import.inserted_records != 2U ||
      !offline_suggestion ||
      offline_suggestion->callsign != QStringLiteral("EM90ZMV") ||
      offline_suggestion->raw_span != QStringLiteral("EM?0ZMV") ||
      offline_suggestion->agreeing_alternatives != 2) {
    return 23;
  }
  if (acoustic_channel.value(QStringLiteral("refinedText")).toString() !=
          QStringLiteral("CQ DE EM?0ZMV ") ||
      !acoustic_channel.value(QStringLiteral("callsign")).toString().isEmpty() ||
      !acoustic_channel.value(QStringLiteral("verifiedCw")).toBool()) {
    return 24;
  }
  QVariantMap ineligible_channel = acoustic_channel;
  ineligible_channel.insert(QStringLiteral("verifiedCw"), false);
  if (cwassistant::desktop::offlineCallsignPresentation(
          ineligible_channel, callsign_database)) {
    return 25;
  }
  ineligible_channel = acoustic_channel;
  ineligible_channel.insert(QStringLiteral("callsign"),
                            QStringLiteral("EM90ZMV"));
  if (cwassistant::desktop::offlineCallsignPresentation(
          ineligible_channel, callsign_database)) {
    return 26;
  }
  QVariantMap stronger_unknown = acoustic_channel;
  stronger_unknown.insert(
      QStringLiteral("acousticAlternatives"),
      QVariantList{
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EM80ZMV")},
                      {QStringLiteral("cost"), 0.0},
                      {QStringLiteral("confidence"), 0.92},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(31)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(50)}},
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EM80ZMV")},
                      {QStringLiteral("cost"), 0.1},
                      {QStringLiteral("confidence"), 0.88},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(31)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(50)}},
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EM90ZMV")},
                      {QStringLiteral("cost"), 0.4},
                      {QStringLiteral("confidence"), 0.55},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(31)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(50)}},
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EM90ZMV")},
                      {QStringLiteral("cost"), 0.5},
                      {QStringLiteral("confidence"), 0.52},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(31)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(50)}}});
  if (cwassistant::desktop::offlineCallsignPresentation(
          stronger_unknown, callsign_database)) {
    return 27;
  }
  QVariantMap old_span = acoustic_channel;
  old_span.insert(QStringLiteral("refinedText"),
                  QStringLiteral("EA?EYL EM?0ZMV "));
  old_span.insert(
      QStringLiteral("acousticAlternatives"),
      QVariantList{
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EA1EYL")},
                      {QStringLiteral("cost"), 0.0},
                      {QStringLiteral("confidence"), 0.9},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(51)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(60)}},
          QVariantMap{{QStringLiteral("text"), QStringLiteral("EA1EYL")},
                      {QStringLiteral("cost"), 0.1},
                      {QStringLiteral("confidence"), 0.85},
                      {QStringLiteral("firstObservationId"),
                       QVariant::fromValue<qulonglong>(51)},
                      {QStringLiteral("lastObservationId"),
                       QVariant::fromValue<qulonglong>(60)}}});
  if (cwassistant::desktop::offlineCallsignPresentation(
          old_span, callsign_database)) {
    return 29;
  }
  acoustic_channel.insert(QStringLiteral("acousticAlternatives"),
                          QVariantList{});
  if (cwassistant::desktop::offlineCallsignPresentation(
          acoustic_channel, callsign_database)) {
    return 28;
  }
  QTemporaryFile local_callsign_file;
  if (!local_callsign_file.open() ||
      local_callsign_file.write("EM90ZMV\nEA1EYL\n") <= 0 ||
      !local_callsign_file.flush()) {
    return 30;
  }
  cwassistant::desktop::ReplayController controller;
  controller.configureOfflineCallsignDatabase(
      true, local_callsign_file.fileName());
  if (controller.offlineCallsignDatabaseState() != QStringLiteral("ready") ||
      controller.offlineCallsignDatabaseEntries() != 2) {
    return 31;
  }
  controller.configureOfflineCallsignDatabase(false, QString{});
  if (controller.offlineCallsignDatabaseState() !=
          QStringLiteral("disabled") ||
      controller.offlineCallsignDatabaseEntries() != 0) {
    return 32;
  }
  using cwassistant::desktop::freshCharacterRefinementCallEvidence;
  if (freshCharacterRefinementCallEvidence("NOISE", 0U) ||
      !freshCharacterRefinementCallEvidence("4X5L", 0U) ||
      freshCharacterRefinementCallEvidence("4X5LL ", 5U) ||
      freshCharacterRefinementCallEvidence("4X5LL", 4U) ||
      !freshCharacterRefinementCallEvidence("4X5L", 3U) ||
      freshCharacterRefinementCallEvidence("4X5LL HEL", 6U) ||
      freshCharacterRefinementCallEvidence("CQ 4X5LL A", 8U) ||
      !freshCharacterRefinementCallEvidence("4X5LL 4X5L", 6U) ||
      freshCharacterRefinementCallEvidence("4X5LL", 5U)) {
    return 22;
  }
  cwassistant::core::CwChannelSnapshot selected_snapshot{
      .id = 5,
      .frequency_hz = 850.0,
      .presentation_frequency_hz = 850.0,
      .verified_cw = false,
      .operator_selected = true,
      .characters = {{.symbol = "A", .confidence = 0.8F,
                      .timing_quality = 0.8F, .known = true}},
      .text = "UNVERIFIED",
      .refined_text = "EA1EYL ",
      .provisional_text = "NO",
      .pending_elements = ".-",
      .callsign = "IU0LFQ",
  };
  QVariantMap selected_model =
      cwassistant::desktop::decoderChannelModel(
          std::span<const cwassistant::core::CwChannelSnapshot>{
              &selected_snapshot, 1})
          .front().toMap();
  if (!selected_model.value(QStringLiteral("operatorSelected")).toBool() ||
      selected_model.value(QStringLiteral("verifiedCw")).toBool() ||
      !selected_model.value(QStringLiteral("text")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("refinedText")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("acousticAlternatives")).toList().isEmpty() ||
      !selected_model.value(QStringLiteral("provisionalText")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("elements")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("callsign")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("localModelText")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("localModelCallsign")).toString().isEmpty() ||
      selected_model.value(QStringLiteral("localModelState")).toString() !=
          QStringLiteral("unavailable") ||
      !selected_model.value(QStringLiteral("characterEvidence")).toList().isEmpty() ||
      selected_model.value(QStringLiteral("color")).toString() !=
          QStringLiteral("#8d9aaa")) {
    return 18;
  }
  const cwassistant::desktop::LocalDecoderChannelPresentation local_decoder{
      .channel_id = 5,
      .state = cwassistant::desktop::LocalDecoderPresentationState::Ready,
      .stable_text = QStringLiteral("CQ TEST "),
      .status = QStringLiteral("Stable local transcript"),
  };
  selected_model =
      cwassistant::desktop::decoderChannelModel(
          std::span<const cwassistant::core::CwChannelSnapshot>{
              &selected_snapshot, 1},
          std::span<const cwassistant::desktop::LocalDecoderChannelPresentation>{
              &local_decoder, 1})
          .front().toMap();
  if (!selected_model.value(QStringLiteral("localModelText")).toString().isEmpty() ||
      !selected_model.value(QStringLiteral("localModelCallsign")).toString().isEmpty() ||
      selected_model.value(QStringLiteral("localModelState")).toString() !=
          QStringLiteral("ready")) {
    return 20;
  }
  selected_snapshot.verified_cw = true;
  selected_model =
      cwassistant::desktop::decoderChannelModel(
          std::span<const cwassistant::core::CwChannelSnapshot>{
              &selected_snapshot, 1},
          std::span<const cwassistant::desktop::LocalDecoderChannelPresentation>{
              &local_decoder, 1})
          .front().toMap();
  if (selected_model.value(QStringLiteral("text")).toString() !=
          QStringLiteral("UNVERIFIED") ||
      selected_model.value(QStringLiteral("refinedText")).toString() !=
          QStringLiteral("EA1EYL ") ||
      selected_model.value(QStringLiteral("callsign")).toString() !=
          QStringLiteral("IU0LFQ") ||
      selected_model.value(QStringLiteral("localModelText")).toString() !=
          QStringLiteral("CQ TEST ") ||
      selected_model.value(QStringLiteral("localModelState")).toString() !=
          QStringLiteral("ready") ||
      selected_model.value(QStringLiteral("localModelStatus")).toString() !=
          QStringLiteral("Stable local transcript") ||
      selected_model.value(QStringLiteral("characterEvidence")).toList().size() != 1 ||
      selected_model.value(QStringLiteral("color")).toString() ==
          QStringLiteral("#8d9aaa")) {
    return 19;
  }
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
  const QList<qulonglong> dismissed =
      cwassistant::desktop::reconcileDecoderSessionOrder(
          {}, QVariantList{previous_session}, QVariantList{reacquired_channel});
  if (!dismissed.isEmpty()) return 17;

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
