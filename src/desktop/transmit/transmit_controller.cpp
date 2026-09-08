#include "transmit_controller.hpp"

#include <QRegularExpression>
#include <QHash>
#include <QVariantMap>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cwassistant::desktop {
namespace {

int wildcardEditDistance(const QString& left, const QString& right,
                         const int limit) {
  if (std::abs(left.size() - right.size()) > limit) return limit + 1;
  QVector<int> previous(right.size() + 1);
  QVector<int> current(right.size() + 1);
  for (qsizetype column = 0; column <= right.size(); ++column)
    previous[column] = static_cast<int>(column);
  for (qsizetype row = 1; row <= left.size(); ++row) {
    current[0] = static_cast<int>(row);
    int row_minimum = current[0];
    for (qsizetype column = 1; column <= right.size(); ++column) {
      const QChar a = left.at(row - 1);
      const QChar b = right.at(column - 1);
      const int substitution = (a == b || a == u'?' || b == u'?') ? 0 : 1;
      current[column] = std::min({previous[column] + 1,
                                  current[column - 1] + 1,
                                  previous[column - 1] + substitution});
      row_minimum = std::min(row_minimum, current[column]);
    }
    if (row_minimum > limit) return limit + 1;
    previous.swap(current);
  }
  return previous.back();
}

bool resemblesCallsign(const QString& token) {
  if (token.size() < 3 || token.size() > 16 || !token.contains(
          QRegularExpression(QStringLiteral("[0-9?]")))) return false;
  return token.contains(QRegularExpression(QStringLiteral("[A-Z?]")));
}

}  // namespace

TransmitController::TransmitController(QObject* parent)
    : QObject(parent), guard_(callsign_policy_) {}

QString TransmitController::state() const {
  return QString::fromLatin1(guard_.state_name().data(),
                             static_cast<qsizetype>(guard_.state_name().size()));
}
const QString& TransmitController::status() const noexcept { return status_; }
bool TransmitController::armed() const noexcept {
  return guard_.state() != cwassistant::core::TransmitState::Disarmed &&
         guard_.state() != cwassistant::core::TransmitState::Fault;
}
bool TransmitController::qsoConfirmed() const noexcept {
  return guard_.state() == cwassistant::core::TransmitState::Confirmed ||
         guard_.state() == cwassistant::core::TransmitState::Transmitting;
}
QString TransmitController::targetCallsign() const {
  return QString::fromLatin1(guard_.pending_callsign().data(),
                             static_cast<qsizetype>(
                                 guard_.pending_callsign().size()));
}
qulonglong TransmitController::targetRfHz() const noexcept {
  return target_rf_hz_;
}
QString TransmitController::preparedMessage() const {
  return QString::fromLatin1(guard_.pending_message().data(),
                             static_cast<qsizetype>(
                                 guard_.pending_message().size()));
}
bool TransmitController::messageConfirmed() const noexcept {
  return guard_.message_confirmed();
}
double TransmitController::previewDurationSeconds() const noexcept {
  return plan_ ? static_cast<double>(plan_->total_duration_ns) /
                     1'000'000'000.0
               : 0.0;
}
int TransmitController::wordsPerMinute() const noexcept {
  return words_per_minute_;
}
const QString& TransmitController::speedSource() const noexcept {
  return speed_source_;
}
const QString& TransmitController::report() const noexcept { return report_; }
bool TransmitController::autoQsoEnabled() const noexcept {
  return auto_qso_enabled_;
}
bool TransmitController::tuning() const noexcept {
  return guard_.state() == cwassistant::core::TransmitState::Tuning;
}
bool TransmitController::onAir() const noexcept {
  return guard_.key_down();
}
const QString& TransmitController::proposedMessage() const noexcept {
  return proposed_message_;
}
const QString& TransmitController::proposedReason() const noexcept {
  return proposed_reason_;
}
bool TransmitController::hardwareAvailable() const noexcept {
  return hardware_enabled_ && hardware_ && hardware_->available();
}
bool TransmitController::transmitting() const noexcept {
  return hardware_ && hardware_->busy();
}
const QString& TransmitController::hardwareStatus() const noexcept {
  static const QString unavailable =
      QStringLiteral("Direct transmit adapter is not configured");
  return hardware_ ? hardware_->status() : unavailable;
}

void TransmitController::setStatus(QString status) {
  status_ = std::move(status);
  emit changed();
}

void TransmitController::setOwnCallsign(const QString& callsign) {
  const auto normalized = cwassistant::core::CallsignPolicy::normalize(
      callsign.trimmed().toStdString());
  own_callsign_ = normalized ? QString::fromStdString(*normalized) : QString{};
  if (own_callsign_.isEmpty() && armed()) disarm();
  emit changed();
}

void TransmitController::configureTxSpeed(const int mode,
                                          const int fixed_wpm) {
  tx_speed_mode_ = std::clamp(mode, 0, 1);
  fixed_tx_wpm_ = std::clamp(fixed_wpm, 5, 80);
  const bool can_match = tx_speed_mode_ == 0 &&
      std::isfinite(selected_rx_wpm_) && selected_rx_wpm_ >= 5.0 &&
      selected_rx_wpm_ <= 80.0;
  const int selected = can_match
      ? std::clamp(static_cast<int>(std::lround(selected_rx_wpm_)), 5, 80)
      : fixed_tx_wpm_;
  const QString source = can_match
      ? QStringLiteral("Matched selected RX")
      : (tx_speed_mode_ == 0 ? QStringLiteral("Fixed fallback; RX WPM unavailable")
                             : QStringLiteral("Fixed setting"));
  const bool source_changed = speed_source_ != source;
  speed_source_ = source;
  const int previous_speed = words_per_minute_;
  setWordsPerMinute(selected);
  if (source_changed && previous_speed == words_per_minute_) emit changed();
}

void TransmitController::setWordsPerMinute(const int value) {
  const int sanitized = std::clamp(value, 5, 80);
  if (words_per_minute_ == sanitized) return;
  words_per_minute_ = sanitized;
  if (!preparedMessage().isEmpty()) {
    plan_ = cwassistant::core::CwTransmitEncoder::encode(
        guard_.pending_message(), static_cast<std::uint16_t>(words_per_minute_));
  }
  emit changed();
}

void TransmitController::setReport(const QString& value) {
  const QString trimmed = value.trimmed().toUpper();
  if (trimmed.isEmpty() || trimmed.size() > 32 || report_ == trimmed) return;
  report_ = trimmed;
  emit changed();
}

void TransmitController::setAutoQsoEnabled(const bool enabled) {
  if (auto_qso_enabled_ == enabled) return;
  auto_qso_enabled_ = enabled;
  proposed_message_.clear();
  proposed_reason_.clear();
  setStatus(enabled
      ? QStringLiteral("Auto-QSO suggestions enabled; every transmission still passes the TX guard")
      : QStringLiteral("Manual TX preparation enabled"));
}

bool TransmitController::arm() {
  if (own_callsign_.isEmpty()) {
    setStatus(QStringLiteral("Configure and save your callsign before arming TX"));
    return false;
  }
  if (!guard_.arm()) {
    setStatus(QStringLiteral("TX can be armed only from the disarmed state"));
    return false;
  }
  setStatus(QStringLiteral("TX armed; select and exactly confirm a station"));
  return true;
}

void TransmitController::disarm() {
  guard_.disarm();
  target_channel_id_ = 0;
  target_rf_hz_ = 0;
  selected_rx_wpm_ = 0.0;
  observed_text_length_ = 0;
  proposed_message_.clear();
  proposed_reason_.clear();
  clearPrepared();
  setStatus(QStringLiteral("Transmit disarmed"));
}

bool TransmitController::selectTarget(const qulonglong channel_id,
                                      const QString& callsign,
                                      const qulonglong rf_frequency_hz,
                                      const double received_wpm) {
  if (!armed()) {
    setStatus(QStringLiteral("Arm TX before selecting a station"));
    return false;
  }
  if (guard_.state() == cwassistant::core::TransmitState::Confirmed)
    static_cast<void>(guard_.end_qso());
  if (!guard_.request_qso(callsign.toStdString())) {
    setStatus(QStringLiteral(
        "Only an exact decoded callsign can be selected for TX confirmation"));
    return false;
  }
  target_channel_id_ = channel_id;
  target_rf_hz_ = rf_frequency_hz;
  selected_rx_wpm_ = std::isfinite(received_wpm) && received_wpm >= 5.0 &&
          received_wpm <= 80.0
      ? received_wpm : 0.0;
  configureTxSpeed(tx_speed_mode_, fixed_tx_wpm_);
  observed_text_length_ = 0;
  clearPrepared();
  setStatus(QStringLiteral("Retype %1 exactly to confirm this QSO target")
                .arg(targetCallsign()));
  return true;
}

bool TransmitController::confirmTarget(const QString& callsign) {
  if (!guard_.confirm(callsign.toStdString())) {
    setStatus(QStringLiteral("The confirmation does not exactly match the selected callsign"));
    return false;
  }
  setStatus(QStringLiteral("QSO with %1 confirmed; prepare a message")
                .arg(targetCallsign()));
  return true;
}

void TransmitController::clearPrepared() { plan_.reset(); }

bool TransmitController::prepare(QString text) {
  if (!guard_.stage_message(text.toStdString())) {
    setStatus(QStringLiteral(
        "Enter 1–512 Morse-compatible characters after confirming the QSO"));
    return false;
  }
  plan_ = cwassistant::core::CwTransmitEncoder::encode(
      guard_.pending_message(), static_cast<std::uint16_t>(words_per_minute_));
  if (!plan_) {
    setStatus(QStringLiteral("The normalized message could not be encoded"));
    return false;
  }
  proposed_message_.clear();
  proposed_reason_.clear();
  setStatus(QStringLiteral("Verify the exact preview before transmission"));
  return true;
}

bool TransmitController::prepareFreeText(const QString& text) {
  return prepare(text);
}
bool TransmitController::prepareOwnCall() { return prepare(own_callsign_); }
bool TransmitController::prepareReport() { return prepare(report_); }
bool TransmitController::acceptProposal() {
  if (proposed_message_.isEmpty()) return false;
  return prepare(proposed_message_);
}

bool TransmitController::confirmPreview(const QString& exact_preview) {
  if (!guard_.confirm_message(exact_preview.toStdString())) {
    setStatus(QStringLiteral("The confirmation must match the normalized preview exactly"));
    return false;
  }
  setStatus(QStringLiteral(
      "Message is guard-confirmed; no hardware keying adapter is installed yet"));
  return true;
}

bool TransmitController::transmitPrepared() {
  if (!guard_.message_confirmed()) {
    setStatus(QStringLiteral("Confirm the exact message preview first"));
    return false;
  }
  setStatus(QStringLiteral(
      "Transmission blocked safely: no tested local KEY/PTT adapter is installed"));
  return false;
}

bool TransmitController::toggleTune() {
  if (tuning()) {
    static_cast<void>(guard_.finish_tune());
    setStatus(QStringLiteral("TUNE released"));
    return true;
  }
  if (!armed()) {
    setStatus(QStringLiteral("Arm TX before using TUNE"));
    return false;
  }
  // Do not enter the guard's tuning state until an adapter exists: doing so
  // would make the UI claim that KEY is asserted when no line can be driven.
  setStatus(QStringLiteral(
      "TUNE blocked safely: no tested local KEY adapter is installed"));
  return false;
}

bool TransmitController::endQso() {
  if (!guard_.end_qso()) return false;
  target_channel_id_ = 0;
  target_rf_hz_ = 0;
  selected_rx_wpm_ = 0.0;
  observed_text_length_ = 0;
  clearPrepared();
  setStatus(QStringLiteral("QSO ended; TX remains armed"));
  return true;
}

void TransmitController::emergencyRelease() {
  guard_.emergency_release();
  target_channel_id_ = 0;
  target_rf_hz_ = 0;
  selected_rx_wpm_ = 0.0;
  observed_text_length_ = 0;
  clearPrepared();
  proposed_message_.clear();
  proposed_reason_.clear();
  setStatus(QStringLiteral("Emergency release latched; PTT/KEY must be inactive"));
}

bool TransmitController::resetFault() {
  if (!guard_.reset_fault()) return false;
  setStatus(QStringLiteral("Fault reset; transmit is disarmed"));
  return true;
}

void TransmitController::observeDecoderChannels(const QVariantList& channels) {
  if (!auto_qso_enabled_ || !qsoConfirmed() || target_channel_id_ == 0 ||
      own_callsign_.isEmpty() || !proposed_message_.isEmpty()) {
    return;
  }
  QVariantMap target;
  for (const QVariant& value : channels) {
    const QVariantMap item = value.toMap();
    if (item.value(QStringLiteral("id")).toULongLong() == target_channel_id_) {
      target = item;
      break;
    }
  }
  if (target.isEmpty()) return;
  // TX suggestions consume the deterministic raw acoustic transcript only.
  // Appending a separately revised model transcript here makes offsets move
  // backwards and can replay old cues as if they were newly heard.
  const QString text = target.value(QStringLiteral("text")).toString().toUpper();
  if (text.size() <= observed_text_length_) return;
  const qsizetype overlap = std::min<qsizetype>(observed_text_length_, 16);
  const QString fresh = text.mid(observed_text_length_ - overlap);
  observed_text_length_ = text.size();
  const QStringList tokens = fresh.split(
      QRegularExpression(QStringLiteral("[^A-Z0-9/]+")), Qt::SkipEmptyParts);
  if (tokens.contains(own_callsign_)) {
    proposed_message_ = report_;
    proposed_reason_ = QStringLiteral("Your exact callsign was decoded");
  } else if (tokens.contains(QStringLiteral("CQ")) ||
             tokens.contains(QStringLiteral("QRZ")) ||
             tokens.contains(QStringLiteral("UP")) ||
             tokens.contains(targetCallsign())) {
    proposed_message_ = own_callsign_;
    proposed_reason_ = QStringLiteral("The selected station appears to be listening");
  }
  if (proposed_message_.isEmpty()) {
    // A near miss is deliberately weaker than an exact own-call decode. It may
    // suggest repeating the operator's call only after the same raw acoustic
    // token appears at least twice; it never confirms or keys anything.
    const QString& raw_text = text;
    QHash<QString, int> occurrences;
    const QRegularExpression call_tokens(QStringLiteral("[A-Z0-9/?]{3,16}"));
    auto match = call_tokens.globalMatch(raw_text);
    while (match.hasNext()) {
      const QString token = match.next().captured();
      if (token != own_callsign_ && resemblesCallsign(token) &&
          wildcardEditDistance(token, own_callsign_, 2) <= 2) {
        ++occurrences[token];
      }
    }
    for (auto iterator = occurrences.cbegin(); iterator != occurrences.cend();
         ++iterator) {
      if (iterator.value() >= 2) {
        proposed_message_ = own_callsign_;
        proposed_reason_ = QStringLiteral(
            "A similar callsign was decoded twice; review before repeating your call");
        break;
      }
    }
  }
  if (!proposed_message_.isEmpty()) emit changed();
}

}  // namespace cwassistant::desktop
