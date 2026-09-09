#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <optional>

#include "cwassistant/core/frequency_plan.hpp"
#include "cwassistant/core/radio_control.hpp"

namespace cwassistant::desktop {

// Provider-neutral Hamlib adapter using rigctld's bounded text protocol.  This
// class deliberately has no PTT or keying API: transmission remains owned by
// CW Buddy's independent guarded keying boundary.
class HamlibRigctldClient final : public QObject {
  Q_OBJECT

 public:
  struct Configuration {
    QString host{QStringLiteral("127.0.0.1")};
    quint16 port{4532};
    QString rx_vfo{QStringLiteral("VFOA")};
    QString tx_vfo{QStringLiteral("VFOB")};
    bool writable{false};
    bool vfo_mode{true};
    int poll_interval_ms{500};
    int request_timeout_ms{1500};
    int reconnect_interval_ms{2000};
  };

  explicit HamlibRigctldClient(QObject* parent = nullptr);
  ~HamlibRigctldClient() override;

  void connectToServer(Configuration configuration);
  void disconnectFromServer();

  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] bool canWrite() const noexcept;
  [[nodiscard]] QString statusText() const;
  [[nodiscard]] cwassistant::core::RadioState radioState() const noexcept;
  [[nodiscard]] std::optional<cwassistant::core::VfoFrequencyPlan>
  frequencyPlan() const noexcept;

  bool setRxFrequency(std::uint64_t frequency_hz);
  bool setTxFrequency(std::uint64_t frequency_hz);
  bool setRxMode(cwassistant::core::RadioMode mode);
  bool setTxMode(cwassistant::core::RadioMode mode);
  bool setSplit(bool enabled);

 signals:
  void statusChanged();
  void radioStateChanged();

 private:
  enum class ReplyKind { Frequency, Mode, Split, Result };
  enum class Field {
    VfoMode,
    RxFrequency,
    TxFrequency,
    RxMode,
    TxMode,
    Split,
    Write
  };
  struct Request {
    QByteArray command;
    ReplyKind reply_kind{ReplyKind::Result};
    Field field{Field::Write};
    int payload_lines{0};
    QList<QByteArray> payload;
  };

  void beginConnection();
  void beginPoll();
  void queue(Request request);
  void sendNext();
  void consumeLine(QByteArray line);
  void completeRequest(int result);
  void failProtocol(const QString& reason);
  void handleDisconnected();
  void scheduleReconnect();
  void setStatus(QString status);
  void clearObservedState();
  void publishPoll();
  [[nodiscard]] QByteArray command(QByteArray opcode, const QString& vfo,
                                   QByteArray argument = {}) const;
  [[nodiscard]] bool validConfiguration(const Configuration& value) const;
  [[nodiscard]] bool queueWrite(const QByteArray& opcode, const QString& vfo,
                                const QByteArray& argument,
                                const cwassistant::core::RadioCommand& command);

  QTcpSocket socket_;
  QTimer poll_timer_;
  QTimer request_timer_;
  QTimer reconnect_timer_;
  Configuration configuration_;
  QList<Request> requests_;
  std::optional<Request> active_request_;
  QByteArray receive_buffer_;
  QString status_{QStringLiteral("Disconnected")};
  cwassistant::core::RadioState state_;
  cwassistant::core::RadioState pending_poll_;
  int poll_fields_remaining_{0};
  bool operator_disconnect_{true};
};

}  // namespace cwassistant::desktop
