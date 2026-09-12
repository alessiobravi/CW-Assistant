#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>

class QTcpServer;
class QTcpSocket;

namespace cwassistant::desktop {

// A line-delimited JSON diagnostics stream, offered on operator-chosen network
// addresses so a station can be watched while it runs.
//
// Why this exists: a fault that did not reproduce on a developer's machine left
// nothing to examine but a description of it. The debug capture answers what
// the decoder found, and the throughput counters answer whether the application
// is keeping up, but both had to be collected, stopped and sent. This publishes
// the same records as they are produced.
//
// NO BYTE FROM A PEER CAN EVER SELECT AN ACTION. THAT IS THE SAFETY ARGUMENT.
//
// This process holds transmit, so the danger to guard against is not that
// bytes are read -- it is that a byte could choose something for this
// application to do. There is no command, no verb, no dispatch and no parser
// here, and nothing a peer sends can reach the radio, the settings or the
// decoder.
//
// Exactly one thing is ever read from a client: the access token, once, as a
// single bounded line before anything is sent to it. It is compared with the
// configured token and the comparison has precisely two outcomes, this
// connection continues or it ends. Nothing is read afterwards; a client that
// keeps sending is disconnected, because it has mistaken the port for
// something that answers.
//
// An earlier draft of this class refused to read at all, and therefore had no
// per-client authentication, on the reasoning that any input path near a
// transmit-capable process was unacceptable. That conflated reading a secret
// with interpreting a command. A fixed-length comparison against a shared
// token is not an interpreter, and refusing it bought no safety while costing
// the only thing standing between a bound routable address and anyone who
// could reach it.
//
// What an operator is choosing when they bind this to a routable address: the
// station's internal state -- frequencies, callsigns decoded, device
// identifiers, transcripts -- becomes readable by anything that can reach that
// address and holds the token. That is a deliberate act and the settings page
// says so plainly. It is off unless asked for, a token is required for any
// address that is not loopback, and the main window shows an indicator for as
// long as the service is listening, because a service an operator has forgotten
// is running is the one that will surprise them.
class DiagnosticsServer final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
  Q_PROPERTY(int clientCount READ clientCount NOTIFY stateChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

 public:
  explicit DiagnosticsServer(QObject* parent = nullptr);
  ~DiagnosticsServer() override;

  [[nodiscard]] bool listening() const noexcept;
  [[nodiscard]] int clientCount() const noexcept;
  [[nodiscard]] const QString& statusMessage() const noexcept;
  // Every address actually bound, for the indicator and the status line.
  [[nodiscard]] QStringList boundAddresses() const;

  // Addresses this machine could bind, as QML-ready maps carrying `address`,
  // `interfaceName`, `loopback` and `description`. Offered to the operator
  // rather than guessed at, because only they know which of their networks is
  // the one they meant.
  [[nodiscard]] static QVariantList discoverLocalAddresses();

  // A token long enough to be worth having. Rejects the empty string and
  // anything shorter than the minimum, because a token that can be guessed is
  // a token that was never asked for.
  [[nodiscard]] static bool isAcceptableToken(const QString& token);

  // Rebinds to exactly these addresses. An address that cannot be bound is
  // reported in the status line and the others still come up: a typo in one
  // interface should not silently take the service down.
  void configure(const QStringList& bind_addresses, std::uint16_t port,
                 const QString& access_token);
  // Which peers may connect at all, as addresses or CIDR subnets --
  // `192.168.1.50`, `192.168.1.0/24`, `2001:db8::/32` -- or the single entry
  // `any` for no restriction.
  //
  // This is the stronger of the two controls and the cheaper one. A peer's
  // address is known from the socket before a byte is exchanged, so a
  // disallowed peer is closed without a greeting, without a handshake and
  // without ever being told what is behind the port. The token can only be
  // checked after inviting the peer to present it; this needs no invitation.
  //
  // An empty list means loopback only. That is the safe reading of "the
  // operator has not said", and it matches the default binding: a service
  // nobody has configured should be reachable from nowhere but the machine
  // running it.
  void setAllowedPeers(const QStringList& patterns);
  [[nodiscard]] const QStringList& allowedPeers() const noexcept;
  // Whether one address would be admitted. Public so a settings page can tell
  // an operator what their list actually permits before the service is
  // running, and so the rule can be tested without a socket.
  [[nodiscard]] static bool peerIsAllowed(const QHostAddress& peer,
                                          const QStringList& patterns);

  void setEnabled(bool enabled);
  [[nodiscard]] bool enabled() const noexcept;

  // Publishes one record to every authenticated client. Called from whichever
  // thread produced the record; delivery is marshalled internally.
  void publish(const QJsonObject& record);

  // 17300 is unassigned in the IANA registry and, more to the point, clear of
  // the ports amateur software has claimed by convention: Hamlib's rigctld
  // answers on 4532 and rotctld on 4533, DX cluster nodes commonly use 7300,
  // 7373, 8000 and 23, and reverse-beacon telnet uses 7000 and 7001. An
  // earlier default of 4531 sat one port below rigctld -- no collision, but a
  // needless invitation to confusion in the one domain where that neighbour is
  // familiar, and this application can itself be a rigctld client. Deliberately
  // in the registered range rather than above 49152, because a listener placed
  // in the ephemeral range can collide with the ports the operating system
  // hands out for this process's own outgoing connections.
  static constexpr std::uint16_t kDefaultPort = 17300;
  // A client has this long to present the token, and this much room to do it
  // in. Both are bounded so an opened-and-forgotten connection cannot hold a
  // slot, and so a peer cannot grow this process by never sending a newline.
  static constexpr int kHandshakeTimeoutMs = 5'000;
  static constexpr qint64 kMaximumHandshakeBytes = 256;
  static constexpr int kMinimumTokenLength = 16;
  // Bounded on purpose. Each client costs a buffer, and a diagnostics stream
  // that let an unbounded number of peers connect would be a way to exhaust
  // the station it is meant to help diagnose.
  static constexpr int kMaximumClients = 4;
  // A client that cannot keep up is disconnected rather than buffered without
  // limit. Losing a slow observer is a smaller failure than growing this
  // process until the station stops.
  static constexpr qint64 kMaximumClientBacklogBytes = 4LL * 1024LL * 1024LL;
  // A peer that sends more than this has mistaken the port for something that
  // answers. Nothing received is ever parsed.
  static constexpr qint64 kMaximumIgnoredInputBytes = 4096;

 signals:
  void stateChanged();

 private:
  void acceptPending(QTcpServer* server);
  void dropClient(QTcpSocket* socket, const QString& reason);
  void restart();
  void setStatus(QString message);

  struct Client;
  QList<QTcpServer*> servers_;
  QList<Client*> clients_;
  QStringList bind_addresses_;
  QStringList bound_addresses_;
  QString access_token_;
  QStringList allowed_peers_;
  QString status_message_;
  std::uint16_t port_{kDefaultPort};
  bool enabled_{false};
};

}  // namespace cwassistant::desktop
