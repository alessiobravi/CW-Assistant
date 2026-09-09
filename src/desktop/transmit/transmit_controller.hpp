#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
#include <QVariantList>

#include <optional>
#include <memory>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cw_transmit_encoder.hpp"
#include "cwassistant/core/transmit_guard.hpp"
#include "transmit/direct_transmit_engine.hpp"

namespace cwassistant::desktop {

// Operator-facing preparation boundary. Decoder output only proposes text;
// this controller owns the independently guarded hardware engine.
class TransmitController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool armed READ armed NOTIFY changed)
  Q_PROPERTY(bool qsoConfirmed READ qsoConfirmed NOTIFY changed)
  Q_PROPERTY(QString targetCallsign READ targetCallsign NOTIFY changed)
  Q_PROPERTY(qulonglong targetRfHz READ targetRfHz NOTIFY changed)
  Q_PROPERTY(QString preparedMessage READ preparedMessage NOTIFY changed)
  Q_PROPERTY(bool messageConfirmed READ messageConfirmed NOTIFY changed)
  Q_PROPERTY(double previewDurationSeconds READ previewDurationSeconds
                 NOTIFY changed)
  Q_PROPERTY(int wordsPerMinute READ wordsPerMinute WRITE setWordsPerMinute
                 NOTIFY changed)
  Q_PROPERTY(QString speedSource READ speedSource NOTIFY changed)
  Q_PROPERTY(QString report READ report WRITE setReport NOTIFY changed)
  Q_PROPERTY(bool autoQsoEnabled READ autoQsoEnabled WRITE setAutoQsoEnabled
                 NOTIFY changed)
  Q_PROPERTY(QString proposedMessage READ proposedMessage NOTIFY changed)
  Q_PROPERTY(QString proposedReason READ proposedReason NOTIFY changed)
  Q_PROPERTY(bool hardwareAvailable READ hardwareAvailable NOTIFY changed)
  Q_PROPERTY(bool stationReady READ stationReady NOTIFY changed)
  Q_PROPERTY(bool transmitting READ transmitting NOTIFY changed)
  Q_PROPERTY(QString hardwareStatus READ hardwareStatus NOTIFY changed)
  Q_PROPERTY(bool onAir READ onAir NOTIFY changed)
  Q_PROPERTY(bool tuning READ tuning NOTIFY changed)

 public:
  explicit TransmitController(QObject* parent = nullptr);
  explicit TransmitController(DirectTransmitEngine::BackendFactory backend_factory,
                              QObject* parent = nullptr);
  ~TransmitController() override;

  [[nodiscard]] QString state() const;
  [[nodiscard]] const QString& status() const noexcept;
  [[nodiscard]] bool armed() const noexcept;
  [[nodiscard]] bool qsoConfirmed() const noexcept;
  [[nodiscard]] QString targetCallsign() const;
  [[nodiscard]] qulonglong targetRfHz() const noexcept;
  [[nodiscard]] QString preparedMessage() const;
  [[nodiscard]] bool messageConfirmed() const noexcept;
  [[nodiscard]] double previewDurationSeconds() const noexcept;
  [[nodiscard]] int wordsPerMinute() const noexcept;
  [[nodiscard]] const QString& speedSource() const noexcept;
  [[nodiscard]] const QString& report() const noexcept;
  [[nodiscard]] bool autoQsoEnabled() const noexcept;
  [[nodiscard]] const QString& proposedMessage() const noexcept;
  [[nodiscard]] const QString& proposedReason() const noexcept;
  [[nodiscard]] bool hardwareAvailable() const noexcept;
  [[nodiscard]] bool stationReady() const noexcept;
  [[nodiscard]] bool transmitting() const noexcept;
  [[nodiscard]] const QString& hardwareStatus() const noexcept;
  [[nodiscard]] bool onAir() const noexcept;
  [[nodiscard]] bool tuning() const noexcept;

  void setOwnCallsign(const QString& callsign);
  void configureHardware(bool enabled, const QString& port_name,
                         int ptt_line_index, int key_line_index,
                         bool ptt_active_high, bool key_active_high,
                         const QString& cat_port_name,
                         bool loopback_validated);
  void configureTxSpeed(int mode, int fixed_wpm);
  void configureRadioSafety(bool radio_enabled, qulonglong tx_rf_hz,
                            const QString& tx_mode_target,
                            bool tx_mode_confirmed, bool split_known,
                            bool split_active);
  void setWordsPerMinute(int value);
  void setReport(const QString& value);
  void setAutoQsoEnabled(bool enabled);
  void observeDecoderChannels(const QVariantList& channels);

  Q_INVOKABLE bool arm();
  Q_INVOKABLE void disarm();
  Q_INVOKABLE bool selectTarget(qulonglong channel_id,
                                const QString& callsign,
                                qulonglong rf_frequency_hz,
                                double received_wpm = 0.0);
  Q_INVOKABLE bool confirmTarget(const QString& callsign);
  Q_INVOKABLE bool prepareFreeText(const QString& text);
  Q_INVOKABLE bool prepareOwnCall();
  Q_INVOKABLE bool prepareReport();
  Q_INVOKABLE bool acceptProposal();
  Q_INVOKABLE bool confirmPreview(const QString& exact_preview);
  Q_INVOKABLE bool transmitPrepared();
  Q_INVOKABLE bool cancelTransmission();
  Q_INVOKABLE bool toggleTune();
  Q_INVOKABLE bool endQso();
  Q_INVOKABLE void emergencyRelease();
  Q_INVOKABLE bool resetFault();

 signals:
  void changed();

 private:
  bool prepare(QString text);
  void clearPrepared();
  void setStatus(QString status);
  void handleHardwareChanged();
  [[nodiscard]] bool radioSafetyStillConfirmed() const noexcept;

  cwassistant::core::CallsignPolicy callsign_policy_;
  cwassistant::core::TransmitGuard guard_;
  std::unique_ptr<DirectTransmitEngine> hardware_;
  std::optional<cwassistant::core::CwTransmitPlan> plan_;
  QString own_callsign_;
  QString report_{QStringLiteral("599")};
  QString status_{QStringLiteral("Transmit disarmed")};
  QString proposed_message_;
  QString proposed_reason_;
  QString speed_source_{QStringLiteral("Fixed fallback")};
  qulonglong target_channel_id_{0};
  qulonglong target_rf_hz_{0};
  qsizetype observed_text_length_{0};
  int words_per_minute_{20};
  int tx_speed_mode_{0};
  int fixed_tx_wpm_{20};
  double selected_rx_wpm_{0.0};
  bool auto_qso_enabled_{false};
  bool hardware_enabled_{false};
  bool hardware_was_busy_{false};
  bool handling_hardware_change_{false};
  bool radio_tx_ready_{false};
  bool radio_split_active_{false};
  bool armed_radio_split_active_{false};
  qulonglong radio_tx_rf_hz_{0};
  qulonglong armed_radio_tx_rf_hz_{0};
  QString radio_tx_mode_;
  QString armed_radio_tx_mode_;
  DirectTransmitEngineConfig hardware_config_{};
  QString hardware_configuration_key_;
  QElapsedTimer hardware_clock_;
  QTimer hardware_watchdog_;
};

}  // namespace cwassistant::desktop
