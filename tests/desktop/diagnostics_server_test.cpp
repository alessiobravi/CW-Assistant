// Guards the diagnostics stream's safety properties.
//
// This process holds transmit, so the one thing that must never become true of
// this class is that a byte from a peer can select an action. Exactly one
// thing is read from a client -- the access token, once, as a single bounded
// line before anything is sent back -- and the cases below pin both halves of
// that: an unauthenticated peer receives no record, and an authenticated one
// that keeps talking is disconnected without its bytes being looked at. Every
// other property here bounds what a connected observer can cost the station it
// is meant to help diagnose. Distinct nonzero exit codes; each comment says
// what an operator loses.
//
// The peer rules are the stronger and cheaper of the two access controls, and
// most of them need no socket: `peerIsAllowed` is pure, so the cases below ask
// it directly what a list permits. The two that do use a socket pin the part a
// pure function cannot -- that a disallowed peer is closed at accept with
// nothing written to it, and that narrowing the list drops whoever it no
// longer covers.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

#include "diagnostics/diagnostics_server.hpp"

namespace {

using cwassistant::desktop::DiagnosticsServer;

void pump(const int milliseconds) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
}

// The port out of the first bound endpoint. The LAST colon, deliberately: an
// IPv6 endpoint is reported as `[::1]:17300`, where the address is bracketed
// precisely so that the colon before the port is the final one. Do not
// "correct" this to the first colon -- that reads `[` and breaks every IPv6
// case below, which is the reason the bracketed form exists.
[[nodiscard]] quint16 boundPort(const DiagnosticsServer& server) {
  const QStringList bound = server.boundAddresses();
  if (bound.isEmpty()) return 0;
  const int colon = bound.front().lastIndexOf(QLatin1Char(':'));
  return colon < 0
             ? 0
             : static_cast<quint16>(
                   bound.front().mid(colon + 1).toUInt());
}

// The token the handshake cases below present. Exactly the minimum acceptable
// length, so that a rule change which quietly raised the minimum would show up
// here, and written to nothing but a loopback socket.
[[nodiscard]] QString testToken() {
  return QStringLiteral("0123456789abcdef");
}

// A token that can be guessed is a token that was never asked for.
bool tokenRuleHolds() {
  if (DiagnosticsServer::isAcceptableToken(QString{})) return false;
  if (DiagnosticsServer::isAcceptableToken(QStringLiteral("short"))) {
    return false;
  }
  if (!DiagnosticsServer::isAcceptableToken(
          QStringLiteral("abcdefghijklmnop"))) {
    return false;
  }
  // Written to no socket, but a token carrying a line break would still be a
  // value that looks like two, and every other operator-supplied string in
  // this application refuses one.
  return !DiagnosticsServer::isAcceptableToken(
             QStringLiteral("abcdefghijklmnop\r\nmore")) &&
         !DiagnosticsServer::isAcceptableToken(
             QStringLiteral("abcdefghij\tklmnop"));
}

// The station's internals must not reach a routable address on the strength of
// a setting alone. The token gates which addresses may be bound, as well as
// which clients may read; this case is about the binding half, which is what
// keeps a routable address from being offered without a token at all.
bool refusesARoutableAddressWithoutAToken() {
  DiagnosticsServer server;
  // TEST-NET-3. Unassignable here, so this exercises the refusal without ever
  // putting an interface of the machine running the test on a socket.
  server.configure({QStringLiteral("203.0.113.7")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const bool refused = !server.listening() && server.boundAddresses().isEmpty();
  server.setEnabled(false);
  return refused;
}

// Loopback needs no token: nothing outside the machine can reach it.
bool bindsLoopbackWithoutAToken() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const bool listening = server.listening() && boundPort(server) != 0;
  server.setEnabled(false);
  return listening;
}

// The whole point of the feature: records reach every observer, one per line.
bool publishesToEveryObserver() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket first;
  QTcpSocket second;
  first.connectToHost(QHostAddress::LocalHost, port);
  second.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 2) return false;

  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(300);

  const QByteArray from_first = first.readAll();
  const QByteArray from_second = second.readAll();
  server.setEnabled(false);
  return from_first.contains("\"probe\"") && from_first.endsWith('\n') &&
         from_second.contains("\"probe\"");
}

// THE assertion. A peer that talks to this port is disconnected, and nothing
// it sent is ever parsed. A diagnostics channel that accepted input would be a
// second, weaker way to reach a radio.
bool disconnectsAPeerThatSends() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket talker;
  talker.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;

  talker.write(QByteArray(
      static_cast<int>(DiagnosticsServer::kMaximumIgnoredInputBytes) + 1, 'x'));
  talker.flush();
  pump(500);

  const bool dropped = server.clientCount() == 0;
  server.setEnabled(false);
  return dropped;
}

// A stream that let an unbounded number of peers attach would be a way to
// exhaust the station it exists to diagnose.
bool boundsTheNumberOfObservers() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  std::vector<QTcpSocket*> sockets;
  for (int index = 0; index < DiagnosticsServer::kMaximumClients + 1; ++index) {
    auto* socket = new QTcpSocket;
    socket->connectToHost(QHostAddress::LocalHost, port);
    sockets.push_back(socket);
  }
  pump(600);
  const bool bounded = server.clientCount() == DiagnosticsServer::kMaximumClients;
  server.setEnabled(false);
  for (auto* socket : sockets) delete socket;
  return bounded;
}

// Disabling must actually take the sockets down, or an operator who switched
// the service off would still be exposed.
bool disablingClosesEverything() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;
  QTcpSocket observer;
  observer.connectToHost(QHostAddress::LocalHost, port);
  pump(300);

  server.setEnabled(false);
  pump(300);
  return !server.listening() && server.clientCount() == 0 &&
         server.boundAddresses().isEmpty();
}

// The settings page can only offer what this reports, so it has to describe
// every address in terms the page renders.
bool describesLocalAddresses() {
  const QVariantList addresses = DiagnosticsServer::discoverLocalAddresses();
  if (addresses.isEmpty()) return false;
  bool saw_loopback = false;
  for (const QVariant& entry : addresses) {
    const QVariantMap map = entry.toMap();
    if (!map.contains(QStringLiteral("address")) ||
        !map.contains(QStringLiteral("interfaceName")) ||
        !map.contains(QStringLiteral("loopback"))) {
      return false;
    }
    if (map.value(QStringLiteral("address")).toString().isEmpty()) return false;
    if (map.value(QStringLiteral("loopback")).toBool()) saw_loopback = true;
  }
  // Every machine has one, and an operator who wants the stream reachable only
  // from this computer needs it offered.
  return saw_loopback;
}

// The handshake, from the client's side: nothing arrives before the token, and
// records arrive after it. The first half is the load-bearing one -- a greeting
// written at accept time would hand the station's protocol, and then its
// records, to a peer that has proved nothing.
bool authenticatesAndThenStreams() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, testToken());
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket observer;
  observer.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;

  // Published while this client still owes its token. Not one byte may reach
  // it: neither the greeting, which is written only on authentication, nor a
  // record, which `publish` must skip an unauthenticated client for.
  QJsonObject early;
  early.insert(QStringLiteral("event"), QStringLiteral("too-early"));
  server.publish(early);
  pump(300);
  if (observer.bytesAvailable() != 0) return false;

  observer.write(testToken().toUtf8() + '\n');
  observer.flush();
  pump(300);
  const QByteArray greeting = observer.readAll();
  if (!greeting.contains("\"diagnostics-stream-open\"")) return false;
  if (!greeting.contains("\"authenticated\":true")) return false;

  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(300);
  const QByteArray streamed = observer.readAll();
  server.setEnabled(false);
  return streamed.contains("\"probe\"") && streamed.endsWith('\n');
}

// A wrong token receives nothing at all -- no greeting, no record, no
// explanation -- and is disconnected. Both a same-length token and a truncated
// one are presented, because the two travel different paths through the
// comparison: one exercises the byte loop, the other the length check.
bool refusesAWrongToken() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, testToken());
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket guesser;
  guesser.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;
  // Sixteen characters, fifteen of them right.
  guesser.write(QByteArray("0123456789abcdeF\n"));
  guesser.flush();
  pump(400);
  if (server.clientCount() != 0) return false;

  QTcpSocket truncator;
  truncator.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;
  truncator.write(QByteArray("0123456789abcde\n"));
  truncator.flush();
  pump(400);
  if (server.clientCount() != 0) return false;

  // Published while both are gone. A record that reached either of them would
  // mean the stream had already been handed to a peer that guessed.
  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(200);
  const bool silent =
      guesser.readAll().isEmpty() && truncator.readAll().isEmpty();
  server.setEnabled(false);
  return silent;
}

// A connection that says nothing must not hold a slot for ever. There are four
// of them, so a peer that opens one and goes quiet -- or four that do -- would
// otherwise cost the station the diagnostics it opened the port for.
bool dropsASilentClientAfterTheHandshakeTimeout() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, testToken());
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket silent;
  silent.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;

  // Long enough to be past the deadline, and no longer.
  pump(DiagnosticsServer::kHandshakeTimeoutMs + 1000);
  const bool dropped = server.clientCount() == 0;
  const bool received_nothing = silent.readAll().isEmpty();
  server.setEnabled(false);
  return dropped && received_nothing;
}

// A peer that never sends a newline cannot grow this process by talking. The
// handshake buffer is the only place peer bytes are ever kept, and it is
// bounded before it is read into rather than after.
bool dropsAnOversizedHandshake() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, testToken());
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket flooder;
  flooder.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;

  flooder.write(QByteArray(
      static_cast<int>(DiagnosticsServer::kMaximumHandshakeBytes) + 1, 'x'));
  flooder.flush();
  // Well inside the handshake deadline, so this asserts the byte bound and not
  // the timeout: a build that only had the timer would still be holding this
  // client here.
  pump(500);
  const bool dropped = server.clientCount() == 0;
  const bool received_nothing = flooder.readAll().isEmpty();
  server.setEnabled(false);
  return dropped && received_nothing;
}

// An address the peer rules are asked about, written once so that the cases
// below read as the question they are asking.
[[nodiscard]] bool admits(const QString& peer, const QStringList& patterns) {
  return DiagnosticsServer::peerIsAllowed(QHostAddress(peer), patterns);
}

// No list means loopback only. An operator who has not said who may watch has
// not said "anyone", and a stream that read silence as permission would expose
// the station on the strength of a setting nobody touched.
bool anEmptyPeerListAdmitsOnlyLoopback() {
  if (!admits(QStringLiteral("127.0.0.1"), {})) return false;
  if (!admits(QStringLiteral("127.0.0.53"), {})) return false;
  if (!admits(QStringLiteral("::1"), {})) return false;
  if (admits(QStringLiteral("192.168.1.50"), {})) return false;
  if (admits(QStringLiteral("203.0.113.7"), {})) return false;
  // An empty settings field arrives as one empty string, not as no strings.
  // If that were read as a list with one unmatchable rule in it, an operator
  // who cleared the field would lock themselves out of their own machine.
  return admits(QStringLiteral("127.0.0.1"), {QString{}, QStringLiteral("  ")});
}

// The operator asked to be able to accept from anyone, and all three spellings
// of that have to work: `0.0.0.0/0` and `::/0` only cover their own family
// when handed to a subnet comparison, so the intent behind them is recognised
// rather than delegated.
bool anyAdmitsEveryPeer() {
  const QStringList spellings{QStringLiteral("any"), QStringLiteral("ANY"),
                              QStringLiteral("0.0.0.0/0"),
                              QStringLiteral("::/0")};
  for (const QString& spelling : spellings) {
    if (!admits(QStringLiteral("127.0.0.1"), {spelling})) return false;
    if (!admits(QStringLiteral("192.168.1.50"), {spelling})) return false;
    if (!admits(QStringLiteral("2001:db8::1"), {spelling})) return false;
  }
  return true;
}

// A bare address admits that address and no other, and is compared as an
// address rather than as text: `::1` and `0:0:0:0:0:0:0:1` are one address,
// and an operator whose rule stopped working because they wrote it out in full
// would have no way to see why.
bool anExactAddressAdmitsOnlyItself() {
  const QStringList one{QStringLiteral("192.168.1.50")};
  if (!admits(QStringLiteral("192.168.1.50"), one)) return false;
  if (admits(QStringLiteral("192.168.1.51"), one)) return false;
  if (admits(QStringLiteral("192.168.1.5"), one)) return false;
  const QStringList six{QStringLiteral("::1")};
  if (!admits(QStringLiteral("0:0:0:0:0:0:0:1"), six)) return false;
  if (admits(QStringLiteral("::2"), six)) return false;
  // The rule written out in full, which is how an operator may well type it,
  // against the short form a socket reports. Compared as text these are two
  // different rules, and the one the operator typed would never match
  // anything; they are one address. Both spellings of the pair are tried,
  // because Qt canonicalises whichever of them it parses and only this
  // direction can tell the two comparisons apart.
  if (!admits(QStringLiteral("::1"),
              {QStringLiteral("0:0:0:0:0:0:0:1")})) {
    return false;
  }
  // Hex case is not part of an address either.
  if (!admits(QStringLiteral("2001:db8::1"),
              {QStringLiteral("2001:DB8::1")})) {
    return false;
  }
  if (admits(QStringLiteral("2001:db8::2"),
             {QStringLiteral("2001:DB8::1")})) {
    return false;
  }
  // Two addresses, and both of them work: a list is a list.
  const QStringList pair{QStringLiteral("192.168.1.50"),
                         QStringLiteral("10.0.0.9")};
  return admits(QStringLiteral("192.168.1.50"), pair) &&
         admits(QStringLiteral("10.0.0.9"), pair) &&
         !admits(QStringLiteral("10.0.0.10"), pair);
}

// A subnet admits its own range and stops at the edge of it. This is the form
// an operator will actually use -- "my LAN may watch" -- so an off-by-one
// network boundary here would either shut out the station's own network or
// quietly include the one next to it.
bool aSubnetAdmitsItsOwnRange() {
  const QStringList lan{QStringLiteral("192.168.1.0/24")};
  if (!admits(QStringLiteral("192.168.1.50"), lan)) return false;
  if (!admits(QStringLiteral("192.168.1.1"), lan)) return false;
  if (!admits(QStringLiteral("192.168.1.255"), lan)) return false;
  if (admits(QStringLiteral("192.168.2.50"), lan)) return false;
  if (admits(QStringLiteral("192.168.0.50"), lan)) return false;
  return !admits(QStringLiteral("10.0.0.1"), lan);
}

// The same rule shape in IPv6, which takes no extra code and must therefore
// keep working: a claim that this supports both families is only worth
// something if one of them is tested.
bool anIpv6SubnetAdmitsItsOwnRange() {
  const QStringList documented{QStringLiteral("2001:db8::/32")};
  if (!admits(QStringLiteral("2001:db8::1"), documented)) return false;
  if (!admits(QStringLiteral("2001:db8:dead:beef::9"), documented)) {
    return false;
  }
  if (admits(QStringLiteral("2001:db9::1"), documented)) return false;
  return !admits(QStringLiteral("fe80::1"), documented);
}

// THE case that decides whether this feature does anything. A socket bound to
// `::` reports an IPv4 client as `::ffff:192.168.1.50`, and `::` is the
// ordinary binding, so a rule written for the network that client is really on
// must admit it. Without this the list would look configured and admit nobody
// -- the one way an access rule must never fail.
bool anIpv4MappedPeerMatchesAnIpv4Rule() {
  const QString mapped = QStringLiteral("::ffff:192.168.1.50");
  if (!admits(mapped, {QStringLiteral("192.168.1.0/24")})) return false;
  if (!admits(mapped, {QStringLiteral("192.168.1.50")})) return false;
  // And the mapping does not widen anything: the address inside it is still
  // the address being judged.
  if (admits(mapped, {QStringLiteral("192.168.2.0/24")})) return false;
  if (admits(mapped, {QStringLiteral("192.168.1.51")})) return false;
  // A mapped loopback client is still this machine, so the no-list default has
  // to recognise it too.
  return admits(QStringLiteral("::ffff:127.0.0.1"), {});
}

// A typo must never widen access. An unreadable rule is a rule that matches
// nobody -- never a wildcard, and never an excuse to fall back to a default
// that would admit somebody the operator did not name.
bool anUnreadableRuleAdmitsNothing() {
  const QStringList nonsense{QStringLiteral("not a subnet")};
  if (admits(QStringLiteral("192.168.1.50"), nonsense)) return false;
  if (admits(QStringLiteral("127.0.0.1"), nonsense)) return false;
  const QStringList impossible{QStringLiteral("192.168.1.0/99")};
  if (admits(QStringLiteral("192.168.1.50"), impossible)) return false;
  if (admits(QStringLiteral("127.0.0.1"), impossible)) return false;
  // Qt's subnet parser accepts abbreviated IPv4 forms, under which a
  // half-typed address becomes a /16. A rule with no prefix is an address, so
  // this is refused rather than silently turned into 65,536 of them.
  if (admits(QStringLiteral("192.168.1.50"), {QStringLiteral("192.168")})) {
    return false;
  }
  // A mistyped rule costs only itself: the good rule beside it still works,
  // and the bad one still admits nobody.
  const QStringList mixed{QStringLiteral("192.168.1.0/99"),
                          QStringLiteral("192.168.1.0/24")};
  return admits(QStringLiteral("192.168.1.50"), mixed) &&
         !admits(QStringLiteral("10.0.0.1"), mixed);
}

// The rule on a real socket. A peer outside the list is closed at accept and
// told nothing -- no greeting, no refusal, nothing that says what is behind the
// port -- and it never becomes an observer, so it cannot take one of the four
// slots from the readers that are allowed.
bool refusesADisallowedPeerWithoutAGreeting() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  // Loopback is not in this list, so the client below is refused by the rule
  // and not by anything else. Only 127.0.0.1 is ever bound.
  server.setAllowedPeers({QStringLiteral("192.168.1.0/24")});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket refused;
  refused.connectToHost(QHostAddress::LocalHost, port);
  pump(400);
  if (server.clientCount() != 0) return false;
  if (refused.bytesAvailable() != 0) return false;

  // Published with the refused peer's socket still open at its end. Nothing
  // may reach it.
  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(200);
  const bool silent = refused.readAll().isEmpty();
  // The operator has to be able to see what was turned away, or a mis-set
  // subnet looks exactly like a broken stream.
  const bool explained =
      server.statusMessage().contains(QStringLiteral("127.0.0.1")) &&
      server.statusMessage().contains(QStringLiteral("refused"));
  server.setEnabled(false);
  return silent && explained;
}

// Tightening the list drops the peers it no longer covers. A narrowed rule
// that left an existing reader attached would not be a tightening: it would
// leave the station readable by exactly the peer just excluded, for as long as
// that peer chose to stay.
bool tighteningTheListDropsAnAttachedPeer() {
  DiagnosticsServer server;
  server.configure({QStringLiteral("127.0.0.1")}, 0, QString{});
  server.setEnabled(true);
  pump(200);
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket observer;
  observer.connectToHost(QHostAddress::LocalHost, port);
  pump(300);
  if (server.clientCount() != 1) return false;
  if (!observer.readAll().contains("\"diagnostics-stream-open\"")) return false;

  server.setAllowedPeers({QStringLiteral("192.168.1.0/24")});
  pump(300);
  if (server.clientCount() != 0) return false;

  // A record published after the tightening must not reach the peer that was
  // just excluded.
  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(200);
  const bool cut_off = observer.readAll().isEmpty();
  // The settings page reads this back to tell an operator what their list
  // permits before the service is running.
  const bool remembered =
      server.allowedPeers() == QStringList{QStringLiteral("192.168.1.0/24")};
  server.setEnabled(false);
  return cut_off && remembered;
}

// Whether this machine can bind IPv6 loopback at all. A test that fails on a
// runner without IPv6 says nothing about this code, so the case below is
// skipped rather than allowed to fail spuriously.
[[nodiscard]] bool ipv6LoopbackIsAvailable() {
  QTcpServer probe;
  const bool bindable =
      probe.listen(QHostAddress(QHostAddress::LocalHostIPv6), 0);
  probe.close();
  return bindable;
}

// IPv6 end to end, because every part of it was written for both families and
// none of it was ever exercised on an IPv6 socket: the address is bound, the
// endpoint is reported in the bracketed form a tool can be pointed at, the
// token handshake completes over it, and a record arrives. The bracketed
// spelling `[::1]` is accepted as well, because that is the form an operator
// will copy out of the status line and paste back into the settings page.
bool bindsAndStreamsOverIpv6() {
  if (!ipv6LoopbackIsAvailable()) {
    std::printf(
        "diagnostics server: IPv6 loopback unavailable on this machine, "
        "IPv6 stream case skipped\n");
    return true;
  }

  DiagnosticsServer server;
  server.configure({QStringLiteral("::1")}, 0, testToken());
  server.setEnabled(true);
  pump(200);
  if (!server.listening()) return false;
  if (server.boundAddresses().isEmpty()) return false;
  // `[::1]:port`, not `::1:port`, which cannot be read back apart.
  if (!server.boundAddresses().front().startsWith(QStringLiteral("[::1]:"))) {
    return false;
  }
  const quint16 port = boundPort(server);
  if (port == 0) return false;

  QTcpSocket observer;
  observer.connectToHost(QHostAddress(QHostAddress::LocalHostIPv6), port);
  pump(400);
  if (server.clientCount() != 1) return false;
  // Nothing before the token, over IPv6 as over IPv4.
  if (observer.bytesAvailable() != 0) return false;

  observer.write(testToken().toUtf8() + '\n');
  observer.flush();
  pump(300);
  if (!observer.readAll().contains("\"diagnostics-stream-open\"")) return false;

  QJsonObject record;
  record.insert(QStringLiteral("event"), QStringLiteral("probe"));
  server.publish(record);
  pump(300);
  const QByteArray streamed = observer.readAll();
  server.setEnabled(false);
  pump(100);
  if (!streamed.contains("\"probe\"") || !streamed.endsWith('\n')) return false;

  // The bracketed literal is the same address, because that is how it is
  // written wherever a port follows it.
  DiagnosticsServer bracketed;
  bracketed.configure({QStringLiteral("[::1]")}, 0, testToken());
  bracketed.setEnabled(true);
  pump(200);
  const bool same = bracketed.listening() &&
                    !bracketed.boundAddresses().isEmpty() &&
                    bracketed.boundAddresses().front().startsWith(
                        QStringLiteral("[::1]:"));
  bracketed.setEnabled(false);
  return same;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  if (!tokenRuleHolds()) return 10;
  if (!refusesARoutableAddressWithoutAToken()) return 11;
  if (!bindsLoopbackWithoutAToken()) return 12;
  if (!publishesToEveryObserver()) return 13;
  if (!disconnectsAPeerThatSends()) return 14;
  if (!boundsTheNumberOfObservers()) return 15;
  if (!disablingClosesEverything()) return 16;
  if (!describesLocalAddresses()) return 17;
  if (!authenticatesAndThenStreams()) return 18;
  if (!refusesAWrongToken()) return 19;
  if (!dropsASilentClientAfterTheHandshakeTimeout()) return 20;
  if (!dropsAnOversizedHandshake()) return 21;
  if (!anEmptyPeerListAdmitsOnlyLoopback()) return 22;
  if (!anyAdmitsEveryPeer()) return 23;
  if (!anExactAddressAdmitsOnlyItself()) return 24;
  if (!aSubnetAdmitsItsOwnRange()) return 25;
  if (!anIpv6SubnetAdmitsItsOwnRange()) return 26;
  if (!anIpv4MappedPeerMatchesAnIpv4Rule()) return 27;
  if (!anUnreadableRuleAdmitsNothing()) return 28;
  if (!refusesADisallowedPeerWithoutAGreeting()) return 29;
  if (!tighteningTheListDropsAnAttachedPeer()) return 30;
  if (!bindsAndStreamsOverIpv6()) return 31;
  std::printf("diagnostics server: all checks passed\n");
  return 0;
}
