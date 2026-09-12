#include "diagnostics_server.hpp"

#include <QAbstractSocket>
#include <QByteArray>
#include <QChar>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
#include <QList>
#include <QMetaObject>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr char16_t kFirstPrintableAscii = 0x0020;
constexpr char16_t kDeleteAscii = 0x007F;
constexpr char16_t kHighestC1Control = 0x009F;

// The line-delimited JSON dialect this stream speaks, announced in the
// greeting so that a reader can refuse a version it does not know instead of
// misreading it.
constexpr int kStreamProtocolVersion = 1;

// A requested address is text the operator typed or pasted, and it is echoed
// back in the status line when it cannot be bound. It is bounded and reduced
// to printable ASCII first: the status line is shown in the UI, and a pasted
// control character has no business reaching it.
constexpr int kMaximumQuotedAddressLength = 64;

// Printable ASCII only, and bounded. The same treatment the cluster client
// gives a spotter, for the same reason: this text is displayed, never used.
[[nodiscard]] QString quotedForStatus(const QString& text) {
  QString safe;
  safe.reserve(kMaximumQuotedAddressLength);
  for (const QChar character : text) {
    if (safe.size() >= kMaximumQuotedAddressLength) break;
    const char16_t code = character.unicode();
    if (code >= kFirstPrintableAscii && code < kDeleteAscii)
      safe.append(QChar(code));
  }
  return safe.isEmpty() ? QStringLiteral("(blank)") : safe;
}

// How an address is written where a port follows it. An IPv6 address is
// bracketed, because `::1:17300` cannot be read back apart and `[::1]:17300`
// can; it is also the form the operator will paste into whatever tool they
// point at this stream.
[[nodiscard]] QString displayHost(const QHostAddress& address) {
  const QString text = address.toString();
  return text.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(text)
                                         : text;
}

[[nodiscard]] QString displayEndpoint(const QHostAddress& address,
                                      const quint16 port) {
  return QStringLiteral("%1:%2").arg(displayHost(address)).arg(port);
}

// The address an operator asked to bind, or nothing if that text is not one.
//
// Two spellings beyond the plain literal are accepted, because both are what
// a person actually has in their clipboard. An IPv6 link-local address is only
// meaningful together with the interface it belongs to -- `fe80::1%en0` -- and
// that interface is carried after a percent sign rather than inside the
// address, so it is split off and applied as the scope id. And `[::1]` is how
// an IPv6 address is written next to a port, so the brackets are taken off
// rather than refused.
//
// `0.0.0.0` and `::` are not special-cased. They parse, they are not loopback,
// and so they already fall under the token requirement below, which is the
// correct answer for them: binding every interface at once is the most
// exposing choice available and needs the same deliberate act as naming one.
[[nodiscard]] std::optional<QHostAddress> parsedBindAddress(
    const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return std::nullopt;
  const qsizetype scope = trimmed.indexOf(QLatin1Char('%'));
  QString literal = scope < 0 ? trimmed : trimmed.left(scope);
  if (literal.size() >= 2 && literal.startsWith(QLatin1Char('[')) &&
      literal.endsWith(QLatin1Char(']'))) {
    literal = literal.mid(1, literal.size() - 2);
  }
  QHostAddress address;
  if (!address.setAddress(literal)) return std::nullopt;
  if (scope >= 0) address.setScopeId(trimmed.mid(scope + 1));
  return address;
}

// Which of the requested addresses are not currently listening, as one
// sentence for the status line, or nothing when they all came up.
//
// Recomputed from the two lists rather than remembered. The precise reason an
// address failed -- "address already in use", "can't assign requested address"
// -- is reported by `restart` at the moment it happens, which is when the
// operator is looking at the settings page and can act on it. Afterwards, when
// the status line is rebuilt because an observer connected, the reason is gone
// but the fact is not, and the fact is the part that must not disappear: a
// service listening on one of the two addresses that were asked for must never
// read as a service that is fully up.
[[nodiscard]] QStringList unboundAddressNotes(const QStringList& requested,
                                              const QStringList& bound) {
  QStringList missing;
  for (const QString& text : requested) {
    const std::optional<QHostAddress> address = parsedBindAddress(text);
    const QString prefix =
        address ? displayHost(*address) + QLatin1Char(':') : QString();
    bool listening = false;
    for (const QString& endpoint : bound) {
      listening =
          listening || (!prefix.isEmpty() && endpoint.startsWith(prefix));
    }
    if (!listening) missing.append(address ? displayHost(*address)
                                          : quotedForStatus(text));
  }
  if (missing.isEmpty()) return {};
  return {QStringLiteral("Not listening on %1.")
              .arg(missing.join(QStringLiteral(", ")))};
}

// The operator-facing status line, rebuilt from state whenever any part of it
// changes so that it always answers the same questions in the same order:
// what is bound, what is not, and who is watching.
//
// `detail` carries the one thing the state cannot recover afterwards -- the
// exact reason for the thing that just happened -- and is dropped on the next
// rebuild.
[[nodiscard]] QString statusLine(const QStringList& bound,
                                 const QStringList& notes,
                                 const int client_count,
                                 const QString& detail) {
  QStringList parts;
  parts.append(bound.isEmpty()
                   ? QStringLiteral("Diagnostics stream is not listening.")
                   : QStringLiteral("Diagnostics stream is listening on %1.")
                         .arg(bound.join(QStringLiteral(", "))));
  parts.append(notes);
  if (!bound.isEmpty()) {
    parts.append(client_count == 0
                     ? QStringLiteral("No observer is connected.")
                     : client_count == 1
                           ? QStringLiteral("One observer is connected.")
                           : QStringLiteral("%1 observers are connected.")
                                 .arg(client_count));
  }
  if (!detail.isEmpty()) parts.append(detail);
  return parts.join(QLatin1Char(' '));
}

// Whether the token a client presented is the configured one, without letting
// the time taken say how much of it was right.
//
// The length is compared first and does leak; the length of a token is not the
// secret, and two byte strings of different length cannot be compared in the
// same number of steps anyway. Past that every byte is examined: the loop
// accumulates differences with OR over XOR and tests the accumulator once, at
// the end. Returning on the first mismatch would turn a token of any length
// into a byte-at-a-time search, and this comparison sits on a socket that a
// LAN can reach.
[[nodiscard]] bool equalsInConstantTime(const QByteArray& presented,
                                        const QByteArray& expected) {
  if (presented.size() != expected.size()) return false;
  unsigned char difference = 0;
  for (qsizetype index = 0; index < expected.size(); ++index) {
    difference = static_cast<unsigned char>(
        difference | (static_cast<unsigned char>(presented.at(index)) ^
                      static_cast<unsigned char>(expected.at(index))));
  }
  return difference == 0;
}

// The first line an authenticated peer receives.
//
// It exists to say what this stream is, and the honesty of what it says is the
// point of it. It is also the first byte this service sends: nothing is
// written before the token has been accepted, so a peer that cannot
// authenticate learns only that something is listening.
//
// `emitOnly` stays true and now means exactly what it says for every byte
// after the handshake. One line is read from a client -- the token, before
// this greeting -- and nothing received afterwards is read or acted on. No
// `challenge` field is sent: the exchange is one line in one direction, not a
// challenge, and a field named for something else would misdescribe it. The
// token itself is never transmitted, because sending the operator's secret to
// a peer would hand out the only thing protecting a bound routable address.
//
// THE TOKEN NOW GATES BOTH BINDING AND ACCESS. It still decides whether a
// routable address may be bound at all, and it is additionally required from
// every client on every connection. It remains one shared secret: it does not
// tell one reader from another and cannot shut out a single reader, so what an
// operator is choosing when they bind a routable address is still that the
// station becomes readable by anything that can reach it AND holds the token.
// That is why the settings page describes enabling it as exposing the station,
// and why the main window shows an indicator for as long as it listens.
//
// `token_presented` says which of the two ways this client authenticated, so
// that the notice describes what actually happened rather than the common
// case. A stream with no token configured is a stream every bound address of
// which is loopback; the binding rule below is what makes that true.
[[nodiscard]] QByteArray greetingLine(const bool token_presented) {
  QJsonObject greeting;
  greeting.insert(QStringLiteral("event"),
                  QStringLiteral("diagnostics-stream-open"));
  greeting.insert(QStringLiteral("protocol"), kStreamProtocolVersion);
  greeting.insert(QStringLiteral("emitOnly"), true);
  greeting.insert(QStringLiteral("authenticated"), true);
  greeting.insert(QStringLiteral("openedUnixMs"),
                  QDateTime::currentMSecsSinceEpoch());
  greeting.insert(
      QStringLiteral("notice"),
      token_presented
          ? QStringLiteral(
                "This stream emits records to authenticated observers. Exactly "
                "one line is ever read from a client -- the access token, "
                "presented before this greeting -- and nothing sent after it "
                "is read or acted on: a client that keeps sending is "
                "disconnected. The token is one shared secret, and it also "
                "decides which addresses this stream may be bound to.")
          : QStringLiteral(
                "This stream only emits. No access token is configured, which "
                "is possible only because every bound address is loopback, so "
                "this connection was authenticated without reading anything. "
                "Nothing sent to this port is read or acted on: a client that "
                "keeps sending is disconnected."));
  QByteArray line = QJsonDocument(greeting).toJson(QJsonDocument::Compact);
  line.append('\n');
  return line;
}

// Whether one rule means "no restriction".
//
// `0.0.0.0/0` and `::/0` are answered here rather than left to the subnet
// comparison below, because `isInSubnet` only ever compares inside one family:
// `0.0.0.0/0` would admit every IPv4 peer and refuse every IPv6 one, and an
// operator who writes either spelling means anybody. `any` is the word the
// settings page offers for the same choice, matched without regard to case
// because it is typed by hand.
[[nodiscard]] bool ruleAllowsEveryPeer(const QString& rule) {
  return rule.compare(QLatin1String("any"), Qt::CaseInsensitive) == 0 ||
         rule == QLatin1String("0.0.0.0/0") || rule == QLatin1String("::/0");
}

// Every spelling of the connected peer that a rule may fairly be compared
// with: the address the socket reported and, when that address is an
// IPv4-mapped IPv6 one, the IPv4 address inside it.
//
// This is the case that decides whether the feature works at all. A socket
// bound to `::` accepts IPv4 clients and reports one as `::ffff:192.168.1.50`,
// so an operator's rule for the network that client is genuinely on --
// `192.168.1.0/24` -- matches nothing at all unless the mapping is undone
// first. Binding `::` is the ordinary choice, which would make this the
// ordinary outcome: a peer list that looked configured and admitted nobody.
// Silently having no effect is the one way an access rule must never fail.
[[nodiscard]] QList<QHostAddress> peerAddressSpellings(
    const QHostAddress& peer) {
  QList<QHostAddress> spellings;
  spellings.append(peer);
  if (peer.protocol() == QAbstractSocket::IPv6Protocol) {
    bool mapped = false;
    const quint32 inner = peer.toIPv4Address(&mapped);
    // True only for a mapped (or v4-compatible) address; every other IPv6
    // address has no IPv4 form and gains no second spelling here.
    if (mapped) spellings.append(QHostAddress(inner));
  }
  return spellings;
}

// Whether one rule admits this peer. A rule carrying a prefix is a subnet and
// a rule without one is a single address; Qt parses both forms for IPv4 and
// IPv6 alike, which is why neither family needs any code of its own here.
//
// An unreadable rule admits nothing. A typo must never widen access, so it
// matches nobody rather than being read as a wildcard -- and `parseSubnet` is
// asked only about text that actually carries a prefix, because it also
// accepts abbreviated IPv4 forms, under which the half-typed `192.168` would
// become `192.168.0.0/16` and admit sixty-five thousand addresses nobody
// named.
[[nodiscard]] bool ruleAdmitsPeer(const QString& rule,
                                  const QList<QHostAddress>& spellings) {
  if (rule.contains(QLatin1Char('/'))) {
    const auto [network, prefix] = QHostAddress::parseSubnet(rule);
    // How `parseSubnet` reports text it could not read, a prefix length that
    // does not exist for the family given (`/99` on IPv4) included.
    if (prefix < 0 || network.isNull()) return false;
    for (const QHostAddress& spelling : spellings) {
      if (spelling.isInSubnet(network, prefix)) return true;
    }
    return false;
  }
  QHostAddress exact;
  if (!exact.setAddress(rule)) return false;
  for (const QHostAddress& spelling : spellings) {
    // Compared as addresses and never as text, so that `::1` and
    // `0:0:0:0:0:0:0:1` are the one address they are. Only the IPv4-mapped
    // conversion is enabled: Qt's tolerant default would additionally equate
    // `::1` with `127.0.0.1`, and a rule should admit what it says and nothing
    // beyond it.
    if (spelling.isEqual(exact, QHostAddress::ConvertV4MappedToIPv4)) {
      return true;
    }
  }
  return false;
}

// Why a peer was turned away, for the status line. The address is named,
// because an operator who has mis-set a subnet can otherwise see only that
// nothing connects; and the rules are named with it, because the address alone
// does not say which of them failed to cover it. Both go through
// `quotedForStatus`, the same bounded printable-ASCII treatment every other
// address in this line gets.
[[nodiscard]] QString peerRefusalDetail(const QHostAddress& peer,
                                        const QStringList& patterns) {
  const QString address = quotedForStatus(peer.toString());
  if (patterns.isEmpty()) {
    return QStringLiteral(
               "A connection from %1 was refused: no allowed peers are set, "
               "so only this computer may watch.")
        .arg(address);
  }
  return QStringLiteral(
             "A connection from %1 was refused: it matches none of the "
             "allowed peers (%2).")
      .arg(address, quotedForStatus(patterns.join(QStringLiteral(", "))));
}

}  // namespace

// One connected observer. Once it is authenticated nothing here describes what
// the peer sent, only how much of it was thrown away, because from that point
// the content is never looked at.
//
// Before that, `handshake_bytes` holds the one line this class ever reads. It
// is bounded by `kMaximumHandshakeBytes`, it is released the moment the token
// is accepted, and nothing else is ever put in it -- so after the handshake
// there is no buffer of peer input anywhere in this class for a parser to be
// attached to later.
struct DiagnosticsServer::Client {
  QTcpSocket* socket{nullptr};
  // Runs only while this client still owes a token, and is stopped the moment
  // one is accepted. A connection that is opened and then says nothing must
  // not hold one of the few slots this service has for ever.
  QTimer* handshake_timer{nullptr};
  QByteArray handshake_bytes;
  qint64 ignored_input_bytes{0};
  bool authenticated{false};
};

DiagnosticsServer::DiagnosticsServer(QObject* parent) : QObject(parent) {
  status_message_ = QStringLiteral("Diagnostics stream is off.");
}

DiagnosticsServer::~DiagnosticsServer() {
  // Torn down by hand rather than through `restart`, which reports the change
  // to the UI: a destructor is not the place to emit state nobody can act on.
  // The sockets and servers are children of this object, so Qt closes and
  // deletes them; only the client records are ours to free.
  qDeleteAll(clients_);
  clients_.clear();
}

bool DiagnosticsServer::listening() const noexcept {
  return !servers_.isEmpty();
}

int DiagnosticsServer::clientCount() const noexcept {
  return static_cast<int>(clients_.size());
}

const QString& DiagnosticsServer::statusMessage() const noexcept {
  return status_message_;
}

QStringList DiagnosticsServer::boundAddresses() const {
  return bound_addresses_;
}

QVariantList DiagnosticsServer::discoverLocalAddresses() {
  QVariantList entries;
  const QList<QNetworkInterface> interfaces =
      QNetworkInterface::allInterfaces();
  for (const QNetworkInterface& interface : interfaces) {
    // An interface that is down has no address worth offering: binding it
    // would fail, and listing it would invite the operator to choose the one
    // thing that cannot work.
    if (!interface.flags().testFlag(QNetworkInterface::IsUp)) continue;
    const bool loopback_interface =
        interface.flags().testFlag(QNetworkInterface::IsLoopBack);
    for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
      const QHostAddress address = entry.ip();
      const QAbstractSocket::NetworkLayerProtocol protocol =
          address.protocol();
      if (protocol != QAbstractSocket::IPv4Protocol &&
          protocol != QAbstractSocket::IPv6Protocol) {
        continue;
      }
      // Loopback is listed too. It is the address most operators should pick
      // -- a stream nothing outside this machine can reach -- and leaving it
      // out would hide the safe choice while offering the exposing ones.
      const bool loopback = address.isLoopback() || loopback_interface;
      QVariantMap map;
      map.insert(QStringLiteral("address"), address.toString());
      map.insert(QStringLiteral("interfaceName"), interface.name());
      map.insert(QStringLiteral("loopback"), loopback);
      // Says what choosing this address means, not what the adapter is
      // called. The operator is deciding who may read the station's internal
      // state, and the hardware name does not answer that question.
      const QString hardware = interface.humanReadableName().isEmpty()
                                   ? interface.name()
                                   : interface.humanReadableName();
      map.insert(
          QStringLiteral("description"),
          loopback
              ? QStringLiteral("%1: reachable only from this computer.")
                    .arg(hardware)
              : QStringLiteral(
                    "%1: reachable by anything on that network. Needs an "
                    "access token.")
                    .arg(hardware));
      entries.append(map);
    }
  }
  return entries;
}

bool DiagnosticsServer::isAcceptableToken(const QString& token) {
  // Tested on the text exactly as given, before any trimming could hide a
  // control character at either end.
  if (token.size() < kMinimumTokenLength) return false;
  for (const QChar character : token) {
    const char16_t code = character.unicode();
    // No control characters, and therefore no carriage return or newline.
    // Those two matter most: this is a line-delimited protocol, and a token
    // carrying a newline would be a value that cannot be written down, read
    // back, or compared without changing shape somewhere along the way.
    if (code < kFirstPrintableAscii) return false;
    if (code >= kDeleteAscii && code <= kHighestC1Control) return false;
  }
  return true;
}

void DiagnosticsServer::configure(const QStringList& bind_addresses,
                                  const std::uint16_t port,
                                  const QString& access_token) {
  bind_addresses_ = bind_addresses;
  port_ = port;
  access_token_ = access_token;
  if (!enabled_) return;
  // Rebound unconditionally, even when the settings are unchanged. Applying
  // the same values again is how an operator retries after fixing the network
  // underneath, and a configure that quietly did nothing would leave a service
  // that never comes back up. The cost is that connected observers are
  // dropped; they are observers, and they can reconnect.
  restart();
}

void DiagnosticsServer::setAllowedPeers(const QStringList& patterns) {
  allowed_peers_ = patterns;
  // Applied to the peers already attached, not only to the next one to
  // arrive. A rule an operator narrows while somebody is connected is most
  // likely being narrowed because of who is connected, and a tightening that
  // left that reader attached would not be a tightening at all -- it would
  // leave the station readable by exactly the peer just excluded, for as long
  // as that peer chose to stay.
  //
  // Iterated over a copy, because `dropClient` removes from `clients_`, and
  // membership is rechecked so that a record already taken away cannot be
  // reached a second time.
  const QList<Client*> attached = clients_;
  for (Client* const client : attached) {
    if (!clients_.contains(client)) continue;
    QTcpSocket* const socket = client->socket;
    if (socket == nullptr) continue;
    const QHostAddress peer = socket->peerAddress();
    if (peerIsAllowed(peer, allowed_peers_)) continue;
    dropClient(socket,
               QStringLiteral("One observer was disconnected: %1 is no longer "
                              "an allowed peer.")
                   .arg(quotedForStatus(peer.toString())));
  }
}

const QStringList& DiagnosticsServer::allowedPeers() const noexcept {
  return allowed_peers_;
}

bool DiagnosticsServer::peerIsAllowed(const QHostAddress& peer,
                                      const QStringList& patterns) {
  const QList<QHostAddress> spellings = peerAddressSpellings(peer);

  // Blank entries are dropped before anything is decided, because an empty
  // settings field reaches this as one empty string rather than as no strings
  // and the two have to mean the same thing. Text that is not blank stays,
  // even when it is not a readable rule: an unreadable rule matches nobody,
  // and leaving it in the list keeps the list non-empty so that the loopback
  // default below can never be reached through a typo.
  QStringList rules;
  for (const QString& pattern : patterns) {
    const QString rule = pattern.trimmed();
    if (!rule.isEmpty()) rules.append(rule);
  }

  // Nothing has been said. Loopback only: the tightest answer available, and
  // the one that matches the default binding, because a service nobody has
  // configured should be reachable from nowhere but the machine running it.
  if (rules.isEmpty()) {
    for (const QHostAddress& spelling : spellings) {
      if (spelling.isLoopback()) return true;
    }
    return false;
  }

  // A list of permissions, so the first rule that admits this peer settles the
  // question. No rule here can deny: one that does not match has nothing to
  // say about this peer, which is why a mistyped entry beside a good one costs
  // only itself.
  for (const QString& rule : std::as_const(rules)) {
    if (ruleAllowsEveryPeer(rule)) return true;
    if (ruleAdmitsPeer(rule, spellings)) return true;
  }
  return false;
}

void DiagnosticsServer::setEnabled(const bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  // Both directions go through the same path. `restart` closes every server
  // and every client first, so switching this off is the same teardown as
  // rebinding, and there is only one place where that teardown is written.
  restart();
}

bool DiagnosticsServer::enabled() const noexcept { return enabled_; }

void DiagnosticsServer::publish(const QJsonObject& record) {
  // Compacted here, in whichever thread produced the record, so the thread
  // that owns this object -- the one also running the user interface -- pays
  // only for the write.
  QByteArray payload = QJsonDocument(record).toJson(QJsonDocument::Compact);
  payload.append('\n');

  auto deliver = [this, payload] {
    if (clients_.isEmpty()) return;
    // Iterated over a copy, because dropping a client below removes it from
    // `clients_`. Membership is rechecked before each use so that a record
    // published to a client that has just been dropped cannot reach a record
    // that no longer exists.
    const QList<Client*> targets = clients_;
    for (Client* const client : targets) {
      if (!clients_.contains(client)) continue;
      QTcpSocket* const socket = client->socket;
      if (socket == nullptr) continue;
      if (socket->state() != QAbstractSocket::ConnectedState) continue;
      // A client that has not presented the token receives nothing. It holds a
      // slot and a socket, and that is all it holds: no record reaches a peer
      // this service has not authenticated.
      if (!client->authenticated) continue;
      // A backlog this size means the peer has stopped reading. Disconnecting
      // it loses one observer; buffering for it would grow this process for as
      // long as it stays away, and this process is the one holding the
      // station's decoder.
      if (socket->bytesToWrite() + payload.size() >
          kMaximumClientBacklogBytes) {
        dropClient(socket,
                   QStringLiteral("One observer was disconnected: it stopped "
                                  "reading and the records for it had piled "
                                  "up."));
        continue;
      }
      socket->write(payload);
    }
  };

  if (QThread::currentThread() == thread()) {
    deliver();
    return;
  }
  // Marshalled onto the thread that owns the sockets. Nothing in this class
  // takes a lock: the client list is touched only from that thread, and this
  // queued call is the only door into it from anywhere else.
  QMetaObject::invokeMethod(this, deliver, Qt::QueuedConnection);
}

void DiagnosticsServer::acceptPending(QTcpServer* server) {
  if (server == nullptr) return;
  while (server->hasPendingConnections()) {
    QTcpSocket* const socket = server->nextPendingConnection();
    if (socket == nullptr) break;

    // The first question asked of a peer, and the only one answerable without
    // it having said anything: the address is known from the socket itself.
    // So this comes before the greeting, before the handshake deadline, and
    // before this connection counts toward `kMaximumClients` -- a peer that
    // may not connect costs nothing, occupies no slot, and cannot crowd out
    // the observers that are allowed.
    //
    // It is told nothing: not even the one-line refusal the client limit below
    // can afford to send. That refusal goes to a peer that has done nothing
    // wrong; this one should not learn that a diagnostics stream is what is
    // behind the port, nor which protocol it speaks. The operator is told
    // instead, in the status line, which is where somebody will read it.
    const QHostAddress peer = socket->peerAddress();
    if (!peerIsAllowed(peer, allowed_peers_)) {
      socket->abort();
      socket->deleteLater();
      setStatus(statusLine(
          bound_addresses_,
          unboundAddressNotes(bind_addresses_, bound_addresses_),
          clientCount(), peerRefusalDetail(peer, allowed_peers_)));
      emit stateChanged();
      continue;
    }

    if (clients_.size() >= kMaximumClients) {
      // Refused, and told so. Each observer costs a buffer, and a diagnostics
      // stream that accepted every peer that asked would be a way to exhaust
      // the station it exists to help diagnose. The refusal is written as one
      // record in the same dialect as the stream, because a peer that speaks
      // this protocol can then report why it was turned away instead of
      // showing an empty connection that closed.
      QJsonObject refusal;
      refusal.insert(QStringLiteral("event"),
                     QStringLiteral("diagnostics-stream-refused"));
      refusal.insert(QStringLiteral("protocol"), kStreamProtocolVersion);
      refusal.insert(QStringLiteral("reason"),
                     QStringLiteral("Too many observers are already "
                                    "connected to this diagnostics stream."));
      refusal.insert(QStringLiteral("maximumClients"), kMaximumClients);
      QByteArray line = QJsonDocument(refusal).toJson(QJsonDocument::Compact);
      line.append('\n');
      socket->write(line);
      // Flushed before the close so the reason actually leaves this machine,
      // and deleted on the disconnection rather than immediately for the same
      // reason.
      socket->flush();
      socket->disconnectFromHost();
      connect(socket, &QTcpSocket::disconnected, socket,
              &QObject::deleteLater);
      // A refused peer is still a peer, and until the close completes this
      // socket would otherwise buffer whatever it keeps sending with nothing
      // draining it. Discarded here for the same two reasons as after a
      // handshake anywhere else: so the memory cannot grow, and so there is no
      // buffer of peer input left for a parser to be attached to later.
      //
      // This refusal is the one line an unauthenticated peer can be sent, and
      // it is the same line whether or not the peer holds the token, because
      // the limit is reached before the handshake. It cannot be used to tell a
      // valid token from an invalid one.
      connect(socket, &QTcpSocket::readyRead, socket,
              [socket] { socket->skip(socket->bytesAvailable()); });
      setStatus(statusLine(
          bound_addresses_,
          unboundAddressNotes(bind_addresses_, bound_addresses_),
          clientCount(),
          QStringLiteral("An observer was refused: at most %1 may watch at "
                         "once.")
              .arg(kMaximumClients)));
      emit stateChanged();
      continue;
    }

    // Reparented, so the client's lifetime no longer depends on the server
    // that happened to accept it. Rebinding deletes the servers, and a socket
    // still being drained must not disappear with one.
    socket->setParent(this);
    Client* const client = new Client{};
    client->socket = socket;
    clients_.append(client);

    // Whether this client owes a token. Asked of the configured token rather
    // than of the address it arrived on: an unacceptable token is also the
    // reason no routable address could be bound, so when there is none every
    // bound address is loopback and the peer is already on this machine.
    const bool token_required = isAcceptableToken(access_token_);

    // The only thing ever connected to `readyRead`, and the only place this
    // class reads a peer at all. Before authentication it takes exactly one
    // bounded line and compares it with the configured token; afterwards it
    // discards, counts, and decides whether to hang up. Neither half contains
    // a command, a verb or a dispatch, and nothing a peer sends can reach the
    // radio, the settings or the decoder. The comparison has precisely two
    // outcomes: this connection continues, or it ends.
    auto receive = [this, client] {
      QTcpSocket* const peer = client->socket;
      if (peer == nullptr) return;

      if (!client->authenticated) {
        // Bounded before it is read, not after. Never more than the handshake
        // allowance is taken off the socket, so a peer that never sends a
        // newline cannot grow this process by talking.
        const qint64 room =
            kMaximumHandshakeBytes -
            static_cast<qint64>(client->handshake_bytes.size());
        if (room > 0) {
          client->handshake_bytes.append(
              peer->read(std::min<qint64>(room, peer->bytesAvailable())));
        }
        const qsizetype newline = client->handshake_bytes.indexOf('\n');
        if (newline < 0) {
          if (static_cast<qint64>(client->handshake_bytes.size()) <
              kMaximumHandshakeBytes) {
            return;
          }
          // The allowance is spent and no line ever arrived. Whatever this
          // peer is speaking, it is not this protocol's one line of token.
          dropClient(peer,
                     QStringLiteral("One observer was disconnected: it sent "
                                    "%1 bytes without presenting an access "
                                    "token.")
                         .arg(kMaximumHandshakeBytes));
          return;
        }
        QByteArray presented = client->handshake_bytes.left(newline);
        // A trailing carriage return is tolerated, because a token typed into
        // a telnet-style client arrives with one and the operator cannot see
        // it. Nothing else is trimmed: the token is compared as it was given,
        // which is also how `isAcceptableToken` judged it.
        if (presented.endsWith('\r')) presented.chop(1);
        const bool accepted =
            equalsInConstantTime(presented, access_token_.toUtf8());
        if (!accepted) {
          // Nothing is written back. The operator is told in the status line,
          // which is where somebody will actually read it; the peer is told by
          // the close, and is told nothing else.
          dropClient(peer,
                     QStringLiteral("One observer was disconnected: it "
                                    "presented the wrong access token. It "
                                    "received no records."));
          return;
        }
        // Authenticated. The buffer is released here and never refilled.
        const qint64 trailing =
            static_cast<qint64>(client->handshake_bytes.size()) -
            (static_cast<qint64>(newline) + 1);
        client->handshake_bytes = QByteArray();
        if (client->handshake_timer != nullptr) {
          client->handshake_timer->stop();
          client->handshake_timer->deleteLater();
          client->handshake_timer = nullptr;
        }
        client->authenticated = true;
        client->ignored_input_bytes += trailing;
        peer->write(greetingLine(true));
        setStatus(statusLine(
            bound_addresses_,
            unboundAddressNotes(bind_addresses_, bound_addresses_),
            clientCount(),
            QStringLiteral("An observer presented the access token.")));
        emit stateChanged();
        // Falls through, so that anything the peer queued behind its token
        // line is discarded and counted exactly like every byte it sends
        // later. The handshake is over for good; there is no second line.
      }

      // `skip` rather than `readAll`: the bytes are dropped by the device
      // without ever being handed to this class as a value. There is no
      // buffer here for a parser to be added to later.
      const qint64 available = peer->bytesAvailable();
      const qint64 discarded = peer->skip(available);
      if (discarded > 0) client->ignored_input_bytes += discarded;
      if (client->ignored_input_bytes <= kMaximumIgnoredInputBytes) return;
      // A peer sending this much has mistaken the port for something that
      // answers -- a rigctld, a telnet session, a REST endpoint. Saying so by
      // hanging up is kinder than letting it wait for a reply that this
      // service will never send.
      dropClient(peer,
                 QStringLiteral("One observer was disconnected: it sent data "
                                "to a stream that only emits. Nothing it sent "
                                "was read."));
    };
    connect(socket, &QTcpSocket::readyRead, this, receive);
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
      dropClient(socket, QStringLiteral("An observer disconnected."));
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, socket](const QAbstractSocket::SocketError) {
              // The peer's error string, not an interpretation of it. This end
              // did nothing and has nothing to explain.
              dropClient(socket, QStringLiteral("An observer was lost: %1.")
                                     .arg(socket->errorString()));
            });

    if (token_required) {
      // A deadline, not a poll. A peer that opens a connection and then says
      // nothing costs one of the few slots this service has, and a station
      // must not lose its own diagnostics to a client that never speaks.
      QTimer* const deadline = new QTimer(socket);
      deadline->setSingleShot(true);
      deadline->setInterval(kHandshakeTimeoutMs);
      // Captures the socket, not the client record, so that a timeout which
      // arrives after this client was dropped for some other reason looks for
      // it, does not find it, and does nothing.
      connect(deadline, &QTimer::timeout, this, [this, socket] {
        dropClient(socket,
                   QStringLiteral("One observer was disconnected: it did not "
                                  "present an access token within %1 seconds.")
                       .arg(kHandshakeTimeoutMs / 1000));
      });
      client->handshake_timer = deadline;
      deadline->start();
    } else {
      // No token is configured, which the binding rule in `restart` permits
      // only when every bound address is loopback. There is nothing to
      // present and nothing outside this machine able to present it, so the
      // connection is authenticated immediately.
      client->authenticated = true;
      socket->write(greetingLine(false));
    }

    setStatus(
        statusLine(bound_addresses_,
                   unboundAddressNotes(bind_addresses_, bound_addresses_),
                   clientCount(), QString()));
    emit stateChanged();

    // A socket handed over by `nextPendingConnection` can already be holding
    // the peer's first bytes, and `readyRead` is not emitted again for bytes
    // that arrived before the connection above was made -- a client that sent
    // its token immediately would otherwise wait out the deadline. Checked
    // after the status is published, so that a token rejected here reports its
    // own reason instead of having it overwritten by this accept.
    if (!client->authenticated && socket->bytesAvailable() > 0) receive();
  }
}

void DiagnosticsServer::dropClient(QTcpSocket* socket, const QString& reason) {
  if (socket == nullptr) return;
  qsizetype index = -1;
  for (qsizetype position = 0; position < clients_.size(); ++position) {
    if (clients_.at(position)->socket == socket) index = position;
  }
  // Already gone. This is reached from `disconnected`, from `errorOccurred`
  // and from the two limits above, and a socket that fails is perfectly
  // capable of arriving here twice.
  if (index < 0) return;
  Client* const client = clients_.takeAt(index);
  // Stopped before the record it belongs to is freed. The timer is a child of
  // the socket and dies with it below; stopping it here means a deadline can
  // never fire for a client that is already gone.
  if (client->handshake_timer != nullptr) {
    client->handshake_timer->stop();
    client->handshake_timer->disconnect(this);
    client->handshake_timer = nullptr;
  }
  delete client;

  // Detached before it is closed, so that a hang-up this class performed does
  // not come back through `disconnected` as news about the peer.
  socket->disconnect(this);
  // Reset rather than closed politely, and no parting record written. Every
  // limit that leads here is reached by a peer that is either not reading what
  // it is sent, not stopping sending, or not authenticating, and a graceful
  // close would hold the socket open -- still receiving, with nothing draining
  // it -- waiting on exactly the peer whose behaviour caused the
  // disconnection. A failed handshake gets the same treatment for a second
  // reason: a peer that guessed wrong is told by the close and told nothing
  // else. The refusal above can afford to explain itself because that peer has
  // done nothing wrong. The operator is told why in the status line, which is
  // where somebody will actually read it.
  socket->abort();
  socket->deleteLater();

  setStatus(statusLine(bound_addresses_,
                       unboundAddressNotes(bind_addresses_, bound_addresses_),
                       clientCount(), reason));
  emit stateChanged();
}

void DiagnosticsServer::restart() {
  for (Client* const client : std::as_const(clients_)) {
    QTcpSocket* const socket = client->socket;
    // Before the record is freed, for the same reason as in `dropClient`.
    if (client->handshake_timer != nullptr) client->handshake_timer->stop();
    if (socket != nullptr) {
      socket->disconnect(this);
      socket->abort();
      socket->deleteLater();
    }
    delete client;
  }
  clients_.clear();
  for (QTcpServer* const server : std::as_const(servers_)) {
    server->disconnect(this);
    server->close();
    server->deleteLater();
  }
  servers_.clear();
  bound_addresses_.clear();

  if (!enabled_) {
    setStatus(QStringLiteral("Diagnostics stream is off."));
    emit stateChanged();
    return;
  }
  if (bind_addresses_.isEmpty()) {
    setStatus(QStringLiteral(
        "Choose an address before starting the diagnostics stream. 127.0.0.1 "
        "keeps it on this computer."));
    emit stateChanged();
    return;
  }

  // Asked once, outside the loop, because the answer is about the token and
  // not about any one address.
  const bool token_acceptable = isAcceptableToken(access_token_);
  QStringList notes;
  for (const QString& requested : std::as_const(bind_addresses_)) {
    const std::optional<QHostAddress> address = parsedBindAddress(requested);
    if (!address) {
      notes.append(QStringLiteral("%1 is not an address this computer can "
                                  "bind.")
                       .arg(quotedForStatus(requested)));
      continue;
    }
    // The token requirement, and the one rule in this class that refuses
    // something an operator asked for. A routable address publishes the
    // station's internal state to everything that can reach it, and the token
    // is the only evidence that this was meant; without one, the address is
    // not bound and the status says why rather than coming up quietly.
    if (!address->isLoopback() && !token_acceptable) {
      notes.append(
          QStringLiteral(
              "%1 was not bound: an address other than loopback needs an "
              "access token of at least %2 characters, because binding it "
              "makes this station readable from the network.")
              .arg(displayHost(*address))
              .arg(kMinimumTokenLength));
      continue;
    }
    // One server per address, never a wildcard standing in for several. The
    // operator named the interfaces they meant, and binding anything wider
    // than that would be this class deciding to expose more than it was asked
    // to.
    QTcpServer* const server = new QTcpServer(this);
    if (!server->listen(*address, port_)) {
      // Named, and the others still come up. A typo in one interface, or one
      // address already taken by something else, must not silently take the
      // whole service down.
      notes.append(QStringLiteral("%1 could not be bound: %2.")
                       .arg(displayEndpoint(*address, port_),
                            server->errorString()));
      server->deleteLater();
      continue;
    }
    connect(server, &QTcpServer::newConnection, this,
            [this, server] { acceptPending(server); });
    servers_.append(server);
    // `serverPort` rather than the requested port, so that the endpoint shown
    // to the operator is the one a tool can actually be pointed at.
    bound_addresses_.append(displayEndpoint(*address, server->serverPort()));
  }

  setStatus(statusLine(bound_addresses_, notes, clientCount(), QString()));
  emit stateChanged();
}

void DiagnosticsServer::setStatus(QString message) {
  status_message_ = std::move(message);
}

}  // namespace cwassistant::desktop
