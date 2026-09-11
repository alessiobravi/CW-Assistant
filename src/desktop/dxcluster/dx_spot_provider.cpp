#include "dx_spot_provider.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkRequest>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <utility>

namespace cwassistant::desktop {
namespace {

// The lowest and highest frequencies a spot may claim. Below the first is the
// 2200 m band's lower neighbourhood, which no amateur CW report reaches, and
// above the second is nothing an amateur allocation contains. A value outside
// them is a unit error or a corrupted record rather than a station, and a
// corroborating overlay drawn at the wrong end of the spectrum is worse than
// an empty one.
using cwassistant::core::kMaximumSpotFrequencyHz;
using cwassistant::core::kMinimumSpotFrequencyHz;

// Real callsigns, including portable and prefix forms, fit comfortably inside
// these bounds; anything outside them is not a callsign this application can
// present next to a decoded one.
constexpr int kMinimumCallsignLength = 3;
constexpr int kMaximumCallsignLength = 16;
constexpr int kMaximumSpotterLength = 32;

// Epoch numbers below this are seconds and above it are milliseconds. The
// boundary is far past any plausible spot in seconds and far before any
// plausible spot in milliseconds, so no real timestamp is ambiguous here.
constexpr double kEpochSecondsUpperBound = 100'000'000'000.0;

[[nodiscard]] QString userAgent() {
  return QStringLiteral("CW-Buddy/%1 (+https://github.com/alessiobravi/"
                        "CW-Buddy)")
      .arg(QCoreApplication::applicationVersion());
}

[[nodiscard]] QUrl redirectedUrl(QNetworkReply* reply) {
  const auto target =
      reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
  return target.isEmpty() ? QUrl{} : reply->url().resolved(target);
}

// Reads what has arrived without ever letting the buffer grow past the limit,
// so a feed that answers with an endless body costs a bounded amount of memory
// and is refused rather than absorbed.
[[nodiscard]] bool drainBoundedBody(QNetworkReply* reply,
                                    QByteArray* destination,
                                    const qint64 limit) {
  constexpr qint64 kReadChunkBytes = 64 * 1'024;
  while (reply->bytesAvailable() > 0) {
    const qint64 remaining = limit - destination->size();
    if (remaining < 0) return false;
    const QByteArray chunk =
        reply->read(std::min(kReadChunkBytes, remaining + 1));
    if (chunk.isEmpty()) break;
    if (destination->size() + chunk.size() > limit) return false;
    destination->append(chunk);
  }
  return true;
}

// The first member present under any of the given names. Feeds disagree about
// spelling far more than they disagree about meaning, so the alternatives are
// listed rather than one being imposed.
[[nodiscard]] QJsonValue firstPresent(
    const QJsonObject& record, std::initializer_list<QLatin1String> keys) {
  for (const QLatin1String key : keys) {
    const QJsonValue value = record.value(key);
    if (!value.isUndefined() && !value.isNull()) return value;
  }
  // Explicitly undefined rather than a default-constructed QJsonValue, which
  // would be null and would read as a member that is present but empty.
  return QJsonValue(QJsonValue::Undefined);
}

// A number, or a string holding one. A feed that quotes its numbers is common
// enough that refusing it would reject usable data for a formatting habit.
[[nodiscard]] std::optional<double> numericValue(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  if (value.isString()) {
    bool converted = false;
    const double parsed = value.toString().trimmed().toDouble(&converted);
    if (converted) return parsed;
  }
  return std::nullopt;
}

// Upper-cased and syntactically checked, never repaired. A callsign that does
// not look like one is dropped, because presenting a mangled call beside a
// decoded one invites the operator to trust it.
[[nodiscard]] QString normalisedCallsign(const QJsonValue& value) {
  if (!value.isString()) return {};
  const QString text = value.toString().trimmed().toUpper();
  if (text.size() < kMinimumCallsignLength ||
      text.size() > kMaximumCallsignLength) {
    return {};
  }
  if (text.startsWith(QLatin1Char('/')) || text.endsWith(QLatin1Char('/')))
    return {};
  bool has_letter = false;
  bool has_digit = false;
  for (const QChar character : text) {
    if (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) {
      has_letter = true;
    } else if (character >= QLatin1Char('0') &&
               character <= QLatin1Char('9')) {
      has_digit = true;
    } else if (character != QLatin1Char('/')) {
      return {};
    }
  }
  return has_letter && has_digit ? text : QString{};
}

// Hertz, whichever way the feed expressed it.
//
// An explicitly named unit is always obeyed. The bare "frequency" member is
// read as kilohertz, which is what every DX cluster and reverse-beacon feed
// has always published -- 14025.3 rather than 14025300 -- so a feed that means
// hertz has to say so with the hertz-named member.
[[nodiscard]] std::optional<double> frequencyHertz(const QJsonObject& record) {
  const QJsonValue hertz =
      firstPresent(record, {QLatin1String("frequencyHz"),
                            QLatin1String("frequency_hz"),
                            QLatin1String("freqHz"), QLatin1String("freq_hz")});
  if (!hertz.isUndefined()) return numericValue(hertz);

  const QJsonValue kilohertz = firstPresent(
      record,
      {QLatin1String("frequencyKhz"), QLatin1String("frequency_khz"),
       QLatin1String("freqKhz"), QLatin1String("freq_khz"),
       QLatin1String("kHz"), QLatin1String("khz")});
  if (!kilohertz.isUndefined()) {
    const auto parsed = numericValue(kilohertz);
    return parsed ? std::optional<double>{*parsed * 1'000.0} : std::nullopt;
  }

  const QJsonValue conventional =
      firstPresent(record, {QLatin1String("frequency"), QLatin1String("freq"),
                            QLatin1String("qrg")});
  if (!conventional.isUndefined()) {
    const auto parsed = numericValue(conventional);
    return parsed ? std::optional<double>{*parsed * 1'000.0} : std::nullopt;
  }
  return std::nullopt;
}

// When the observation happened, on the same Unix-epoch clock the registry
// ages everything against.
//
// A missing or unreadable timestamp does not lose the record: the spot is
// dated to the moment the batch arrived, which is the most recent instant it
// could describe. A timestamp ahead of our own clock is pulled back to that
// same instant, because a spotter whose clock runs fast would otherwise
// publish a spot that outlives every retention window it is measured against.
[[nodiscard]] std::uint64_t observedNanoseconds(
    const QJsonObject& record, const std::uint64_t received_ns) {
  const QJsonValue value = firstPresent(
      record, {QLatin1String("time"), QLatin1String("timestamp"),
               QLatin1String("spotted_at"), QLatin1String("spottedAt"),
               QLatin1String("observed_at"), QLatin1String("when")});
  std::optional<qint64> epoch_ms;
  if (value.isString()) {
    const QString text = value.toString().trimmed();
    QDateTime moment = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!moment.isValid()) moment = QDateTime::fromString(text, Qt::ISODate);
    if (moment.isValid()) epoch_ms = moment.toMSecsSinceEpoch();
  } else if (const auto numeric = numericValue(value); numeric) {
    epoch_ms = static_cast<qint64>(*numeric < kEpochSecondsUpperBound
                                       ? *numeric * 1'000.0
                                       : *numeric);
  }
  if (!epoch_ms || *epoch_ms <= 0) return received_ns;
  // Named with the exact type rather than deduced. A `ULL` literal promotes
  // the product to `unsigned long long`, which on a platform where `uint64_t`
  // is `unsigned long` is a different type of the same width, and `std::min`
  // deduces one type from both arguments. It compiles on the platforms where
  // those two spellings happen to agree and fails on the one where they do not.
  const std::uint64_t observed_ns =
      static_cast<std::uint64_t>(*epoch_ms) * 1'000'000ULL;
  return std::min(observed_ns, received_ns);
}

// Who reported it, kept only as printable text. The spotter is shown to the
// operator and is never matched, corrected, or acted on, so it is sanitised
// rather than validated.
[[nodiscard]] QString normalisedSpotter(const QJsonObject& record) {
  const QJsonValue value = firstPresent(
      record, {QLatin1String("spotter"), QLatin1String("spotter_callsign"),
               QLatin1String("spotterCallsign"), QLatin1String("reporter"),
               QLatin1String("de")});
  if (!value.isString()) return {};
  QString text;
  text.reserve(kMaximumSpotterLength);
  for (const QChar character : value.toString().trimmed()) {
    if (text.size() >= kMaximumSpotterLength) break;
    if (character.isPrint() && character.unicode() < 128U)
      text.append(character.toUpper());
  }
  return text;
}

// Which kind of observation this is.
//
// An unnamed or unrecognised source is treated as a cluster spot rather than a
// reverse-beacon report. Claiming machine provenance for something whose
// provenance is unknown would present the stronger of the two kinds of
// evidence on no grounds at all.
[[nodiscard]] cwassistant::core::CwSpotSource spotSource(
    const QJsonObject& record) {
  const QJsonValue value = firstPresent(
      record, {QLatin1String("source"), QLatin1String("feed"),
               QLatin1String("origin"), QLatin1String("type")});
  if (!value.isString()) return cwassistant::core::CwSpotSource::Cluster;
  const QString text = value.toString().trimmed().toLower();
  if (text.contains(QLatin1String("rbn")) ||
      text.contains(QLatin1String("reverse")) ||
      text.contains(QLatin1String("skimmer")) ||
      text.contains(QLatin1String("beacon"))) {
    return cwassistant::core::CwSpotSource::ReverseBeacon;
  }
  return cwassistant::core::CwSpotSource::Cluster;
}

// The array of spot objects, whether the body is that array or an object that
// wraps it. Both shapes are in wide use and neither is ambiguous.
[[nodiscard]] bool spotArray(const QJsonDocument& document,
                            QJsonArray* records) {
  if (document.isArray()) {
    *records = document.array();
    return true;
  }
  if (document.isObject()) {
    const QJsonValue wrapped =
        firstPresent(document.object(),
                     {QLatin1String("spots"), QLatin1String("results"),
                      QLatin1String("data")});
    if (wrapped.isArray()) {
      *records = wrapped.toArray();
      return true;
    }
  }
  return false;
}

}  // namespace

std::uint64_t currentUnixTimeNs() noexcept {
  const qint64 epoch_ms = QDateTime::currentMSecsSinceEpoch();
  return epoch_ms <= 0 ? 0ULL
                       : static_cast<std::uint64_t>(epoch_ms) * 1'000'000ULL;
}

DxSpotProvider::DxSpotProvider(QObject* parent)
    : DxSpotProvider(nullptr, parent) {}

DxSpotProvider::DxSpotProvider(QNetworkAccessManager* network, QObject* parent)
    : QObject(parent), network_(network) {
  if (network_ == nullptr) network_ = new QNetworkAccessManager(this);
  poll_timer_.setSingleShot(false);
  poll_timer_.setInterval(refresh_seconds_ * 1'000);
  connect(&poll_timer_, &QTimer::timeout, this, [this] {
    // A tick that lands while the previous request is still open is dropped
    // rather than queued. Polling faster than the feed answers would build a
    // backlog of requests against somebody else's server.
    if (active_reply_ != nullptr) return;
    requestSpots(endpoint_, kMaximumRedirects);
  });
}

bool DxSpotProvider::enabled() const noexcept { return enabled_; }

void DxSpotProvider::setEnabled(const bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (enabled_) {
    startPolling();
  } else {
    stopPolling();
    consecutive_failures_ = 0;
    setStatus(QStringLiteral("DX spots are off."));
  }
  emit stateChanged();
}

const QUrl& DxSpotProvider::endpoint() const noexcept { return endpoint_; }

void DxSpotProvider::setEndpoint(const QUrl& endpoint) {
  if (endpoint_ == endpoint) return;
  endpoint_ = endpoint;
  // The conditional-request state describes the previous feed and says nothing
  // about this one; carrying it over could make a new endpoint answer 304 and
  // appear to be permanently empty.
  entity_tag_.clear();
  last_modified_.clear();
  consecutive_failures_ = 0;
  if (enabled_) {
    stopPolling();
    startPolling();
  }
  emit stateChanged();
}

int DxSpotProvider::refreshSeconds() const noexcept { return refresh_seconds_; }

void DxSpotProvider::setRefreshSeconds(const int seconds) {
  const int clamped =
      std::clamp(seconds, kMinimumRefreshSeconds, kMaximumRefreshSeconds);
  if (refresh_seconds_ == clamped) return;
  refresh_seconds_ = clamped;
  poll_timer_.setInterval(refresh_seconds_ * 1'000);
  emit stateChanged();
}

bool DxSpotProvider::fetching() const noexcept {
  return active_reply_ != nullptr;
}

const QString& DxSpotProvider::statusMessage() const noexcept {
  return status_message_;
}

int DxSpotProvider::consecutiveFailures() const noexcept {
  return consecutive_failures_;
}

void DxSpotProvider::refreshNow() {
  if (!enabled_ || active_reply_ != nullptr) return;
  requestSpots(endpoint_, kMaximumRedirects);
}

void DxSpotProvider::startPolling() {
  if (!isAcceptableEndpoint(endpoint_)) {
    setStatus(QStringLiteral(
        "Enter an HTTPS DX spot address with no embedded credentials."));
    return;
  }
  setStatus(QStringLiteral("Waiting for the first DX spot update…"));
  poll_timer_.start();
  // The first fetch is immediate so that enabling the feature shows something
  // before the first interval elapses; up to ten minutes of an apparently
  // broken feature would be indistinguishable from a broken one.
  requestSpots(endpoint_, kMaximumRedirects);
}

void DxSpotProvider::stopPolling() {
  poll_timer_.stop();
  payload_.clear();
  oversized_ = false;
  if (active_reply_ != nullptr) {
    QNetworkReply* const abandoned = active_reply_;
    active_reply_ = nullptr;
    // Detached before it is cancelled. A cancellation this class caused is not
    // news about the feed, and reporting it as a failed update would tell the
    // operator their spot server is broken when all they did was switch the
    // feature off or change its address.
    abandoned->disconnect(this);
    abandoned->abort();
    abandoned->deleteLater();
  }
}

QNetworkRequest DxSpotProvider::spotRequest(const QUrl& url) const {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  request.setTransferTimeout(kTransferTimeoutMs);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  // Nothing identifying the operator may leave this station for a read-only
  // public feed, so stored cookies are neither sent nor kept and no
  // authentication is ever reused.
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                       QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                       QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,
                       QNetworkRequest::Manual);
  if (!entity_tag_.isEmpty())
    request.setRawHeader("If-None-Match", entity_tag_.toLatin1());
  if (!last_modified_.isEmpty())
    request.setRawHeader("If-Modified-Since", last_modified_.toLatin1());
  return request;
}

void DxSpotProvider::requestSpots(const QUrl& url,
                                  const int redirects_remaining) {
  if (!enabled_ || active_reply_ != nullptr) return;
  if (!isAcceptableEndpoint(url)) {
    finishFailure(QStringLiteral(
        "The DX spot address must be HTTPS and carry no credentials."));
    return;
  }
  payload_.clear();
  oversized_ = false;
  // Receive-only: the single verb this class ever issues, with no body of any
  // kind attached to it.
  auto* reply = network_->get(spotRequest(url));
  active_reply_ = reply;
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    if (!drainBoundedBody(reply, &payload_, kMaximumBodyBytes)) {
      oversized_ = true;
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, redirects_remaining] {
            handleReply(reply, redirects_remaining);
          });
  emit stateChanged();
}

void DxSpotProvider::handleReply(QNetworkReply* reply,
                                 const int redirects_remaining) {
  if (active_reply_ == reply) active_reply_ = nullptr;
  if (!enabled_) {
    reply->deleteLater();
    payload_.clear();
    oversized_ = false;
    emit stateChanged();
    return;
  }
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QUrl redirect = redirectedUrl(reply);
  if (!redirect.isEmpty()) {
    reply->deleteLater();
    if (redirects_remaining <= 0 || !isAllowedRedirectUrl(redirect)) {
      finishFailure(
          QStringLiteral("The DX spot feed redirected somewhere unsafe."));
      return;
    }
    requestSpots(redirect, redirects_remaining - 1);
    return;
  }
  if (oversized_) {
    reply->deleteLater();
    finishFailure(
        QStringLiteral("The DX spot feed exceeded the 1 MiB safety limit."));
    return;
  }
  if (status == 304) {
    reply->deleteLater();
    payload_.clear();
    consecutive_failures_ = 0;
    // Nothing is emitted: the spots already held are still the current ones,
    // and their ages continue to advance where they are stored.
    setStatus(QStringLiteral("DX spots unchanged."));
    emit stateChanged();
    return;
  }
  if (reply->error() != QNetworkReply::NoError || status != 200) {
    const QString error = reply->errorString();
    reply->deleteLater();
    // No immediate retry and no backoff loop. The next scheduled poll is the
    // retry, so a feed that is down costs one request per interval and can
    // neither spin nor stall the application.
    finishFailure(QStringLiteral("DX spot update failed: ") + error);
    return;
  }
  if (!drainBoundedBody(reply, &payload_, kMaximumBodyBytes)) oversized_ = true;
  const QString received_tag =
      QString::fromLatin1(reply->rawHeader("ETag")).trimmed();
  const QString received_modified =
      QString::fromLatin1(reply->rawHeader("Last-Modified")).trimmed();
  reply->deleteLater();
  if (oversized_) {
    finishFailure(
        QStringLiteral("The DX spot feed exceeded the 1 MiB safety limit."));
    return;
  }

  const DxSpotBatch batch =
      parseSpotDocument(payload_, currentUnixTimeNs());
  payload_.clear();
  if (!batch.accepted) {
    finishFailure(batch.rejection_reason);
    return;
  }
  // Only a body that was accepted whole may update the conditional-request
  // state; remembering the validators of a body we refused would make the
  // next poll answer 304 and hide the problem.
  entity_tag_ = received_tag;
  last_modified_ = received_modified;
  consecutive_failures_ = 0;
  setStatus(batch.rejected_records == 0
                ? QStringLiteral("%1 DX spots received.")
                      .arg(batch.accepted_records)
                : QStringLiteral("%1 DX spots received, %2 unusable.")
                      .arg(batch.accepted_records)
                      .arg(batch.rejected_records));
  if (!batch.spots.empty()) emit spotsReceived(batch.spots);
  emit stateChanged();
}

void DxSpotProvider::finishFailure(QString message) {
  payload_.clear();
  oversized_ = false;
  if (consecutive_failures_ < std::numeric_limits<int>::max())
    ++consecutive_failures_;
  setStatus(std::move(message));
  emit stateChanged();
}

void DxSpotProvider::setStatus(QString message) {
  status_message_ = std::move(message);
}

bool DxSpotProvider::isAllowedRedirectUrl(const QUrl& url) const noexcept {
  // A redirect may move within the configured host and nowhere else. A feed
  // that wants to hand this station to another origin is refused, so a
  // compromised or mistyped endpoint cannot be pointed at an arbitrary server.
  return isAcceptableEndpoint(url) && endpoint_.isValid() &&
         url.host().compare(endpoint_.host(), Qt::CaseInsensitive) == 0 &&
         url.port(-1) == endpoint_.port(-1);
}

bool DxSpotProvider::isAcceptableEndpoint(const QUrl& url) noexcept {
  return url.isValid() && url.scheme() == QStringLiteral("https") &&
         !url.host().isEmpty() && url.userInfo().isEmpty();
}

DxSpotBatch DxSpotProvider::parseSpotDocument(
    const QByteArray& payload, const std::uint64_t received_ns) {
  DxSpotBatch batch;
  if (payload.isEmpty()) {
    batch.rejection_reason = QStringLiteral("The DX spot feed was empty.");
    return batch;
  }
  if (payload.size() > kMaximumBodyBytes) {
    batch.rejection_reason =
        QStringLiteral("The DX spot feed exceeded the 1 MiB safety limit.");
    return batch;
  }
  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    batch.rejection_reason =
        QStringLiteral("The DX spot feed was not valid JSON.");
    return batch;
  }
  QJsonArray records;
  if (!spotArray(document, &records)) {
    batch.rejection_reason =
        QStringLiteral("The DX spot feed did not contain a list of spots.");
    return batch;
  }
  if (records.size() > kMaximumRecords) {
    batch.rejection_reason =
        QStringLiteral("The DX spot feed listed more than %1 spots.")
            .arg(kMaximumRecords);
    return batch;
  }

  batch.spots.reserve(static_cast<std::size_t>(records.size()));
  for (const QJsonValue& value : records) {
    if (!value.isObject()) {
      ++batch.rejected_records;
      continue;
    }
    const QJsonObject record = value.toObject();
    const QString callsign = normalisedCallsign(
        firstPresent(record, {QLatin1String("callsign"),
                              QLatin1String("call"), QLatin1String("dx"),
                              QLatin1String("dx_call")}));
    if (callsign.isEmpty()) {
      ++batch.rejected_records;
      continue;
    }
    const auto frequency_hz = frequencyHertz(record);
    if (!frequency_hz || !std::isfinite(*frequency_hz) ||
        *frequency_hz < kMinimumSpotFrequencyHz ||
        *frequency_hz > kMaximumSpotFrequencyHz) {
      ++batch.rejected_records;
      continue;
    }

    cwassistant::core::CwSpot spot;
    spot.callsign = callsign.toStdString();
    spot.frequency_hz = *frequency_hz;
    spot.observed_ns = observedNanoseconds(record, received_ns);
    spot.spotter = normalisedSpotter(record).toStdString();
    spot.source = spotSource(record);
    batch.spots.push_back(std::move(spot));
    ++batch.accepted_records;
  }

  batch.accepted = true;
  return batch;
}

}  // namespace cwassistant::desktop
