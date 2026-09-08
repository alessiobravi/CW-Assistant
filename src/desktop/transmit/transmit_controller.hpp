#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <optional>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cw_transmit_encoder.hpp"
#include "cwassistant/core/transmit_guard.hpp"

namespace cwassistant::desktop {

// Operator-facing preparation boundary for future hardware keying. This class
// can arm, confirm, encode, and queue a preview; it deliberately has no device
// handle and cannot assert PTT or KEY.
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
  Q_PROPERTY(QString report READ report WRITE setReport NOTIFY changed)
  Q_PROPERTY(bool autoQsoEnabled READ autoQsoEnabled WRITE setAutoQsoEnabled
                 NOTIFY changed)
  Q_PROPERTY(QString proposedMessage READ proposedMessage NOTIFY changed)
  Q_PROPERTY(QString proposedReason READ proposedReason NOTIFY changed)
  Q_PROPERTY(bool hardwareAvailable READ hardwareAvailable CONSTANT)
  Q_PROPERTY(bool onAir READ onAir NOTIFY changed)
  Q_PROPERTY(bool tuning READ tuning NOTIFY changed)

 public:
  explicit TransmitController(QObject* parent = nullptr);

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
  [[nodiscard]] const QString& report() const noexcept;
  [[nodiscard]] bool autoQsoEnabled() const noexcept;
  [[nodiscard]] const QString& proposedMessage() const noexcept;
  [[nodiscard]] const QString& proposedReason() const noexcept;
  [[nodiscard]] bool hardwareAvailable() const noexcept { return false; }
  [[nodiscard]] bool onAir() const noexcept;
  [[nodiscard]] bool tuning() const noexcept;

  void setOwnCallsign(const QString& callsign);
  void setWordsPerMinute(int value);
  void setReport(const QString& value);
  void setAutoQsoEnabled(bool enabled);
  void observeDecoderChannels(const QVariantList& channels);

  Q_INVOKABLE bool arm();
  Q_INVOKABLE void disarm();
  Q_INVOKABLE bool selectTarget(qulonglong channel_id,
                                const QString& callsign,
                                qulonglong rf_frequency_hz);
  Q_INVOKABLE bool confirmTarget(const QString& callsign);
  Q_INVOKABLE bool prepareFreeText(const QString& text);
  Q_INVOKABLE bool prepareOwnCall();
  Q_INVOKABLE bool prepareReport();
  Q_INVOKABLE bool acceptProposal();
  Q_INVOKABLE bool confirmPreview(const QString& exact_preview);
  Q_INVOKABLE bool transmitPrepared();
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

  cwassistant::core::CallsignPolicy callsign_policy_;
  cwassistant::core::TransmitGuard guard_;
  std::optional<cwassistant::core::CwTransmitPlan> plan_;
  QString own_callsign_;
  QString report_{QStringLiteral("599")};
  QString status_{QStringLiteral("Transmit disarmed")};
  QString proposed_message_;
  QString proposed_reason_;
  qulonglong target_channel_id_{0};
  qulonglong target_rf_hz_{0};
  qsizetype observed_text_length_{0};
  int words_per_minute_{20};
  bool auto_qso_enabled_{false};
};

}  // namespace cwassistant::desktop
