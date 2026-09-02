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
    const bool expose_verified_content = channel.verified_cw;
    item.insert(QStringLiteral("id"),
                QVariant::fromValue<qulonglong>(channel.id));
    item.insert(QStringLiteral("frequencyHz"), channel.frequency_hz);
    item.insert(QStringLiteral("presentationFrequencyHz"),
                channel.presentation_frequency_hz);
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
    item.insert(QStringLiteral("operatorSelected"),
                channel.operator_selected);
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
      if (!expose_verified_content) break;
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
                expose_verified_content
                    ? QString::fromStdString(channel.text) : QString{});
    item.insert(QStringLiteral("refinedText"),
                expose_verified_content
                    ? QString::fromStdString(channel.refined_text)
                    : QString{});
    QVariantList acoustic_alternatives;
    if (expose_verified_content) {
      acoustic_alternatives.reserve(static_cast<qsizetype>(
          channel.acoustic_alternatives.size()));
      for (const auto& alternative : channel.acoustic_alternatives) {
        QVariantMap candidate;
        candidate.insert(QStringLiteral("text"),
                         QString::fromStdString(alternative.text));
        candidate.insert(QStringLiteral("elements"),
                         QString::fromStdString(
                             alternative.provisional_elements));
        candidate.insert(QStringLiteral("wpm"), alternative.wpm);
        candidate.insert(QStringLiteral("cost"),
                         alternative.acoustic_cost);
        candidate.insert(QStringLiteral("confidence"),
                         alternative.evidence_confidence);
        acoustic_alternatives.push_back(candidate);
      }
    }
    item.insert(QStringLiteral("acousticAlternatives"),
                acoustic_alternatives);
    item.insert(QStringLiteral("provisionalText"),
                expose_verified_content
                    ? QString::fromStdString(channel.provisional_text)
                    : QString{});
    item.insert(QStringLiteral("elements"),
                expose_verified_content
                    ? QString::fromStdString(channel.pending_elements)
                    : QString{});
    item.insert(QStringLiteral("callsign"),
                expose_verified_content
                    ? QString::fromStdString(channel.callsign) : QString{});
    item.insert(QStringLiteral("color"),
                expose_verified_content
                    ? QString::fromLatin1(kChannelColors[
                          channel.color_index % kChannelColors.size()])
                    : QStringLiteral("#8d9aaa"));
    model.push_back(item);
  }
  return model;
}

QVariantMap verificationDiagnosticsModel(
    const cwassistant::core::CwVerificationDiagnostics& diagnostics) {
  QVariantMap model;
  model.insert(QStringLiteral("candidateTracks"),
               static_cast<qulonglong>(diagnostics.candidate_tracks));
  model.insert(QStringLiteral("morseLikelyTracks"),
               static_cast<qulonglong>(diagnostics.morse_likely_tracks));
  model.insert(QStringLiteral("verifiedTracks"),
               static_cast<qulonglong>(diagnostics.verified_tracks));
  model.insert(QStringLiteral("verifiedTransitions"),
               static_cast<qulonglong>(diagnostics.verified_transitions));
  model.insert(QStringLiteral("expiredUnverifiedTracks"),
               static_cast<qulonglong>(diagnostics.expired_unverified_tracks));
  model.insert(QStringLiteral("maxDecodedSymbols"),
               diagnostics.maximum_decoded_symbols);
  model.insert(QStringLiteral("maxKeyTransitions"),
               diagnostics.maximum_key_transitions);
  model.insert(QStringLiteral("bestTimingQuality"),
               diagnostics.best_timing_quality);
  model.insert(QStringLiteral("bestCadenceQuality"),
               diagnostics.best_cadence_quality);
  model.insert(QStringLiteral("bestNarrowbandCoherence"),
               diagnostics.best_narrowband_coherence);
  QVariantMap reason_counts;
  for (std::size_t reason = 0; reason < diagnostics.current_reason_counts.size();
       ++reason) {
    const auto count = diagnostics.current_reason_counts[reason];
    if (count == 0) {
      continue;
    }
    reason_counts.insert(
        QString::fromLatin1(cwassistant::core::cwVerificationReasonName(
            static_cast<cwassistant::core::CwVerificationReason>(reason))),
        static_cast<qulonglong>(count));
  }
  model.insert(QStringLiteral("reasonCounts"), reason_counts);
  return model;
}

}  // namespace cwassistant::desktop
