#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

#include <cstdint>
#include <optional>

#include "cwassistant/core/cat4om_protocol.hpp"

namespace cwassistant::desktop {

// Native CAT4OM Control-channel client. It intentionally exposes frequency and
// split only; transmitter control remains behind CW Assistant's independent
// local safety boundary.
class Cat4OmClient final : public QObject {
  Q_OBJECT

 public:
  explicit Cat4OmClient(QObject* parent = nullptr);

  void connectToServer(const QUrl& endpoint, QString radio_id,
                       const QString& password, bool observer);
  void disconnectFromServer();

  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool canWrite() const noexcept;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] QString radioId() const;
  [[nodiscard]] std::optional<cwassistant::core::VfoFrequencyPlan>
  frequencyPlan() const noexcept;

  bool requestOwnership();
  bool setFrequency(std::uint64_t frequency_hz, const QString& vfo = {});
  bool setSplit(bool enabled, const QString& tx_vfo = {});

 signals:
  void statusChanged();
  void radioStateChanged();

 private:
  void sendHello();
  void handleTextMessage(const QString& message);
  void handleWelcome(const QJsonObject& message);
  void handleStateUpdate(const QJsonObject& message);
  void handleEvent(const QJsonObject& message);
  void handleResponse(const QJsonObject& message);
  void replaceOrMergeRadios(const QJsonArray& radios, bool full);
  void updateSelectedRadio();
  bool sendRequest(const QString& action, const QJsonObject& parameters,
                   bool radio_scoped);
  void updateRole(const QString& master_id);
  void setStatus(QString status);
  void fail(QString status);
  void scheduleReconnect();
  [[nodiscard]] static cwassistant::core::Cat4OmRadioState parseRadio(
      const QJsonObject& radio);

  QWebSocket socket_;
  QTimer handshake_timer_;
  QTimer inactivity_timer_;
  QTimer reconnect_timer_;
  QUrl endpoint_;
  QString configured_radio_id_;
  QString password_;
  QString client_id_;
  QString instance_id_;
  QString status_{QStringLiteral("Disconnected")};
  cwassistant::core::Cat4OmRole role_{cwassistant::core::Cat4OmRole::Unknown};
  cwassistant::core::Cat4OmRadioState selected_radio_;
  QHash<QString, QJsonObject> radios_;
  QHash<QString, QString> pending_actions_;
  qint64 last_sequence_{-1};
  quint64 request_sequence_{0};
  int reconnect_attempt_{0};
  bool observer_{true};
  bool welcomed_{false};
  bool user_disconnect_{true};
  bool preserve_disconnect_status_{false};
};

}  // namespace cwassistant::desktop
