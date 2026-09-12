#include "decoder_channel_model.hpp"

#include <QVariantMap>

#include <algorithm>
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

QString localDecoderStateName(const LocalDecoderPresentationState state) {
  switch (state) {
    case LocalDecoderPresentationState::Unavailable:
      return QStringLiteral("unavailable");
    case LocalDecoderPresentationState::Disabled:
      return QStringLiteral("disabled");
    case LocalDecoderPresentationState::Loading:
      return QStringLiteral("loading");
    case LocalDecoderPresentationState::Ready:
      return QStringLiteral("ready");
    case LocalDecoderPresentationState::Error:
      return QStringLiteral("error");
  }
  return QStringLiteral("unavailable");
}

QString localDecoderDefaultStatus(const LocalDecoderPresentationState state) {
  switch (state) {
    case LocalDecoderPresentationState::Unavailable:
      return QStringLiteral("Unavailable in this build");
    case LocalDecoderPresentationState::Disabled:
      return QStringLiteral("Disabled");
    case LocalDecoderPresentationState::Loading:
      return QStringLiteral("Loading local decoder");
    case LocalDecoderPresentationState::Ready:
      return QStringLiteral("Listening for stable text");
    case LocalDecoderPresentationState::Error:
      return QStringLiteral("Local decoder error");
  }
  return QStringLiteral("Unavailable in this build");
}

}  // namespace

namespace {

// Rounds a continuously varying measurement to what an operator can read.
//
// The session list compares each row with the one it already holds and only
// tells the view about rows that differ. Signal-to-noise, speed and the
// confidences move a little on every single update, so every row differed
// every time, every row was reported changed, and the view re-evaluated an
// entire decoded card -- transcript text layout included -- for each of them.
// With several signals decoding that is hundreds of full card rebuilds a
// second on the one thread that draws, which is felt as the application
// becoming jerky while the processor is plainly not busy.
//
// A tenth of a decibel or of a word per minute is already below what the card
// displays, so rounding costs the operator nothing and lets a row that has not
// meaningfully changed compare equal and stay quiet.
[[nodiscard]] double readable(const double value, const double step = 0.1) {
  return std::isfinite(value) ? std::round(value / step) * step : value;
}

}  // namespace

QVariantList decoderChannelModel(
    const std::span<const cwassistant::core::CwChannelSnapshot> channels,
    const std::span<const LocalDecoderChannelPresentation> local_decoder) {
  QVariantList model;
  model.reserve(static_cast<qsizetype>(channels.size()));
  for (const auto& channel : channels) {
    QVariantMap item;
    const bool expose_verified_content = channel.verified_cw;
    item.insert(QStringLiteral("id"),
                QVariant::fromValue<qulonglong>(channel.id));
    item.insert(QStringLiteral("frequencyHz"), readable(channel.frequency_hz));
    item.insert(QStringLiteral("presentationFrequencyHz"),
                readable(channel.presentation_frequency_hz));
    item.insert(QStringLiteral("driftHzPerSecond"),
                readable(channel.drift_hz_per_second));
    item.insert(QStringLiteral("filterWidthHz"),
                readable(channel.filter_width_hz, 1.0));
    item.insert(QStringLiteral("snrDb"), readable(channel.snr_db));
    item.insert(QStringLiteral("wpm"), readable(channel.wpm));
    item.insert(QStringLiteral("confidence"),
                readable(channel.confidence, 0.01));
    item.insert(QStringLiteral("keyProbability"),
                readable(channel.key_down_probability, 0.01));
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
                readable(channel.verification_confidence, 0.01));
    item.insert(QStringLiteral("verificationCadenceQuality"),
                readable(channel.verification_cadence_quality, 0.01));
    item.insert(QStringLiteral("verificationTimingQuality"),
                readable(channel.verification_timing_quality, 0.01));
    item.insert(QStringLiteral("verificationCharacterConfidence"),
                readable(channel.verification_character_confidence, 0.01));
    item.insert(QStringLiteral("cadenceQuality"),
                readable(channel.cadence_quality, 0.01));
    item.insert(QStringLiteral("meanCharacterConfidence"),
                readable(channel.mean_character_confidence, 0.01));
    item.insert(QStringLiteral("narrowbandCoherence"),
                readable(channel.narrowband_coherence, 0.01));
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
      evidence.insert(QStringLiteral("confidence"),
                      readable(character.confidence, 0.01));
      evidence.insert(QStringLiteral("timingQuality"),
                      readable(character.timing_quality, 0.01));
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
        candidate.insert(QStringLiteral("wpm"), readable(alternative.wpm));
        candidate.insert(QStringLiteral("cost"),
                         readable(alternative.acoustic_cost));
        candidate.insert(QStringLiteral("confidence"),
                         readable(alternative.evidence_confidence, 0.01));
        candidate.insert(QStringLiteral("firstObservationId"),
                         QVariant::fromValue<qulonglong>(
                             alternative.first_observation_id));
        candidate.insert(QStringLiteral("lastObservationId"),
                         QVariant::fromValue<qulonglong>(
                             alternative.last_observation_id));
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
    QVariantList transmissions;
    QVariantList sender_cadences;
    if (expose_verified_content) {
      transmissions.reserve(static_cast<qsizetype>(
          channel.transmissions.size()));
      for (const auto& transmission : channel.transmissions) {
        QVariantMap turn;
        turn.insert(QStringLiteral("sequence"),
                    QVariant::fromValue<qulonglong>(transmission.sequence));
        turn.insert(QStringLiteral("text"),
                    QString::fromStdString(transmission.text));
        turn.insert(QStringLiteral("sender"),
                    QString::fromStdString(transmission.sender_callsign));
        turn.insert(QStringLiteral("wpm"), readable(transmission.wpm));
        turn.insert(QStringLiteral("cadenceConfidence"),
                    readable(transmission.cadence_confidence, 0.01));
        transmissions.push_back(turn);
      }
      sender_cadences.reserve(static_cast<qsizetype>(
          channel.sender_cadences.size()));
      for (const auto& cadence : channel.sender_cadences) {
        QVariantMap item_cadence;
        item_cadence.insert(QStringLiteral("callsign"),
                            QString::fromStdString(cadence.callsign));
        item_cadence.insert(QStringLiteral("wpm"), readable(cadence.wpm));
        item_cadence.insert(QStringLiteral("confidence"),
                            readable(cadence.confidence, 0.01));
        item_cadence.insert(QStringLiteral("turns"), cadence.observed_turns);
        sender_cadences.push_back(item_cadence);
      }
    }
    item.insert(QStringLiteral("transmissions"), transmissions);
    item.insert(QStringLiteral("senderCadences"), sender_cadences);
    item.insert(QStringLiteral("activeTransmissionSequence"),
                QVariant::fromValue<qulonglong>(
                    channel.active_transmission_sequence));
    item.insert(QStringLiteral("currentSenderCallsign"),
                expose_verified_content
                    ? QString::fromStdString(channel.current_sender_callsign)
                    : QString{});
    item.insert(QStringLiteral("currentSenderWpm"),
                expose_verified_content ? readable(channel.current_sender_wpm)
                                        : 0.0);
    item.insert(QStringLiteral("contextualText"),
                expose_verified_content
                    ? QString::fromStdString(channel.contextual_text)
                    : QString{});
    item.insert(QStringLiteral("callsign"),
                expose_verified_content
                    ? QString::fromStdString(channel.callsign) : QString{});
    QVariantList qso_participants;
    if (expose_verified_content) {
      qso_participants.reserve(
          static_cast<qsizetype>(channel.qso_participants.size()));
      for (const auto& participant : channel.qso_participants)
        qso_participants.push_back(QString::fromStdString(participant));
    }
    item.insert(QStringLiteral("qsoParticipants"), qso_participants);
    const auto local = std::find_if(
        local_decoder.begin(), local_decoder.end(),
        [&channel](const LocalDecoderChannelPresentation& candidate) {
          return candidate.channel_id == channel.id;
        });
    const auto local_state = local == local_decoder.end()
        ? LocalDecoderPresentationState::Unavailable : local->state;
    item.insert(QStringLiteral("localModelState"),
                localDecoderStateName(local_state));
    item.insert(QStringLiteral("localModelStatus"),
                local != local_decoder.end() && !local->status.isEmpty()
                    ? local->status : localDecoderDefaultStatus(local_state));
    item.insert(QStringLiteral("localModelText"),
                expose_verified_content && local != local_decoder.end()
                    ? local->stable_text : QString{});
    item.insert(QStringLiteral("localModelCallsign"), QString{});
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
