#include "dx_cluster_client.hpp"

#include <QAbstractSocket>
#include <QChar>
#include <QDate>
#include <QDateTime>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::desktop {
namespace {

// The frequency a cluster spot may claim, in hertz. A cluster quotes
// kilohertz, so a line that is actually reporting hertz, or one whose
// frequency field has been mangled, lands orders of magnitude outside these
// bounds rather than slightly outside them. Below the first there is no
// amateur allocation at all, and above the second nothing a CW spot describes.
using cwassistant::core::kMaximumSpotFrequencyHz;
using cwassistant::core::kMinimumSpotFrequencyHz;
constexpr double kHertzPerKilohertz = 1'000.0;

// The spotter is shown beside the spot and is never matched or acted on, so it
// is bounded and sanitised rather than validated as a callsign. A skimmer
// identifies itself as `SE5E-#` or `EA2RCF-4-#`, which is not a callsign and
// must not be filed down into one: the suffix is how the operator tells a
// machine's report from a person's.
constexpr int kMaximumSpotterLength = 32;

// An ADIF band name is at most a few characters -- 20m, 70cm, 2200m, 1.25cm.
// Anything longer is not one.
constexpr int kMaximumBandNameLength = 8;

// Nothing this client ever writes is longer than a callsign or a short setup
// command. A server description that somehow carries a paragraph is truncated
// at the socket rather than sent.
constexpr int kMaximumSentLineBytes = 120;

// Spots wait here only for the length of one batch interval. Reaching this
// many means nobody is draining `spotsReceived`, and a feed that runs at
// several spots a second would otherwise grow this buffer for as long as the
// connection lasts.
constexpr std::size_t kMaximumPendingSpots = 4'096;

// Telnet, RFC 854. IAC introduces one or two bytes of protocol; SB ... SE
// wraps a subnegotiation whose payload has no length this side can predict.
constexpr char16_t kTelnetIac = 0x00FF;
constexpr char16_t kTelnetSubnegotiationEnd = 0x00F0;
constexpr char16_t kTelnetSubnegotiationBegin = 0x00FA;
constexpr char16_t kTelnetLowestOptionCommand = 0x00FB;   // WILL
constexpr char16_t kTelnetHighestOptionCommand = 0x00FE;  // DONT

constexpr char16_t kFirstPrintableAscii = 0x0020;
constexpr char16_t kDeleteAscii = 0x007F;

// Nanoseconds since the Unix epoch, on the wall clock every spot is aged
// against. Deliberately local to this file: the HTTPS provider exports the
// same helper, and linking a telnet client against an HTTP client to borrow
// one call to QDateTime would tie two independent features together for
// nothing.
[[nodiscard]] std::uint64_t currentUnixNanoseconds() noexcept {
  const qint64 epoch_ms = QDateTime::currentMSecsSinceEpoch();
  return epoch_ms <= 0 ? 0ULL
                       : static_cast<std::uint64_t>(epoch_ms) * 1'000'000ULL;
}

// The printable text of one received line, with the telnet protocol removed.
//
// Cluster servers negotiate options in band: an IAC byte and the one or two
// bytes behind it are protocol rather than text, and a parser that reads them
// as characters sees a corrupted first line on about half the servers in the
// list. Everything that is not printable ASCII goes with them, which is also
// what removes the carriage return of the CRLF every cluster sends.
[[nodiscard]] QString sanitisedLine(const QString& line) {
  QString text;
  text.reserve(line.size());
  for (int index = 0; index < line.size(); ++index) {
    const char16_t character = line.at(index).unicode();
    if (character == kTelnetIac) {
      if (index + 1 >= line.size()) break;
      const char16_t command = line.at(index + 1).unicode();
      if (command == kTelnetSubnegotiationBegin) {
        // A subnegotiation ends at IAC SE and its payload is arbitrary bytes,
        // so it cannot be skipped by length. One that never terminates takes
        // the rest of the line with it rather than being read as text.
        int end = index + 2;
        while (end + 1 < line.size() &&
               !(line.at(end).unicode() == kTelnetIac &&
                 line.at(end + 1).unicode() == kTelnetSubnegotiationEnd)) {
          ++end;
        }
        index = end + 1 < line.size() ? end + 1 : line.size();
        continue;
      }
      // WILL, WONT, DO and DONT carry an option byte. Every other command,
      // including the doubled IAC that escapes a literal 0xFF, does not.
      index += command >= kTelnetLowestOptionCommand &&
                       command <= kTelnetHighestOptionCommand
                   ? 2
                   : 1;
      continue;
    }
    if (character == u'\t') {
      // A tab is a column separator on some servers. It becomes a space so
      // that one whitespace rule splits every line.
      text.append(QLatin1Char(' '));
      continue;
    }
    if (character >= kFirstPrintableAscii && character < kDeleteAscii)
      text.append(QChar(character));
  }
  return text.trimmed();
}

// Whether the server is asking for the login callsign.
//
// Every cluster implementation words this differently and none of them
// announce which one they are, so the prompt is recognised by shape and the
// callsign is sent only in answer to one. The prompt also arrives without a
// newline, which is why it is tested against a partial line as well.
[[nodiscard]] bool looksLikeLoginPrompt(const QString& text) {
  const QString lowered = text.toLower();
  if (lowered.contains(QLatin1String("enter your call"))) return true;
  return lowered == QLatin1String("login:");
}

// Printable text, upper-cased and bounded. The same treatment the HTTPS
// provider gives a spotter, for the same reason: it is displayed, never used.
[[nodiscard]] QString sanitisedSpotter(const QString& value) {
  QString text;
  text.reserve(kMaximumSpotterLength);
  for (const QChar character : value) {
    if (text.size() >= kMaximumSpotterLength) break;
    if (character.isPrint() && character.unicode() < 128U)
      text.append(character.toUpper());
  }
  return text;
}

// The HHMM'Z' stamp a cluster line ends with, if it has one.
//
// It is searched for rather than indexed, because the fields in front of it
// differ per server and a human spot ends with free text where a skimmer line
// ends with a mode and a signal report. The last match wins: the stamp is the
// final field, and a comment that happens to contain one is in front of it.
[[nodiscard]] std::optional<QTime> stampedTime(const QStringList& tokens,
                                               const int first) {
  std::optional<QTime> stamp;
  for (int index = first; index < tokens.size(); ++index) {
    const QString token = tokens.at(index);
    if (token.size() != 5) continue;
    if (token.at(4).toUpper() != QLatin1Char('Z')) continue;
    bool digits = true;
    for (int position = 0; position < 4; ++position)
      digits = digits && token.at(position).isDigit();
    if (!digits) continue;
    const int value = token.left(4).toInt();
    const int hour = value / 100;
    const int minute = value % 100;
    if (hour > 23 || minute > 59) continue;
    stamp = QTime(hour, minute);
  }
  return stamp;
}

// When the spot was heard, on the same epoch the registry ages everything
// against.
//
// The cluster stamp carries no date, only a time of day, so it is anchored to
// the date the line arrived. A stamp later in the day than the arrival belongs
// to the previous day, because a spot cannot have been heard after it reached
// this machine; the result is capped at the arrival for the same reason.
[[nodiscard]] std::uint64_t observedNanoseconds(
    const std::optional<QTime>& stamp, const std::uint64_t received_ns) {
  if (!stamp || received_ns == 0ULL) return received_ns;
  const QDateTime received = QDateTime::fromMSecsSinceEpoch(
      static_cast<qint64>(received_ns / 1'000'000ULL), QTimeZone::UTC);
  QDateTime observed(received.date(), *stamp, QTimeZone::UTC);
  if (observed > received) observed = observed.addDays(-1);
  const qint64 epoch_ms = observed.toMSecsSinceEpoch();
  if (epoch_ms <= 0) return received_ns;
  // Named with the exact type rather than deduced, because a `ULL` literal
  // promotes the product to a type that is the same width as `uint64_t` but
  // not the same spelling on every platform, and `std::min` deduces one type
  // from both of its arguments.
  const std::uint64_t observed_ns =
      static_cast<std::uint64_t>(epoch_ms) * 1'000'000ULL;
  return std::min(observed_ns, received_ns);
}

// A band name safe to substitute into a command line.
//
// ADIF spells these as digits, letters and at most a decimal point -- 20M,
// 70CM, 1.25M -- so anything else is not a band this can name. It matters more
// than it looks: the result is pasted into a command written to somebody
// else's server, and a band name carrying a space or a control character would
// be a second command line sent under the operator's callsign. Unrecognisable
// text becomes no band at all rather than a repaired one.
//
// Lower case because that is how every cluster's own band table spells them.
[[nodiscard]] QString acceptableBandName(const QString& band_name) {
  const QString trimmed = band_name.trimmed();
  if (trimmed.isEmpty() || trimmed.size() > kMaximumBandNameLength) return {};
  for (const QChar character : trimmed) {
    const char16_t code = character.unicode();
    const bool nameable = (code >= u'0' && code <= u'9') ||
                          (code >= u'a' && code <= u'z') ||
                          (code >= u'A' && code <= u'Z') || code == u'.';
    if (!nameable) return {};
  }
  return trimmed.toLower();
}

// Whether two descriptions name the same session. Changing any of these means
// a different machine, a different kind of evidence, or a different setup
// conversation, and all of them require the connection to be made again.
[[nodiscard]] bool describesSameServer(const DxClusterServer& left,
                                       const DxClusterServer& right) {
  return left.host == right.host && left.port == right.port &&
         left.source == right.source &&
         left.login_commands == right.login_commands && left.name == right.name;
}

[[nodiscard]] QString serverLabel(const DxClusterServer& server) {
  return server.name.isEmpty() ? server.host : server.name;
}

// What to tell the operator when the far end hangs up. It is the ordinary way
// a cluster ends a session -- full, restarting, or unconvinced by the login --
// and whether the login had completed is the one piece of it the operator can
// act on.
[[nodiscard]] QString closedByServerMessage(const DxClusterServer& server,
                                            const bool was_logged_in) {
  return was_logged_in
             ? QStringLiteral("%1 closed the connection.")
                   .arg(serverLabel(server))
             : QStringLiteral(
                   "%1 closed the connection before the login completed.")
                   .arg(serverLabel(server));
}

}  // namespace

DxClusterClient::DxClusterClient(QObject* parent) : QObject(parent) {
  reconnect_timer_.setSingleShot(true);
  connect(&reconnect_timer_, &QTimer::timeout, this, [this] {
    if (enabled_) openConnection();
  });

  batch_timer_.setSingleShot(false);
  batch_timer_.setInterval(kBatchIntervalMs);
  connect(&batch_timer_, &QTimer::timeout, this, &DxClusterClient::flushBatch);

  idle_timer_.setSingleShot(true);
  idle_timer_.setInterval(kIdleTimeoutSeconds * 1'000);
  connect(&idle_timer_, &QTimer::timeout, this, [this] {
    if (!enabled_) return;
    // A cluster that has sent nothing for this long is not a quiet band, it is
    // a link the socket still believes is open. It is the usual way a cluster
    // connection dies and the only way to notice it is the silence.
    closeConnection();
    if (consecutive_failures_ < std::numeric_limits<int>::max())
      ++consecutive_failures_;
    setStatus(QStringLiteral("%1 went silent for %2 minutes.")
                  .arg(serverLabel(server_))
                  .arg(kIdleTimeoutSeconds / 60));
    scheduleReconnect();
    emit stateChanged();
  });
}

DxClusterClient::~DxClusterClient() { closeConnection(); }

bool DxClusterClient::enabled() const noexcept { return enabled_; }

void DxClusterClient::setEnabled(const bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (enabled_) {
    consecutive_failures_ = 0;
    openConnection();
  } else {
    reconnect_timer_.stop();
    closeConnection();
    // What has already been parsed is delivered before the timer stops.
    // Switching the feature off is not a reason to lose observations that were
    // received while it was on.
    flushBatch();
    batch_timer_.stop();
    pending_.clear();
    consecutive_failures_ = 0;
    setStatus(QStringLiteral("DX cluster is off."));
  }
  emit stateChanged();
}

bool DxClusterClient::connected() const noexcept {
  return socket_ != nullptr &&
         socket_->state() == QAbstractSocket::ConnectedState;
}

const QString& DxClusterClient::statusMessage() const noexcept {
  return status_message_;
}

int DxClusterClient::consecutiveFailures() const noexcept {
  return consecutive_failures_;
}

void DxClusterClient::setServer(const DxClusterServer& server) {
  if (describesSameServer(server_, server)) return;
  server_ = server;
  consecutive_failures_ = 0;
  if (enabled_) {
    reconnect_timer_.stop();
    closeConnection();
    openConnection();
  }
  emit stateChanged();
}

const DxClusterServer& DxClusterClient::server() const noexcept {
  return server_;
}

void DxClusterClient::setLoginCallsign(const QString& callsign) {
  // Stored as the operator typed it, minus surrounding blanks. What goes to
  // the wire is the normalised form of exactly this text, produced by the same
  // function that accepted it, so there is no third spelling of the callsign
  // anywhere between the settings page and the socket.
  const QString trimmed = callsign.trimmed();
  if (login_callsign_ == trimmed) return;
  login_callsign_ = trimmed;
  consecutive_failures_ = 0;
  if (enabled_) {
    reconnect_timer_.stop();
    closeConnection();
    openConnection();
  }
  emit stateChanged();
}

const QString& DxClusterClient::loginCallsign() const noexcept {
  return login_callsign_;
}

void DxClusterClient::setBandFilter(const QString& band_name) {
  const QString band = acceptableBandName(band_name);
  if (band_filter_ == band) return;
  band_filter_ = band;
  // The connection is deliberately left alone. Following the receiver across a
  // band edge is a filter change on a session that is already established, and
  // dropping and rejoining a volunteer's cluster every time the operator tunes
  // would be both slower and ruder than sending one command.
  //
  // Only the commands that mention the band are re-sent. The rest are setup,
  // they were accepted at login, and repeating `set/skimmer` on every band
  // change is noise on a machine shared by thousands of operators.
  if (logged_in_) {
    for (const QString& command : resolvedCommands(true)) sendLine(command);
  }
  // Nothing is sent when the band has become unknown, which leaves the last
  // filter the server was given in force. That is the conservative end of the
  // choice: the alternative is widening the filter to everything, and a feed
  // nobody asked for is worse than one band too narrow.
  emit stateChanged();
}

const QString& DxClusterClient::bandFilter() const noexcept {
  return band_filter_;
}

DxClusterLine DxClusterClient::parseLine(
    const QString& line, const std::uint64_t received_ns,
    const cwassistant::core::CwSpotSource source) {
  DxClusterLine parsed;
  const QString text = sanitisedLine(line);
  if (text.isEmpty()) return parsed;
  if (looksLikeLoginPrompt(text)) {
    parsed.is_login_prompt = true;
    return parsed;
  }

  // Everything else a cluster sends -- banners, user counts, talk messages,
  // the reminder that FT8 spots need SET/FT8 -- is not a spot and is not an
  // error either. Only the `DX de` shape is read, and only as far as it holds.
  const QStringList tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (tokens.size() < 5) return parsed;
  if (tokens.at(0).compare(QLatin1String("DX"), Qt::CaseInsensitive) != 0 ||
      tokens.at(1).compare(QLatin1String("de"), Qt::CaseInsensitive) != 0) {
    return parsed;
  }

  // Split on whitespace, never on column. The columns look fixed until a
  // skimmer with a long name appears: `EA2RCF-4-#:` is four characters wider
  // than `SE5E-#:` and pushes every field to its right, so a parser that
  // slices by index silently drops exactly the busiest reporters.
  int index = 2;
  QString spotter = tokens.at(index);
  if (spotter.endsWith(QLatin1Char(':'))) {
    spotter.chop(1);
  } else if (tokens.at(index + 1) == QLatin1String(":")) {
    // A few servers put a space in front of the colon.
    ++index;
  } else {
    // No colon at all is not the `DX de` announcement shape. Refused rather
    // than guessed at: the next two fields would be read as a frequency and a
    // callsign on nothing more than their position.
    return parsed;
  }
  if (index + 2 >= tokens.size()) return parsed;

  // A spot names who heard it or it is not a spot. `DX de :` yielded an empty
  // spotter and was still accepted, which is an assertion from nowhere: the
  // whole value of a report is that some identified receiver made it, and a
  // marker beside a callsign would be claiming corroboration nobody gave.
  // Every real cluster sends `DX de CALL:`.
  //
  // Checked here, before any field is accepted, rather than at the assignment
  // below. Returning from there left is_spot already set true, so the line was
  // refused and reported as a spot at the same time.
  const QString sanitised_spotter = sanitisedSpotter(spotter);
  if (sanitised_spotter.isEmpty()) return parsed;

  bool numeric = false;
  const double kilohertz = tokens.at(index + 1).toDouble(&numeric);
  if (!numeric || !std::isfinite(kilohertz)) return parsed;
  // Clusters have always published kilohertz -- 14007.90, not 14007900 -- and
  // CwSpot carries hertz.
  const double frequency_hz = kilohertz * kHertzPerKilohertz;
  if (frequency_hz < kMinimumSpotFrequencyHz ||
      frequency_hz > kMaximumSpotFrequencyHz) {
    return parsed;
  }

  // The callsign syntax rules live in one place for the whole program. A
  // cluster feed does not get a second, looser definition of a valid call, and
  // a call that fails it is dropped rather than repaired, because a repaired
  // spot corroborates nothing.
  const auto callsign = cwassistant::core::CallsignPolicy::normalize(
      tokens.at(index + 2).toStdString());
  if (!callsign) return parsed;

  parsed.is_spot = true;
  parsed.spot.callsign = *callsign;
  parsed.spot.frequency_hz = frequency_hz;
  parsed.spot.observed_ns =
      observedNanoseconds(stampedTime(tokens, index + 3), received_ns);
  parsed.spot.spotter = sanitised_spotter.toStdString();
  // What kind of evidence this is comes from the server that was connected to,
  // not from the line. A line can claim anything; the operator chose an RBN
  // node or a human cluster, and that choice is what decides whether this is a
  // receiver's measurement or a person's report.
  parsed.spot.source = source;
  return parsed;
}

bool DxClusterClient::isAcceptableLoginCallsign(const QString& callsign) {
  // Two questions, both of which have to be answered yes.
  //
  // The first is syntax, and it is not asked here. CallsignPolicy::normalize
  // is the whole program's definition of a valid callsign and a cluster login
  // does not get a second one beside it, which would be free to drift.
  //
  // The second is what makes this different from every other callsign check in
  // the application, and why it is not simply `normalize`. This string is
  // written to somebody else's machine as one line of a line-oriented
  // protocol. A carriage return or newline inside it would not be a malformed
  // callsign, it would be a second command line sent to that server under the
  // operator's name. The test is applied to the text as given, before any
  // trimming can hide a control character at either end.
  for (const QChar character : callsign) {
    const char16_t code = character.unicode();
    if (code < kFirstPrintableAscii || code == kDeleteAscii) return false;
  }
  return cwassistant::core::CallsignPolicy::normalize(callsign.toStdString())
      .has_value();
}

void DxClusterClient::openConnection() {
  if (!enabled_) return;
  // One socket at a time, always. These are volunteer machines and a client
  // that can hold two connections to one of them will eventually hold ten.
  if (socket_ != nullptr) return;
  if (!server_.isValid()) {
    setStatus(QStringLiteral("Choose a DX cluster server before connecting."));
    emit stateChanged();
    return;
  }
  if (!isAcceptableLoginCallsign(login_callsign_)) {
    // No anonymous connection and no invented callsign. A cluster login is an
    // identity claim on somebody else's server, and the only person who may
    // make it is the operator.
    setStatus(login_callsign_.isEmpty()
                  ? QStringLiteral("Enter your callsign in settings: a DX "
                                   "cluster cannot be joined anonymously.")
                  : QStringLiteral("That callsign cannot be sent to a "
                                   "cluster login. Check it in settings."));
    emit stateChanged();
    return;
  }

  logged_in_ = false;
  buffer_.clear();
  socket_ = new QTcpSocket(this);
  connect(socket_, &QTcpSocket::readyRead, this,
          &DxClusterClient::handleReadyRead);
  connect(socket_, &QTcpSocket::connected, this, [this] {
    setStatus(QStringLiteral("Connected to %1; waiting for the login prompt.")
                  .arg(serverLabel(server_)));
    emit stateChanged();
  });
  connect(socket_, &QTcpSocket::errorOccurred, this,
          [this](const QAbstractSocket::SocketError error) {
            if (socket_ == nullptr) return;
            const bool was_logged_in = logged_in_;
            const QString reason = socket_->errorString();
            closeConnection();
            if (!enabled_) {
              emit stateChanged();
              return;
            }
            if (consecutive_failures_ < std::numeric_limits<int>::max())
              ++consecutive_failures_;
            // A server hanging up arrives here as an error before it arrives
            // as a disconnection, and calling that "could not be reached"
            // would send the operator hunting a network fault that is not
            // there. The two are reported as the different things they are.
            setStatus(error == QAbstractSocket::RemoteHostClosedError
                          ? closedByServerMessage(server_, was_logged_in)
                          : QStringLiteral("%1 could not be reached: %2.")
                                .arg(serverLabel(server_), reason));
            scheduleReconnect();
            emit stateChanged();
          });
  connect(socket_, &QTcpSocket::disconnected, this, [this] {
    const bool was_logged_in = logged_in_;
    closeConnection();
    if (!enabled_) {
      emit stateChanged();
      return;
    }
    if (consecutive_failures_ < std::numeric_limits<int>::max())
      ++consecutive_failures_;
    setStatus(closedByServerMessage(server_, was_logged_in));
    scheduleReconnect();
    emit stateChanged();
  });

  setStatus(QStringLiteral("Connecting to %1…").arg(serverLabel(server_)));
  // Started here rather than on `connected`, so that a connection which never
  // completes is also bounded by it.
  idle_timer_.start();
  batch_timer_.start();
  socket_->connectToHost(server_.host, server_.port);
  emit stateChanged();
}

void DxClusterClient::closeConnection() {
  idle_timer_.stop();
  logged_in_ = false;
  buffer_.clear();
  if (socket_ == nullptr) return;
  QTcpSocket* const abandoned = socket_;
  socket_ = nullptr;
  // Detached before it is dropped. A close this class performed is not news
  // about the server, and letting it arrive back as an error would report a
  // broken cluster to the operator when all that happened was a setting
  // changing underneath it.
  abandoned->disconnect(this);
  abandoned->abort();
  abandoned->deleteLater();
}

void DxClusterClient::scheduleReconnect() {
  if (!enabled_) return;
  // Doubling, from the minimum to the maximum. A server that is down, full, or
  // refusing this callsign gets one attempt every ten minutes rather than a
  // retry loop; it is somebody's volunteered machine and the operator can
  // always disable and re-enable the feature to retry at once.
  int seconds = kMinimumReconnectSeconds;
  for (int attempt = 1;
       attempt < consecutive_failures_ && seconds < kMaximumReconnectSeconds;
       ++attempt) {
    seconds *= 2;
  }
  seconds = std::min(seconds, kMaximumReconnectSeconds);
  // Appended rather than replacing the reason. The operator needs both halves:
  // why the link went, and when it will be tried again.
  setStatus(status_message_ +
            QStringLiteral(" Reconnecting in %1 s.").arg(seconds));
  reconnect_timer_.start(seconds * 1'000);
}

void DxClusterClient::handleReadyRead() {
  if (socket_ == nullptr) return;
  const QByteArray chunk = socket_->readAll();
  if (chunk.isEmpty()) return;
  // Any byte at all is evidence the link is alive, including a banner nobody
  // will read.
  idle_timer_.start();
  buffer_.append(chunk);

  // Latin-1 rather than UTF-8, on purpose. A cluster line is ASCII by
  // protocol, and Latin-1 maps every byte to exactly one code point, so a
  // stray 0xFF is still recognisable as a telnet IAC instead of being replaced
  // by a decoder that found an invalid sequence.
  for (qsizetype newline = buffer_.indexOf('\n'); newline >= 0;
       newline = buffer_.indexOf('\n')) {
    const QByteArray raw = buffer_.left(newline);
    buffer_.remove(0, newline + 1);
    if (raw.size() > kMaximumLineBytes) continue;
    handleLine(QString::fromLatin1(raw));
  }

  if (buffer_.size() > kMaximumLineBytes) {
    // No newline in what is left. One byte past the limit is kept so that the
    // loop above still recognises this line as oversized and discards it when
    // its newline finally arrives; everything else is dropped instead of
    // letting a server without a newline grow this process's memory.
    buffer_.truncate(kMaximumLineBytes + 1);
    return;
  }
  if (logged_in_ || buffer_.isEmpty()) return;
  // The login prompt is the one thing a cluster sends without a newline behind
  // it: the server writes `Please enter your call: ` and waits. Splitting on
  // newlines alone would sit in front of that prompt until the idle timer gave
  // up, so the incomplete tail is offered to the parser as well, and consumed
  // only if it really is the prompt.
  const QString pending = QString::fromLatin1(buffer_);
  if (parseLine(pending, currentUnixNanoseconds(), server_.source)
          .is_login_prompt) {
    buffer_.clear();
    handleLine(pending);
  }
}

void DxClusterClient::handleLine(const QString& line) {
  const DxClusterLine parsed =
      parseLine(line, currentUnixNanoseconds(), server_.source);

  if (parsed.is_login_prompt) {
    // Answered exactly once. A server that prompts again has not accepted this
    // callsign, and answering it again would be a login loop against a machine
    // that has already said no; the server or the idle timer ends the session
    // instead.
    if (logged_in_) return;
    const auto callsign = cwassistant::core::CallsignPolicy::normalize(
        login_callsign_.toStdString());
    if (!callsign) return;
    logged_in_ = true;
    consecutive_failures_ = 0;
    const QString login = QString::fromStdString(*callsign);
    sendLine(login);
    // The only other thing ever written to this socket: the setup commands the
    // chosen server needs before it sends what a CW decoder can use, with the
    // band token resolved. After these it speaks only when the receiver
    // changes band. An empty list is normal and not a misconfiguration -- the
    // reverse beacon network's telnet port accepts no commands at all.
    for (const QString& command : resolvedCommands(false)) sendLine(command);
    setStatus(QStringLiteral("Connected to %1 as %2.")
                  .arg(serverLabel(server_), login));
    emit stateChanged();
    return;
  }

  if (!parsed.is_spot) return;
  // Reaching the cap means nothing is draining the batches. Dropping the spot
  // is the honest failure: it is corroboration, it expires in minutes anyway,
  // and holding it would cost memory that belongs to the decoder.
  if (pending_.size() >= kMaximumPendingSpots) return;
  pending_.push_back(parsed.spot);
}

void DxClusterClient::flushBatch() {
  if (pending_.empty()) return;
  CwClusterSpotBatch batch;
  batch.swap(pending_);
  emit spotsReceived(batch);
}

void DxClusterClient::setStatus(QString message) {
  status_message_ = std::move(message);
}

QStringList DxClusterClient::resolvedCommands(
    const bool band_dependent_only) const {
  const QString token = QStringLiteral("{BAND}");
  QStringList resolved;
  resolved.reserve(server_.login_commands.size());
  for (const QString& command : server_.login_commands) {
    const bool band_dependent = command.contains(token);
    // Asked for only the band-dependent ones: this is a band change on a live
    // session, and a command that does not mention the band was setup that the
    // server has already accepted.
    if (band_dependent_only && !band_dependent) continue;
    if (!band_dependent) {
      resolved.append(command);
      continue;
    }
    // No band, no command. Sending it with the token still in it, or with the
    // token replaced by nothing, produces `accept/spots 0 on /cw` -- a syntax
    // error on somebody else's machine, submitted under the operator's
    // callsign.
    if (band_filter_.isEmpty()) continue;
    QString filled = command;
    filled.replace(token, band_filter_);
    resolved.append(filled);
  }
  return resolved;
}

void DxClusterClient::sendLine(const QString& text) {
  if (socket_ == nullptr ||
      socket_->state() != QAbstractSocket::ConnectedState) {
    return;
  }
  // Printable ASCII only, and bounded. A control character in a configured
  // login command would be a second line written to a stranger's server, and
  // the whole promise of this class is that it writes only what it says it
  // writes.
  QByteArray payload;
  payload.reserve(text.size());
  for (const QChar character : text) {
    if (payload.size() >= kMaximumSentLineBytes) break;
    const char16_t code = character.unicode();
    if (code >= kFirstPrintableAscii && code < kDeleteAscii)
      payload.append(static_cast<char>(code));
  }
  payload = payload.trimmed();
  if (payload.isEmpty()) return;
  // CRLF: the line ending of the telnet protocol, not of this platform.
  payload.append("\r\n", 2);
  socket_->write(payload);
}

}  // namespace cwassistant::desktop
