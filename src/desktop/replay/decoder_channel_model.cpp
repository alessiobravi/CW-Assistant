#include "decoder_channel_model.hpp"

#include <QVariantMap>

#include <array>

namespace cwassistant::desktop {
namespace {

constexpr std::array<const char*, 24> kChannelColors{
    "#4dd0e1", "#ffb74d", "#ba68c8", "#81c784",
    "#ff6b8a", "#64b5f6", "#dce775", "#f06292",
    "#4db6ac", "#9575cd", "#ffd54f", "#90a4ae",
    "#ff8a65", "#a1887f", "#7986cb", "#aed581",
    "#4fc3f7", "#e57373", "#fff176", "#ce93d8",
    "#80cbc4", "#ffcc80", "#9fa8da", "#b0bec5",
};

}  // namespace

QVariantList decoderChannelModel(
    const std::span<const cwassistant::core::CwChannelSnapshot> channels) {
  QVariantList model;
  model.reserve(static_cast<qsizetype>(channels.size()));
  for (const auto& channel : channels) {
    QVariantMap item;
    item.insert(QStringLiteral("id"),
                QVariant::fromValue<qulonglong>(channel.id));
    item.insert(QStringLiteral("frequencyHz"), channel.frequency_hz);
    item.insert(QStringLiteral("driftHzPerSecond"),
                channel.drift_hz_per_second);
    item.insert(QStringLiteral("filterWidthHz"), channel.filter_width_hz);
    item.insert(QStringLiteral("snrDb"), channel.snr_db);
    item.insert(QStringLiteral("wpm"), channel.wpm);
    item.insert(QStringLiteral("confidence"), channel.confidence);
    item.insert(QStringLiteral("keyProbability"),
                channel.key_down_probability);
    item.insert(QStringLiteral("keyDown"), channel.key_down);
    item.insert(QStringLiteral("active"), channel.active);
    item.insert(QStringLiteral("verifiedCw"), channel.verified_cw);
    item.insert(QStringLiteral("verificationState"), QString::fromLatin1(
        cwassistant::core::cwTrackStateName(channel.verification_state)));
    item.insert(QStringLiteral("verificationReason"), QString::fromLatin1(
        cwassistant::core::cwVerificationReasonName(
            channel.verification_reason)));
    item.insert(QStringLiteral("verificationConfidence"),
                channel.verification_confidence);
    item.insert(QStringLiteral("verificationCadenceQuality"),
                channel.verification_cadence_quality);
    item.insert(QStringLiteral("verificationTimingQuality"),
                channel.verification_timing_quality);
    item.insert(QStringLiteral("verificationCharacterConfidence"),
                channel.verification_character_confidence);
    item.insert(QStringLiteral("cadenceQuality"), channel.cadence_quality);
    item.insert(QStringLiteral("meanCharacterConfidence"),
                channel.mean_character_confidence);
    item.insert(QStringLiteral("narrowbandCoherence"),
                channel.narrowband_coherence);
    item.insert(QStringLiteral("keyTransitions"),
                QVariant::fromValue<qulonglong>(channel.key_transitions));
    QVariantList character_evidence;
    character_evidence.reserve(
        static_cast<qsizetype>(channel.characters.size()));
    for (const auto& character : channel.characters) {
      QVariantMap evidence;
      evidence.insert(QStringLiteral("symbol"),
                      QString::fromStdString(character.symbol));
      evidence.insert(QStringLiteral("confidence"), character.confidence);
      evidence.insert(QStringLiteral("timingQuality"),
                      character.timing_quality);
      evidence.insert(QStringLiteral("known"), character.known);
      character_evidence.push_back(evidence);
    }
    item.insert(QStringLiteral("characterEvidence"), character_evidence);
    item.insert(QStringLiteral("text"),
                QString::fromStdString(channel.text));
    item.insert(QStringLiteral("provisionalText"),
                QString::fromStdString(channel.provisional_text));
    item.insert(QStringLiteral("elements"),
                QString::fromStdString(channel.pending_elements));
    item.insert(QStringLiteral("callsign"),
                QString::fromStdString(channel.callsign));
    item.insert(QStringLiteral("color"), QString::fromLatin1(
        kChannelColors[channel.color_index % kChannelColors.size()]));
    model.push_back(item);
  }
  return model;
}

}  // namespace cwassistant::desktop
