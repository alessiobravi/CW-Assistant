#include "cat4om_client.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <utility>

namespace cwassistant::desktop {
namespace {

constexpr auto kHandshakeTimeoutMs = 10'000;
constexpr auto kDefaultInactivityTimeoutMs = 30'000;

QString authenticationProof(const QString& password,
                            const QString& timestamp) {
  const QByteArray material =
      password.toUtf8() + '\n' + timestamp.toUtf8();
  return QString::fromLatin1(
      QCryptographicHash::hash(material, QCryptographicHash::Sha256).toBase64());
}

std::uint64_t jsonFrequency(const QJsonValue& value) {
  const auto number = value.toDouble(-1.0);
  if (number <= 0.0 || number > 9'007'199'254'740'991.0 ||
      number != static_cast<double>(static_cast<std::uint64_t>(number))) {
    return 0;
  }
  return static_cast<std::uint64_t>(number);
}

}  // namespace

Cat4OmClient::Cat4OmClient(QObject* parent) : QObject(parent) {
  handshake_timer_.setSingleShot(true);
  inactivity_timer_.setSingleShot(true);
  reconnect_timer_.setSingleShot(true);

  connect(&socket_, &QWebSocket::connected, this, &Cat4OmClient::sendHello);
  connect(&socket_, &QWebSocket::textMessageReceived, this,
          &Cat4OmClient::handleTextMessage);
  connect(&socket_, &QWebSocket::binaryMessageReceived, this,
          [](const QByteArray&) {
            // Binary frames are reserved by protocol 1.x and must be ignored
            // unless an audio stream was explicitly requested.
          });
  connect(&socket_, &QWebSocket::disconnected, this, [this] {
    welcomed_ = false;
    role_ = cwassistant::core::Cat4OmRole::Unknown;
    client_id_.clear();
    pending_actions_.clear();
    handshake_timer_.stop();
    inactivity_timer_.stop();
    if (!preserve_disconnect_status_) {
      setStatus(QStringLiteral("Disconnected"));
    }
    preserve_disconnect_status_ = false;
    if (!user_disconnect_) {
      scheduleReconnect();
    }
  });
  connect(&socket_, &QWebSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError) {
            preserve_disconnect_status_ = true;
            setStatus(QStringLiteral("Connection error: %1")
                          .arg(socket_.errorString()));
          });
  connect(&handshake_timer_, &QTimer::timeout, this, [this] {
    fail(QStringLiteral("CAT4OM did not complete its hello handshake in time."));
  });
  connect(&inactivity_timer_, &QTimer::timeout, this, [this] {
    fail(QStringLiteral("CAT4OM stopped sending state/keepalive messages."));
  });
  connect(&reconnect_timer_, &QTimer::timeout, this, [this] {
    if (!user_disconnect_) {
      setStatus(QStringLiteral("Reconnecting to CAT4OM…"));
      socket_.open(endpoint_);
    }
  });
}

void Cat4OmClient::connectToServer(const QUrl& endpoint, QString radio_id,
                                   const QString& password,
                                   const bool observer) {
  disconnectFromServer();
  if (!endpoint.isValid() ||
      (endpoint.scheme() != QStringLiteral("ws") &&
       endpoint.scheme() != QStringLiteral("wss")) ||
      endpoint.host().isEmpty() || endpoint.port(-1) == 0) {
    setStatus(QStringLiteral("Enter a valid ws:// or wss:// CAT4OM Control URL."));
    return;
  }

  endpoint_ = endpoint;
  configured_radio_id_ = radio_id.trimmed();
  password_ = password;
  observer_ = observer;
  user_disconnect_ = false;
  reconnect_attempt_ = 0;
  setStatus(QStringLiteral("Connecting to CAT4OM…"));
  socket_.open(endpoint_);
}

void Cat4OmClient::disconnectFromServer() {
  user_disconnect_ = true;
  reconnect_timer_.stop();
  handshake_timer_.stop();
  inactivity_timer_.stop();
  password_.clear();
  preserve_disconnect_status_ = false;
  if (socket_.state() != QAbstractSocket::UnconnectedState) {
    socket_.close(QWebSocketProtocol::CloseCodeNormal,
                  QStringLiteral("Operator disconnect"));
  }
}

bool Cat4OmClient::connected() const noexcept { return welcomed_; }

bool Cat4OmClient::canWrite() const noexcept {
  return welcomed_ && !observer_ &&
         role_ == cwassistant::core::Cat4OmRole::Master;
}

QString Cat4OmClient::statusText() const { return status_; }
QString Cat4OmClient::radioId() const {
  return QString::fromStdString(selected_radio_.radio_id);
}

std::optional<cwassistant::core::VfoFrequencyPlan>
Cat4OmClient::frequencyPlan() const noexcept {
  return cwassistant::core::cat4om_frequency_plan(selected_radio_);
}

bool Cat4OmClient::requestOwnership() {
  if (observer_) {
    setStatus(QStringLiteral("Reconnect as a control client before requesting ownership."));
    return false;
  }
  return sendRequest(QStringLiteral("getOwnership"), {}, false);
}

bool Cat4OmClient::setFrequency(const std::uint64_t frequency_hz,
                                const QString& vfo) {
  if (!canWrite() || frequency_hz == 0 ||
      frequency_hz > 9'007'199'254'740'991ULL ||
      selected_radio_.connection_status != "connected" ||
      !cwassistant::core::cat4om_has_command(selected_radio_,
                                             "SetFrequency")) {
    setStatus(QStringLiteral("Frequency write is unavailable for this connection/radio."));
    return false;
  }
  QJsonObject parameters{{QStringLiteral("frequency"),
                          static_cast<qint64>(frequency_hz)}};
  if (!vfo.isEmpty()) {
    parameters.insert(QStringLiteral("vfo"), vfo);
  }
  return sendRequest(QStringLiteral("setFrequency"), parameters, true);
}

bool Cat4OmClient::setSplit(const bool enabled, const QString& tx_vfo) {
  if (!canWrite() || selected_radio_.connection_status != "connected" ||
      !cwassistant::core::cat4om_has_command(selected_radio_, "SetSplit")) {
    setStatus(QStringLiteral("Split control is unavailable for this connection/radio."));
    return false;
  }
  QJsonObject parameters{{QStringLiteral("enabled"), enabled}};
  if (!tx_vfo.isEmpty()) {
    parameters.insert(QStringLiteral("txVfo"), tx_vfo);
  }
  return sendRequest(QStringLiteral("setSplit"), parameters, true);
}

void Cat4OmClient::sendHello() {
  QJsonObject hello{
      {QStringLiteral("type"), QStringLiteral("hello")},
      {QStringLiteral("protocolVersion"),
       QString::fromLatin1(cwassistant::core::kCat4OmProtocolVersion.data(),
                           static_cast<qsizetype>(
                               cwassistant::core::kCat4OmProtocolVersion.size()))},
      {QStringLiteral("appName"), QStringLiteral("CW Assistant")},
      {QStringLiteral("appVersion"), QStringLiteral("0.1.0")},
      {QStringLiteral("requestMaster"), false},
  };
  if (observer_) {
    hello.insert(QStringLiteral("noRegister"), true);
  } else if (!password_.isEmpty()) {
    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmm"));
    hello.insert(
        QStringLiteral("auth"),
        QJsonObject{{QStringLiteral("scheme"),
                     QStringLiteral("cat4om-sha256-minute-v1")},
                    {QStringLiteral("timestampUtc"), timestamp},
                    {QStringLiteral("proof"),
                     authenticationProof(password_, timestamp)}});
  }
  socket_.sendTextMessage(
      QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));
  handshake_timer_.start(kHandshakeTimeoutMs);
  setStatus(QStringLiteral("Waiting for CAT4OM welcome…"));
}

void Cat4OmClient::handleTextMessage(const QString& text) {
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(text.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    setStatus(QStringLiteral("Ignored malformed CAT4OM JSON message."));
    return;
  }
  const QJsonObject message = document.object();
  const QString type = message.value(QStringLiteral("type")).toString();
  if (!welcomed_) {
    if (type == QStringLiteral("error")) {
      fail(QStringLiteral("CAT4OM rejected the handshake: %1")
               .arg(message.value(QStringLiteral("message")).toString()));
    } else if (type == QStringLiteral("welcome")) {
      handleWelcome(message);
    } else {
      fail(QStringLiteral("The endpoint is not a CAT4OM Control channel."));
    }
    return;
  }

  inactivity_timer_.start(inactivity_timer_.interval());
  if (type == QStringLiteral("stateUpdate")) {
    handleStateUpdate(message);
  } else if (type == QStringLiteral("event")) {
    handleEvent(message);
  } else if (type == QStringLiteral("response")) {
    handleResponse(message);
  }
  // Unknown message types are intentionally ignored for 1.x compatibility.
}

void Cat4OmClient::handleWelcome(const QJsonObject& message) {
  if (message.value(QStringLiteral("endpoint")).toString() !=
      QStringLiteral("control")) {
    fail(QStringLiteral("The configured CAT4OM URL is not a Control endpoint."));
    return;
  }
  const auto version =
      message.value(QStringLiteral("protocolVersion")).toString().toStdString();
  if (!cwassistant::core::cat4om_protocol_compatible(version)) {
    fail(QStringLiteral("Incompatible CAT4OM protocol major version."));
    return;
  }

  const QString new_instance =
      message.value(QStringLiteral("instanceId")).toString();
  if (!instance_id_.isEmpty() && instance_id_ != new_instance) {
    radios_.clear();
    last_sequence_ = -1;
  }
  instance_id_ = new_instance;
  client_id_ = message.value(QStringLiteral("clientId")).toString();
  role_ = cwassistant::core::cat4om_role_from_string(
      message.value(QStringLiteral("role")).toString().toStdString());
  last_sequence_ = message.value(QStringLiteral("seq")).toInteger(-1);
  replaceOrMergeRadios(message.value(QStringLiteral("radios")).toArray(), true);
  const int keepalive = std::max(
      1'000, message.value(QStringLiteral("keepaliveIntervalMs")).toInt(10'000));
  inactivity_timer_.setInterval(std::max(kDefaultInactivityTimeoutMs,
                                         keepalive * 3));
  inactivity_timer_.start();
  handshake_timer_.stop();
  welcomed_ = true;
  reconnect_attempt_ = 0;
  setStatus(observer_ ? QStringLiteral("CAT4OM monitor connected (read-only).")
                      : QStringLiteral("CAT4OM control channel connected."));
  emit radioStateChanged();
}

void Cat4OmClient::handleStateUpdate(const QJsonObject& message) {
  const qint64 sequence = message.value(QStringLiteral("seq")).toInteger(-1);
  if (last_sequence_ >= 0 && sequence > last_sequence_ + 1 && !observer_) {
    sendRequest(QStringLiteral("getAllState"), {}, false);
  }
  if (sequence >= 0 && sequence < last_sequence_) {
    radios_.clear();
  }
  last_sequence_ = sequence;
  updateRole(message.value(QStringLiteral("masterId")).toString());
  replaceOrMergeRadios(message.value(QStringLiteral("radios")).toArray(),
                       message.value(QStringLiteral("full")).toBool(true));
  emit radioStateChanged();
}

void Cat4OmClient::handleEvent(const QJsonObject& message) {
  if (message.value(QStringLiteral("event")).toString() ==
      QStringLiteral("ownershipChanged")) {
    updateRole(message.value(QStringLiteral("details"))
                   .toObject()
                   .value(QStringLiteral("masterId"))
                   .toString());
  }
}

void Cat4OmClient::handleResponse(const QJsonObject& message) {
  const QString id = message.value(QStringLiteral("id")).toString();
  const QString action = pending_actions_.take(id);
  if (!message.value(QStringLiteral("success")).toBool()) {
    const QJsonObject error = message.value(QStringLiteral("error")).toObject();
    const QString code = error.value(QStringLiteral("code")).toString();
    setStatus(QStringLiteral("CAT4OM %1 failed (%2): %3")
                  .arg(action, code,
                       error.value(QStringLiteral("message")).toString()));
    return;
  }
  if (action == QStringLiteral("getOwnership")) {
    role_ = cwassistant::core::Cat4OmRole::Master;
    emit statusChanged();
  } else if (action == QStringLiteral("getAllState")) {
    replaceOrMergeRadios(message.value(QStringLiteral("result"))
                             .toObject()
                             .value(QStringLiteral("radios"))
                             .toArray(),
                         true);
    emit radioStateChanged();
  }
  setStatus(QStringLiteral("CAT4OM accepted %1; awaiting pushed radio state.")
                .arg(action));
}

void Cat4OmClient::replaceOrMergeRadios(const QJsonArray& radios,
                                        const bool full) {
  if (full) {
    radios_.clear();
  }
  for (const auto& value : radios) {
    const QJsonObject radio = value.toObject();
    const QString id = radio.value(QStringLiteral("radioId")).toString();
    if (!id.isEmpty()) {
      radios_.insert(id, radio);
    }
  }
  updateSelectedRadio();
}

void Cat4OmClient::updateSelectedRadio() {
  QString id = configured_radio_id_;
  if (id.isEmpty() && !radios_.isEmpty()) {
    id = radios_.constBegin().key();
  }
  selected_radio_ = parseRadio(radios_.value(id));
}

bool Cat4OmClient::sendRequest(const QString& action,
                               const QJsonObject& parameters,
                               const bool radio_scoped) {
  if (!welcomed_ || observer_) {
    setStatus(QStringLiteral("CAT4OM request is unavailable on this connection."));
    return false;
  }
  const QString id = QStringLiteral("cwa-%1").arg(++request_sequence_);
  QJsonObject request{
      {QStringLiteral("type"), QStringLiteral("request")},
      {QStringLiteral("id"), id},
      {QStringLiteral("clientId"), client_id_},
      {QStringLiteral("action"), action},
      {QStringLiteral("params"), parameters},
  };
  if (radio_scoped) {
    const QString radio_id = radioId();
    if (radio_id.isEmpty()) {
      setStatus(QStringLiteral("Select a CAT4OM radio before sending a request."));
      return false;
    }
    request.insert(QStringLiteral("radioId"), radio_id);
  }
  const bool sent = socket_.sendTextMessage(
                        QString::fromUtf8(QJsonDocument(request).toJson(
                            QJsonDocument::Compact))) > 0;
  if (sent) {
    pending_actions_.insert(id, action);
  }
  return sent;
}

void Cat4OmClient::updateRole(const QString& master_id) {
  if (observer_) {
    role_ = cwassistant::core::Cat4OmRole::Observer;
  } else {
    role_ = master_id == client_id_ ? cwassistant::core::Cat4OmRole::Master
                                    : cwassistant::core::Cat4OmRole::Slave;
  }
  emit statusChanged();
}

void Cat4OmClient::setStatus(QString status) {
  if (status_ != status) {
    status_ = std::move(status);
    emit statusChanged();
  }
}

void Cat4OmClient::fail(QString status) {
  preserve_disconnect_status_ = true;
  setStatus(std::move(status));
  socket_.close(QWebSocketProtocol::CloseCodeProtocolError,
                QStringLiteral("Protocol failure"));
}

void Cat4OmClient::scheduleReconnect() {
  reconnect_attempt_ = std::min(reconnect_attempt_ + 1, 6);
  reconnect_timer_.start(std::min(30'000, 500 * (1 << reconnect_attempt_)));
}

cwassistant::core::Cat4OmRadioState Cat4OmClient::parseRadio(
    const QJsonObject& radio) {
  cwassistant::core::Cat4OmRadioState state{
      .radio_id = radio.value(QStringLiteral("radioId")).toString().toStdString(),
      .connection_status = radio.value(QStringLiteral("connectionStatus"))
                               .toString()
                               .toStdString(),
      .active_vfo = radio.value(QStringLiteral("activeVfo")).toString().toStdString(),
      .tx_vfo = radio.value(QStringLiteral("txVfo")).toString().toStdString(),
      .split = radio.value(QStringLiteral("split")).toBool(),
  };
  const QJsonObject vfos = radio.value(QStringLiteral("vfos")).toObject();
  for (auto iterator = vfos.constBegin(); iterator != vfos.constEnd(); ++iterator) {
    state.vfos.push_back({.id = iterator.key().toStdString(),
                          .frequency_hz = jsonFrequency(
                              iterator.value().toObject().value(
                                  QStringLiteral("frequency")))});
  }
  for (const auto& command :
       radio.value(QStringLiteral("availableCommands")).toArray()) {
    state.available_commands.push_back(command.toString().toStdString());
  }
  return state;
}

}  // namespace cwassistant::desktop
