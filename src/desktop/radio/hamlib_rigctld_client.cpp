#include "hamlib_rigctld_client.hpp"

#include <QAbstractSocket>
#include <QHostAddress>

#include <algorithm>
#include <charconv>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr qsizetype kMaximumLineBytes = 256;
constexpr qsizetype kMaximumBufferedBytes = 4096;
constexpr std::uint64_t kMaximumFrequencyHz = 99'999'999'999ULL;

bool validVfo(const QString& value) {
  if (value.isEmpty() ||
      value.size() > static_cast<qsizetype>(
                         cwassistant::core::kMaximumRadioVfoIdentifierLength)) {
    return false;
  }
  for (const QChar character : value) {
    if (!character.isLetterOrNumber() && character != QLatin1Char('_')) {
      return false;
    }
  }
  return true;
}

bool validToken(const QByteArray& value) {
  const QByteArray trimmed = value.trimmed();
  if (trimmed.isEmpty() || trimmed.size() > 32) return false;
  return std::all_of(trimmed.cbegin(), trimmed.cend(), [](const char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-';
  });
}

bool isLoopbackHost(const QString& value) {
  const QString trimmed = value.trimmed();
  if (trimmed.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
    return true;
  QHostAddress address;
  return address.setAddress(trimmed) && address.isLoopback();
}

std::optional<std::uint64_t> parseFrequency(const QByteArray& text) {
  const QByteArray trimmed = text.trimmed();
  std::uint64_t value = 0;
  const auto result = std::from_chars(
      trimmed.constData(), trimmed.constData() + trimmed.size(), value);
  if (result.ec != std::errc{} ||
      result.ptr != trimmed.constData() + trimmed.size() ||
      value == 0 || value > kMaximumFrequencyHz) {
    return std::nullopt;
  }
  return value;
}

std::optional<int> parseResult(const QByteArray& line) {
  if (!line.startsWith("RPRT ")) return std::nullopt;
  const QByteArray value_text = line.mid(5).trimmed();
  int value = 0;
  const auto result = std::from_chars(
      value_text.constData(), value_text.constData() + value_text.size(), value);
  if (result.ec != std::errc{} ||
      result.ptr != value_text.constData() + value_text.size()) {
    return std::nullopt;
  }
  return value;
}

QByteArray hamlibMode(const cwassistant::core::RadioMode mode) {
  using cwassistant::core::RadioMode;
  switch (mode) {
    case RadioMode::Cw: return "CW";
    case RadioMode::CwReverse: return "CWR";
    case RadioMode::LowerSideband: return "LSB";
    case RadioMode::UpperSideband: return "USB";
    case RadioMode::Am: return "AM";
    case RadioMode::Fm: return "FM";
    case RadioMode::DigitalLower: return "PKTLSB";
    case RadioMode::DigitalUpper: return "PKTUSB";
    case RadioMode::Rtty: return "RTTY";
    case RadioMode::RttyReverse: return "RTTYR";
    case RadioMode::Unknown: return {};
  }
  return {};
}

cwassistant::core::RadioMode parseMode(const QByteArray& mode) {
  using cwassistant::core::RadioMode;
  const QByteArray normalized = mode.trimmed().toUpper();
  if (normalized == "CW") return RadioMode::Cw;
  if (normalized == "CWR") return RadioMode::CwReverse;
  if (normalized == "PKTLSB") return RadioMode::DigitalLower;
  if (normalized == "PKTUSB") return RadioMode::DigitalUpper;
  if (normalized == "RTTY") return RadioMode::Rtty;
  if (normalized == "RTTYR") return RadioMode::RttyReverse;
  return cwassistant::core::radio_mode_from_token(normalized.toStdString());
}

cwassistant::core::RadioCapabilityBits writableCapabilities() {
  using cwassistant::core::RadioCapability;
  return RadioCapability::SetRxFrequency | RadioCapability::SetTxFrequency |
         RadioCapability::SetRxMode | RadioCapability::SetTxMode |
         RadioCapability::SetSplit;
}

}  // namespace

HamlibRigctldClient::HamlibRigctldClient(QObject* parent) : QObject(parent) {
  poll_timer_.setSingleShot(true);
  request_timer_.setSingleShot(true);
  reconnect_timer_.setSingleShot(true);

  connect(&socket_, &QTcpSocket::connected, this, [this] {
    request_timer_.stop();
    setStatus(QStringLiteral("Hamlib rigctld connected; verifying VFO mode…"));
    queue({QByteArrayLiteral("\\chk_vfo"), ReplyKind::Frequency,
           Field::VfoMode, 1, {}});
  });
  connect(&socket_, &QTcpSocket::readyRead, this, [this] {
    receive_buffer_.append(socket_.readAll());
    if (receive_buffer_.size() > kMaximumBufferedBytes) {
      failProtocol(
          QStringLiteral("Hamlib reply exceeded the protocol buffer limit."));
      return;
    }
    qsizetype newline = -1;
    while ((newline = receive_buffer_.indexOf('\n')) >= 0) {
      QByteArray line = receive_buffer_.left(newline);
      receive_buffer_.remove(0, newline + 1);
      if (line.endsWith('\r')) line.chop(1);
      if (line.size() > kMaximumLineBytes) {
        failProtocol(
            QStringLiteral("Hamlib reply line exceeded the protocol limit."));
        return;
      }
      consumeLine(std::move(line));
      if (socket_.state() == QAbstractSocket::UnconnectedState) return;
    }
  });
  connect(&socket_, &QTcpSocket::disconnected, this,
          &HamlibRigctldClient::handleDisconnected);
  connect(&socket_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) {
            if (!operator_disconnect_) {
              setStatus(QStringLiteral("Hamlib connection error: %1")
                            .arg(socket_.errorString()));
            }
          });
  connect(&poll_timer_, &QTimer::timeout, this,
          &HamlibRigctldClient::beginPoll);
  connect(&request_timer_, &QTimer::timeout, this, [this] {
    failProtocol(socket_.state() == QAbstractSocket::ConnectingState
                     ? QStringLiteral("Hamlib rigctld connection timed out.")
                     : QStringLiteral(
                           "Hamlib rigctld did not reply before the timeout."));
  });
  connect(&reconnect_timer_, &QTimer::timeout, this,
          &HamlibRigctldClient::beginConnection);
  clearObservedState();
}

HamlibRigctldClient::~HamlibRigctldClient() {
  operator_disconnect_ = true;
  QObject::disconnect(&socket_, nullptr, this, nullptr);
  socket_.abort();
}

void HamlibRigctldClient::connectToServer(Configuration configuration) {
  disconnectFromServer();
  if (!validConfiguration(configuration)) {
    setStatus(QStringLiteral("Invalid Hamlib host, VFO, port, or timeout configuration."));
    return;
  }
  // Do not send even the conventional localhost name through DNS. rigctld
  // has no authentication or encryption, so the accepted alias is pinned to
  // the numeric IPv4 loopback address before QTcpSocket sees it.
  if (configuration.host.trimmed().compare(
          QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
    configuration.host = QStringLiteral("127.0.0.1");
  }
  configuration_ = std::move(configuration);
  operator_disconnect_ = false;
  beginConnection();
}

void HamlibRigctldClient::disconnectFromServer() {
  operator_disconnect_ = true;
  poll_timer_.stop();
  request_timer_.stop();
  reconnect_timer_.stop();
  requests_.clear();
  active_request_.reset();
  receive_buffer_.clear();
  if (socket_.state() != QAbstractSocket::UnconnectedState) socket_.abort();
  clearObservedState();
  setStatus(QStringLiteral("Disconnected"));
}

bool HamlibRigctldClient::connected() const noexcept {
  return socket_.state() == QAbstractSocket::ConnectedState &&
         state_.availability == cwassistant::core::RadioObservation::Known;
}

bool HamlibRigctldClient::canWrite() const noexcept {
  return connected() && configuration_.writable;
}

QString HamlibRigctldClient::statusText() const { return status_; }

cwassistant::core::RadioState HamlibRigctldClient::radioState() const noexcept {
  return state_;
}

std::optional<cwassistant::core::VfoFrequencyPlan>
HamlibRigctldClient::frequencyPlan() const noexcept {
  using cwassistant::core::RadioObservation;
  if (state_.rx_frequency.observation != RadioObservation::Known ||
      state_.tx_frequency.observation != RadioObservation::Known ||
      state_.split.observation != RadioObservation::Known) {
    return std::nullopt;
  }
  return cwassistant::core::VfoFrequencyPlan{
      .rx_dial_hz = state_.rx_frequency.hz,
      .tx_dial_hz = state_.tx_frequency.hz,
      .split_enabled = state_.split.split == cwassistant::core::RadioSplit::Enabled};
}

bool HamlibRigctldClient::setRxFrequency(const std::uint64_t frequency_hz) {
  return queueWrite("F", configuration_.rx_vfo,
                    QByteArray::number(frequency_hz),
                    cwassistant::core::SetRxFrequency{frequency_hz});
}

bool HamlibRigctldClient::setTxFrequency(const std::uint64_t frequency_hz) {
  // Hamlib's split-frequency API is scoped to the RX/current VFO; the TX VFO
  // itself is selected independently by set_split_vfo.
  return queueWrite("I", configuration_.rx_vfo,
                    QByteArray::number(frequency_hz),
                    cwassistant::core::SetTxFrequency{frequency_hz});
}

bool HamlibRigctldClient::setRxMode(const cwassistant::core::RadioMode mode) {
  const QByteArray token = hamlibMode(mode);
  if (token.isEmpty()) return false;
  return queueWrite("M", configuration_.rx_vfo, token + " 0",
                    cwassistant::core::SetRxMode{mode});
}

bool HamlibRigctldClient::setTxMode(const cwassistant::core::RadioMode mode) {
  const QByteArray token = hamlibMode(mode);
  if (token.isEmpty()) return false;
  return queueWrite("X", configuration_.rx_vfo, token + " 0",
                    cwassistant::core::SetTxMode{mode});
}

bool HamlibRigctldClient::setSplit(const bool enabled) {
  return queueWrite("S", configuration_.rx_vfo,
                    QByteArray(enabled ? "1 " : "0 ") +
                        configuration_.tx_vfo.toLatin1(),
                    cwassistant::core::SetSplit{enabled});
}

void HamlibRigctldClient::beginConnection() {
  if (operator_disconnect_ ||
      socket_.state() != QAbstractSocket::UnconnectedState)
    return;
  setStatus(QStringLiteral("Connecting to Hamlib rigctld…"));
  socket_.connectToHost(configuration_.host, configuration_.port);
  request_timer_.start(configuration_.request_timeout_ms);
}

void HamlibRigctldClient::beginPoll() {
  if (socket_.state() != QAbstractSocket::ConnectedState || active_request_ ||
      !requests_.isEmpty()) return;
  using cwassistant::core::RadioObservation;
  pending_poll_ = {};
  pending_poll_.availability = RadioObservation::Known;
  pending_poll_.rx_vfo = {RadioObservation::Known,
                          configuration_.rx_vfo.toStdString()};
  pending_poll_.tx_vfo = {RadioObservation::Known,
                          configuration_.tx_vfo.toStdString()};
  pending_poll_.capabilities = {
      RadioObservation::Known,
      configuration_.writable ? writableCapabilities() : 0U};
  poll_fields_remaining_ = 5;
  queue({command("f", configuration_.rx_vfo), ReplyKind::Frequency,
         Field::RxFrequency, 1, {}});
  queue({command("i", configuration_.rx_vfo), ReplyKind::Frequency,
         Field::TxFrequency, 1, {}});
  queue({command("m", configuration_.rx_vfo), ReplyKind::Mode,
         Field::RxMode, 2, {}});
  queue({command("x", configuration_.rx_vfo), ReplyKind::Mode,
         Field::TxMode, 2, {}});
  queue({command("s", configuration_.rx_vfo), ReplyKind::Split,
         Field::Split, 2, {}});
}

void HamlibRigctldClient::queue(Request request) {
  if (request.command.isEmpty() || request.command.size() > kMaximumLineBytes) {
    failProtocol(QStringLiteral("Refused an invalid Hamlib command."));
    return;
  }
  requests_.push_back(std::move(request));
  sendNext();
}

void HamlibRigctldClient::sendNext() {
  if (active_request_ || requests_.isEmpty() ||
      socket_.state() != QAbstractSocket::ConnectedState) return;
  active_request_ = requests_.takeFirst();
  const qint64 written = socket_.write(active_request_->command + '\n');
  if (written != active_request_->command.size() + 1) {
    failProtocol(QStringLiteral("Could not send the complete Hamlib command."));
    return;
  }
  request_timer_.start(configuration_.request_timeout_ms);
}

void HamlibRigctldClient::consumeLine(QByteArray line) {
  if (!active_request_) {
    failProtocol(QStringLiteral("Hamlib sent an unsolicited reply."));
    return;
  }
  if (const auto result = parseResult(line)) {
    completeRequest(*result);
    return;
  }
  if (active_request_->reply_kind == ReplyKind::Result) {
    failProtocol(QStringLiteral("Hamlib write returned a malformed result."));
    return;
  }
  active_request_->payload.push_back(std::move(line));
  if (active_request_->payload.size() == active_request_->payload_lines) {
    completeRequest(0);
  }
}

void HamlibRigctldClient::completeRequest(const int result) {
  request_timer_.stop();
  Request request = std::move(*active_request_);
  active_request_.reset();
  if (result != 0) {
    failProtocol(QStringLiteral("Hamlib command failed (RPRT %1).").arg(result));
    return;
  }

  using cwassistant::core::RadioMode;
  using cwassistant::core::RadioObservation;
  bool valid = true;
  if (request.field == Field::VfoMode) {
    valid = request.payload.size() == 1 &&
            request.payload.front().trimmed() == "1";
    if (!valid) {
      failProtocol(QStringLiteral("Hamlib rigctld must run in VFO mode "
                                  "(--vfo) for exact RX/TX targeting."));
      return;
    }
    beginPoll();
  } else if (request.field == Field::RxFrequency ||
             request.field == Field::TxFrequency) {
    const auto frequency = request.payload.isEmpty()
        ? std::nullopt : parseFrequency(request.payload.front());
    valid = frequency.has_value();
    if (valid) {
      auto& target = request.field == Field::RxFrequency
          ? pending_poll_.rx_frequency : pending_poll_.tx_frequency;
      target = {RadioObservation::Known, *frequency};
    }
  } else if (request.field == Field::RxMode || request.field == Field::TxMode) {
    valid = !request.payload.isEmpty() && validToken(request.payload.front());
    if (valid) {
      const RadioMode mode = parseMode(request.payload.front());
      auto& target = request.field == Field::RxMode
          ? pending_poll_.rx_mode : pending_poll_.tx_mode;
      target = {mode == RadioMode::Unknown ? RadioObservation::Unavailable
                                          : RadioObservation::Known,
                mode};
    }
  } else if (request.field == Field::Split) {
    valid = request.payload.size() == 2 &&
            (request.payload.front().trimmed() == "0" ||
             request.payload.front().trimmed() == "1") &&
            validVfo(QString::fromLatin1(request.payload.back().trimmed()));
    if (valid) {
      pending_poll_.split = {
          RadioObservation::Known,
          request.payload.front().trimmed() == "1"
              ? cwassistant::core::RadioSplit::Enabled
              : cwassistant::core::RadioSplit::Disabled};
      pending_poll_.tx_vfo = {
          RadioObservation::Known,
          request.payload.back().trimmed().toStdString()};
    }
  }
  if (!valid) {
    failProtocol(QStringLiteral("Hamlib returned an invalid radio-state value."));
    return;
  }

  if (request.field != Field::Write && request.field != Field::VfoMode &&
      --poll_fields_remaining_ == 0) {
    publishPoll();
  }
  sendNext();
  if (request.field == Field::Write && !active_request_ &&
      requests_.isEmpty()) {
    poll_timer_.start(configuration_.poll_interval_ms);
  }
}

void HamlibRigctldClient::failProtocol(const QString& reason) {
  poll_timer_.stop();
  request_timer_.stop();
  requests_.clear();
  active_request_.reset();
  receive_buffer_.clear();
  clearObservedState();
  setStatus(reason);
  socket_.abort();
}

void HamlibRigctldClient::handleDisconnected() {
  poll_timer_.stop();
  request_timer_.stop();
  requests_.clear();
  active_request_.reset();
  receive_buffer_.clear();
  clearObservedState();
  if (!operator_disconnect_) scheduleReconnect();
}

void HamlibRigctldClient::scheduleReconnect() {
  if (!reconnect_timer_.isActive())
    reconnect_timer_.start(configuration_.reconnect_interval_ms);
}

void HamlibRigctldClient::setStatus(QString status) {
  if (status_ == status) return;
  status_ = std::move(status);
  emit statusChanged();
}

void HamlibRigctldClient::clearObservedState() {
  state_ = {};
  state_.availability = cwassistant::core::RadioObservation::Unavailable;
  emit radioStateChanged();
}

void HamlibRigctldClient::publishPoll() {
  if (!cwassistant::core::radio_state_is_valid(pending_poll_)) {
    failProtocol(QStringLiteral("Hamlib returned an inconsistent radio state."));
    return;
  }
  if (state_ != pending_poll_) {
    state_ = pending_poll_;
    emit radioStateChanged();
  }
  setStatus(configuration_.writable
                ? QStringLiteral("Hamlib rigctld control connected.")
                : QStringLiteral("Hamlib rigctld monitor connected (read-only)."));
  poll_timer_.start(configuration_.poll_interval_ms);
}

QByteArray HamlibRigctldClient::command(QByteArray opcode, const QString& vfo,
                                        QByteArray argument) const {
  QByteArray result = std::move(opcode);
  if (configuration_.vfo_mode) result += ' ' + vfo.toLatin1();
  if (!argument.isEmpty()) result += ' ' + argument;
  return result;
}

bool HamlibRigctldClient::validConfiguration(
    const Configuration& value) const {
  // rigctld has no authentication or transport encryption.  Remote control is
  // accepted only through an operator-managed local tunnel, never plaintext
  // directly across a LAN or the Internet.
  return isLoopbackHost(value.host) && !value.host.contains('\n') &&
         !value.host.contains('\r') && value.port != 0 &&
         validVfo(value.rx_vfo) && validVfo(value.tx_vfo) &&
         value.rx_vfo != value.tx_vfo && value.vfo_mode &&
         value.poll_interval_ms >= 100 &&
         value.request_timeout_ms >= 100 &&
         value.reconnect_interval_ms >= 100;
}

bool HamlibRigctldClient::queueWrite(
    const QByteArray& opcode, const QString& vfo, const QByteArray& argument,
    const cwassistant::core::RadioCommand& requested) {
  if (!canWrite() ||
      cwassistant::core::validate_radio_command(state_, requested) !=
          cwassistant::core::RadioCommandValidation::Valid) {
    setStatus(QStringLiteral(
        "Hamlib rejected an unavailable or unsafe radio command."));
    return false;
  }
  const QByteArray wire = command(opcode, vfo, argument);
  if (wire.contains('\r') || wire.contains('\n')) return false;
  poll_timer_.stop();
  queue({wire, ReplyKind::Result, Field::Write, 0, {}});
  // The next complete poll, not RPRT alone, is authoritative confirmation.
  return true;
}

}  // namespace cwassistant::desktop
