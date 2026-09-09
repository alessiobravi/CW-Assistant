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
    : TransmitController(DirectTransmitEngine::BackendFactory{}, parent) {}

TransmitController::TransmitController(
    DirectTransmitEngine::BackendFactory backend_factory, QObject* parent)
    : QObject(parent), guard_(callsign_policy_),
      hardware_(std::make_unique<DirectTransmitEngine>(
          std::move(backend_factory), this)) {
  hardware_clock_.start();
  connect(hardware_.get(), &DirectTransmitEngine::changed, this,
          &TransmitController::handleHardwareChanged);
  hardware_watchdog_.setInterval(25);
  hardware_watchdog_.setTimerType(Qt::PreciseTimer);
  connect(&hardware_watchdog_, &QTimer::timeout, this,
          &TransmitController::handleHardwareChanged);
}

TransmitController::~TransmitController() {
  hardware_watchdog_.stop();
  if (!hardware_) return;
  disconnect(hardware_.get(), nullptr, this, nullptr);
  hardware_->close();
  hardware_.reset();
}

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
const QString& TransmitController::exchange() const noexcept {
  return exchange_;
}
bool TransmitController::autoQsoEnabled() const noexcept {
  return auto_qso_enabled_;
}
bool TransmitController::tuning() const noexcept {
  return guard_.state() == cwassistant::core::TransmitState::Tuning;
}
bool TransmitController::onAir() const noexcept {
  return hardware_ && hardware_->key();
}
double TransmitController::txElapsedSeconds() const noexcept {
  return hardware_ ? static_cast<double>(hardware_->elapsedNs()) /
                         1'000'000'000.0 : 0.0;
}
double TransmitController::txRemainingSeconds() const noexcept {
  return hardware_ ? static_cast<double>(hardware_->remainingNs()) /
                         1'000'000'000.0 : 0.0;
}
double TransmitController::txProgress() const noexcept {
  return hardware_ ? hardware_->progress() : 0.0;
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
bool TransmitController::stationReady() const noexcept {
  return hardwareAvailable() && radio_tx_ready_;
}
bool TransmitController::transmitting() const noexcept {
  return guard_.state() == cwassistant::core::TransmitState::Transmitting;
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

void TransmitController::configureHardware(
    const bool enabled, const QString& port_name, const int ptt_line_index,
    const int key_line_index, const bool ptt_active_high,
    const bool key_active_high, const QString& cat_port_name,
    const bool loopback_validated) {
  const QString port = port_name.trimmed();
  const QString cat_port = cat_port_name.trimmed();
  const QString configuration_key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
      .arg(enabled ? 1 : 0).arg(port).arg(ptt_line_index).arg(key_line_index)
      .arg(ptt_active_high ? 1 : 0).arg(key_active_high ? 1 : 0).arg(cat_port)
      .arg(loopback_validated ? 1 : 0);
  if (configuration_key == hardware_configuration_key_) return;

  const bool was_active = armed() || (hardware_ &&
      (hardware_->busy() || hardware_->ptt() || hardware_->key()));
  if (was_active) hardware_watchdog_.stop();
  if (was_active && hardware_) hardware_->emergencyRelease();
  if (was_active) guard_.trip_fault();
  else guard_.disarm();

  hardware_configuration_key_ = configuration_key;
  const bool line_indices_valid =
      (ptt_line_index == 0 || ptt_line_index == 1) &&
      (key_line_index == 0 || key_line_index == 1);
  hardware_enabled_ = enabled && !port.isEmpty() && line_indices_valid &&
      ptt_line_index != key_line_index && ptt_active_high && key_active_high &&
      loopback_validated &&
      (cat_port.isEmpty() || port.compare(cat_port, Qt::CaseInsensitive) != 0);
  hardware_config_ = {
      .keying = {
          .port_name = port,
          .ptt_line = ptt_line_index == 0 ? DirectKeyingLine::Rts
                                         : DirectKeyingLine::Dtr,
          .key_line = key_line_index == 0 ? DirectKeyingLine::Rts
                                         : DirectKeyingLine::Dtr,
          .ptt_active_high = ptt_active_high,
          .key_active_high = key_active_high,
      },
  };
  if (hardware_) {
    if (hardware_enabled_) hardware_->configure(hardware_config_);
    else hardware_->close();
  }
  hardware_was_busy_ = false;

  if (!enabled) {
    setStatus(QStringLiteral("Direct KEY/PTT hardware is disabled"));
  } else if (port.isEmpty()) {
    setStatus(QStringLiteral("Select an explicit direct KEY/PTT port"));
  } else if (!cat_port.isEmpty() &&
             port.compare(cat_port, Qt::CaseInsensitive) == 0) {
    setStatus(QStringLiteral(
        "Direct KEY/PTT and CAT must use physically separate ports"));
  } else if (!line_indices_valid) {
    setStatus(QStringLiteral("PTT and KEY must each select RTS or DTR"));
  } else if (ptt_line_index == key_line_index) {
    setStatus(QStringLiteral("PTT and KEY must use different control lines"));
  } else if (!ptt_active_high || !key_active_high) {
    setStatus(QStringLiteral(
        "This guarded hardware slice accepts verified active-high interfaces only"));
  } else if (!loopback_validated) {
    setStatus(QStringLiteral(
        "Complete disconnected-line and physical-loopback validation before arming"));
  } else if (was_active) {
    setStatus(QStringLiteral(
        "Hardware configuration changed; emergency release is latched"));
  } else {
    setStatus(QStringLiteral(
        "Direct KEY/PTT configured and closed; arming will verify inactive lines"));
  }
}

void TransmitController::configureRadioSafety(
    const bool radio_enabled, const qulonglong tx_rf_hz,
    const QString& tx_mode_target, const bool tx_mode_confirmed,
    const bool split_known, const bool split_active) {
  const QString mode = tx_mode_target.trimmed().toUpper();
  const bool ready = radio_enabled && tx_rf_hz > 0U &&
      tx_rf_hz <= 99'999'999'999ULL &&
      (mode == QStringLiteral("CW") || mode == QStringLiteral("CW-R")) &&
      tx_mode_confirmed && split_known;
  const bool identity_changed = radio_tx_ready_ != ready ||
      radio_tx_rf_hz_ != tx_rf_hz || radio_tx_mode_ != mode ||
      radio_split_active_ != split_active;
  radio_tx_ready_ = ready;
  radio_tx_rf_hz_ = tx_rf_hz;
  radio_tx_mode_ = mode;
  radio_split_active_ = split_active;

  if (armed() && identity_changed && !radioSafetyStillConfirmed()) {
    hardware_watchdog_.stop();
    if (hardware_ &&
        (hardware_->busy() || hardware_->ptt() || hardware_->key())) {
      hardware_->emergencyRelease();
      guard_.trip_fault();
      setStatus(QStringLiteral(
          "Confirmed TX radio state changed on air; emergency release is latched"));
    } else {
      if (hardware_) hardware_->close();
      guard_.disarm();
      setStatus(QStringLiteral(
          "Confirmed TX radio state changed; TX is disarmed and must be re-armed"));
    }
    hardware_was_busy_ = false;
    return;
  }
  if (identity_changed) emit changed();
}

bool TransmitController::radioSafetyStillConfirmed() const noexcept {
  return radio_tx_ready_ && radio_tx_rf_hz_ == armed_radio_tx_rf_hz_ &&
      radio_tx_mode_ == armed_radio_tx_mode_ &&
      radio_split_active_ == armed_radio_split_active_;
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

void TransmitController::setExchange(const QString& value) {
  const QString trimmed = value.trimmed().toUpper();
  if (trimmed.isEmpty() || trimmed.size() > 64 || exchange_ == trimmed) return;
  exchange_ = trimmed;
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
  if (!hardwareAvailable()) {
    setStatus(QStringLiteral(
        "Configure a dedicated, supported direct KEY/PTT adapter before arming"));
    return false;
  }
  if (!radio_tx_ready_) {
    setStatus(QStringLiteral(
        "TX frequency, CW/CW-R mode, and simplex/split state must all be confirmed by the radio provider"));
    return false;
  }
  if (!hardware_->openSafe()) {
    setStatus(QStringLiteral("Cannot arm: %1").arg(hardware_->status()));
    return false;
  }
  if (!guard_.arm()) {
    hardware_->close();
    setStatus(QStringLiteral("TX can be armed only from the disarmed state"));
    return false;
  }
  armed_radio_tx_rf_hz_ = radio_tx_rf_hz_;
  armed_radio_tx_mode_ = radio_tx_mode_;
  armed_radio_split_active_ = radio_split_active_;
  setStatus(QStringLiteral(
      "TX armed at %1 Hz %2; select and exactly confirm a station")
                .arg(radio_tx_rf_hz_).arg(radio_tx_mode_));
  return true;
}

void TransmitController::disarm() {
  hardware_watchdog_.stop();
  if (hardware_) hardware_->close();
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
bool TransmitController::prepareExchange() { return prepare(exchange_); }
bool TransmitController::prepareMacro(const QString& text) {
  return prepare(text);
}
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
      "Message is exactly confirmed and ready for guarded transmission"));
  return true;
}

bool TransmitController::transmitPrepared() {
  if (!guard_.message_confirmed()) {
    setStatus(QStringLiteral("Confirm the exact message preview first"));
    return false;
  }
  if (!plan_ || !stationReady() || !radioSafetyStillConfirmed() ||
      !hardware_->openSafeState() || hardware_->busy()) {
    setStatus(QStringLiteral(
        "Transmission blocked: hardware and confirmed TX radio state must remain ready"));
    return false;
  }
  if (!guard_.begin_transmission()) {
    setStatus(QStringLiteral("The TX safety guard rejected the message"));
    return false;
  }
  if (!hardware_->start(*plan_, true)) {
    hardware_->emergencyRelease();
    guard_.trip_fault();
    setStatus(QStringLiteral("Transmit start failed; emergency release is latched: %1")
                  .arg(hardware_->status()));
    return false;
  }
  hardware_watchdog_.start();
  setStatus(QStringLiteral("Transmitting exactly confirmed CW at %1 WPM")
                .arg(words_per_minute_));
  return true;
}

bool TransmitController::cancelTransmission() {
  if (!transmitting() || !hardware_) return false;
  if (!hardware_->cancel()) {
    hardware_->emergencyRelease();
    guard_.trip_fault();
    setStatus(QStringLiteral("Cancellation failed; emergency release is latched"));
    return false;
  }
  hardware_watchdog_.stop();
  if (guard_.state() == cwassistant::core::TransmitState::Transmitting)
    static_cast<void>(guard_.finish_transmission());
  setStatus(QStringLiteral("Transmission cancelled; KEY and PTT are inactive"));
  return true;
}

bool TransmitController::toggleTune() {
  if (tuning()) {
    if (!hardware_ || !hardware_->stopTune()) {
      if (hardware_) hardware_->emergencyRelease();
      guard_.trip_fault();
      setStatus(QStringLiteral("TUNE release failed; emergency release is latched"));
      return false;
    }
    hardware_watchdog_.stop();
    if (guard_.state() == cwassistant::core::TransmitState::Tuning)
      static_cast<void>(guard_.finish_tune());
    setStatus(QStringLiteral("TUNE released"));
    return true;
  }
  if (!armed()) {
    setStatus(QStringLiteral("Arm TX before using TUNE"));
    return false;
  }
  if (!stationReady() || !radioSafetyStillConfirmed() || !hardware_ ||
      !hardware_->openSafeState() || hardware_->busy()) {
    setStatus(QStringLiteral(
        "TUNE blocked: hardware and confirmed TX radio state must remain ready"));
    return false;
  }
  if (!guard_.begin_tune() || !hardware_->startTune(true)) {
    if (hardware_) hardware_->emergencyRelease();
    guard_.trip_fault();
    setStatus(QStringLiteral("TUNE start failed; emergency release is latched"));
    return false;
  }
  hardware_watchdog_.start();
  setStatus(QStringLiteral("TUNE active; press again to stop (15-second maximum)"));
  return true;
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
  hardware_watchdog_.stop();
  if (hardware_) hardware_->emergencyRelease();
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
  if (hardware_) {
    if (hardware_enabled_) hardware_->configure(hardware_config_);
    else hardware_->close();
  }
  hardware_was_busy_ = false;
  hardware_watchdog_.stop();
  setStatus(QStringLiteral("Fault reset; hardware is closed and transmit is disarmed"));
  return true;
}

void TransmitController::handleHardwareChanged() {
  if (handling_hardware_change_ || !hardware_) return;
  handling_hardware_change_ = true;

  const auto state = guard_.state();
  const bool guarded_output =
      state == cwassistant::core::TransmitState::Transmitting ||
      state == cwassistant::core::TransmitState::Tuning;
  if (hardware_->fault() && (armed() || hardware_->ptt() || hardware_->key())) {
    hardware_watchdog_.stop();
    guard_.trip_fault();
    setStatus(QStringLiteral("Hardware fault; KEY/PTT release is latched: %1")
                  .arg(hardware_->status()));
  } else if ((hardware_->ptt() || hardware_->key()) && !guarded_output) {
    hardware_watchdog_.stop();
    hardware_->emergencyRelease();
    guard_.trip_fault();
    setStatus(QStringLiteral(
        "Unexpected transmit line assertion; emergency release is latched"));
  } else if (guarded_output) {
    const qint64 elapsed_ns = hardware_clock_.isValid()
        ? hardware_clock_.nsecsElapsed() : -1;
    const auto timestamp_ns = elapsed_ns >= 0
        ? static_cast<std::uint64_t>(elapsed_ns) : 0U;
    if (!guard_.observe_key_state(hardware_->key(), timestamp_ns)) {
      hardware_watchdog_.stop();
      hardware_->emergencyRelease();
      setStatus(QStringLiteral(
          "Independent KEY watchdog rejected hardware state; emergency release is latched"));
    } else if (hardware_was_busy_ && !hardware_->busy()) {
      if (state == cwassistant::core::TransmitState::Transmitting) {
        hardware_watchdog_.stop();
        static_cast<void>(guard_.finish_transmission());
        setStatus(QStringLiteral(
            "Confirmed CW message completed; KEY and PTT are inactive"));
      } else if (state == cwassistant::core::TransmitState::Tuning &&
                 !hardware_->fault()) {
        hardware_watchdog_.stop();
        static_cast<void>(guard_.finish_tune());
        setStatus(QStringLiteral("TUNE released; KEY and PTT are inactive"));
      }
    }
  }
  hardware_was_busy_ = hardware_->busy();
  handling_hardware_change_ = false;
  emit changed();
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
