#include "callsign_database_updater.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <string_view>
#include <utility>

#include "cwassistant/core/offline_callsign_database.hpp"

namespace cwassistant::desktop {
namespace {

constexpr auto kProviderOrigin = "https://www.supercheckpartial.com";
constexpr auto kDatabaseName = "MASTER.SCP";

constexpr auto kAutoUpdateKey = "callsignDatabase/autoUpdateEnabled";
constexpr auto kManagedEnabledKey = "callsignDatabase/managedEnabled";
constexpr auto kLastCheckedKey = "callsignDatabase/lastCheckedIso";
constexpr auto kInstalledHashKey = "callsignDatabase/installedSha256";
constexpr auto kInstalledModifiedKey = "callsignDatabase/installedModified";

[[nodiscard]] QString userAgent() {
  return QStringLiteral("CW-Assistant/%1 (+https://github.com/alessiobravi/"
                        "CW-Assistant)")
      .arg(QCoreApplication::applicationVersion());
}

[[nodiscard]] QNetworkRequest providerRequest(const QUrl &url) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
  request.setTransferTimeout(20'000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  return request;
}

[[nodiscard]] QUrl redirectedUrl(QNetworkReply *reply) {
  const auto target =
      reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
  return target.isEmpty() ? QUrl{} : reply->url().resolved(target);
}

[[nodiscard]] bool drainBounded(QNetworkReply *reply, QByteArray *destination,
                                const qint64 limit) {
  constexpr qint64 kReadChunkBytes = 64 * 1'024;
  while (reply->bytesAvailable() > 0) {
    const qint64 remaining = limit - destination->size();
    if (remaining < 0)
      return false;
    const QByteArray chunk =
        reply->read(std::min(kReadChunkBytes, remaining + 1));
    if (chunk.isEmpty())
      break;
    if (destination->size() + chunk.size() > limit)
      return false;
    destination->append(chunk);
  }
  return true;
}

[[nodiscard]] bool isValidProviderTimestamp(const QString &text) {
  static const QRegularExpression timestamp(
      QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}"
                     "(?:\\.\\d{1,9})?Z$"));
  if (!timestamp.match(text).hasMatch())
    return false;
  QString milliseconds = text;
  const qsizetype decimal = milliseconds.indexOf(QLatin1Char('.'));
  if (decimal >= 0) {
    const qsizetype z = milliseconds.size() - 1;
    QString fraction = milliseconds.mid(decimal + 1, z - decimal - 1);
    fraction = fraction.left(3).leftJustified(3, QLatin1Char('0'));
    milliseconds =
        milliseconds.left(decimal + 1) + fraction + QStringLiteral("Z");
  }
  return QDateTime::fromString(milliseconds, Qt::ISODateWithMs).isValid();
}

} // namespace

CallsignDatabaseUpdater::CallsignDatabaseUpdater(QObject *parent)
    : CallsignDatabaseUpdater(nullptr,
                              QUrl(QString::fromLatin1(kProviderOrigin)),
                              QString{}, parent) {}

CallsignDatabaseUpdater::CallsignDatabaseUpdater(QNetworkAccessManager *network,
                                                 QUrl provider_origin,
                                                 QString managed_file_path,
                                                 QObject *parent)
    : QObject(parent), network_(network),
      provider_origin_(std::move(provider_origin)) {
  if (network_ == nullptr)
    network_ = new QNetworkAccessManager(this);
  if (managed_file_path.isEmpty()) {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!base.isEmpty()) {
      managed_file_path_ =
          QDir(base).filePath(QStringLiteral("callsign-databases/"
                                             "supercheckpartial/MASTER.SCP"));
    }
  } else {
    managed_file_path_ = std::move(managed_file_path);
  }

  QSettings settings;
  managed_enabled_ =
      settings.value(QString::fromLatin1(kManagedEnabledKey), false).toBool();
  auto_update_enabled_ =
      settings.value(QString::fromLatin1(kAutoUpdateKey), true).toBool();
  last_checked_ = QDateTime::fromString(
      settings.value(QString::fromLatin1(kLastCheckedKey)).toString(),
      Qt::ISODate);
  installed_hash_ = normalizedEtag(
      settings.value(QString::fromLatin1(kInstalledHashKey)).toString());
  installed_modified_ =
      settings.value(QString::fromLatin1(kInstalledModifiedKey)).toString();

  const QString actual_hash = localFileHash();
  if (actual_hash.isEmpty()) {
    installed_hash_.clear();
    installed_modified_.clear();
  } else if (actual_hash != installed_hash_) {
    // Trust the file only after hashing it. Metadata from a previous install
    // cannot describe a file that was externally replaced.
    installed_hash_ = actual_hash;
    installed_modified_.clear();
  }
  if (!actual_hash.isEmpty()) {
    installed_file_path_ = managed_file_path_;
    status_message_ =
        managed_enabled_
            ? QStringLiteral("Managed callsign database ready.")
            : QStringLiteral(
                  "Managed callsign database installed but disabled.");
  } else {
    status_message_ =
        managed_enabled_
            ? QStringLiteral(
                  "Managed callsign database enabled; check for an update.")
            : QStringLiteral("Managed callsign database disabled.");
  }
}

bool CallsignDatabaseUpdater::managedEnabled() const noexcept {
  return managed_enabled_;
}

void CallsignDatabaseUpdater::setManagedEnabled(const bool enabled) {
  if (managed_enabled_ == enabled)
    return;
  managed_enabled_ = enabled;
  QSettings settings;
  settings.setValue(QString::fromLatin1(kManagedEnabledKey), enabled);
  setStatus(enabled
                ? (installed_file_path_.isEmpty()
                       ? QStringLiteral("Managed callsign database enabled; "
                                        "check for an update.")
                       : QStringLiteral("Managed callsign database enabled."))
                : QStringLiteral("Managed callsign database disabled."));
  emit managedEnabledChanged();
  emit stateChanged();
}

bool CallsignDatabaseUpdater::autoUpdateEnabled() const noexcept {
  return auto_update_enabled_;
}

void CallsignDatabaseUpdater::setAutoUpdateEnabled(const bool enabled) {
  if (auto_update_enabled_ == enabled)
    return;
  auto_update_enabled_ = enabled;
  QSettings settings;
  settings.setValue(QString::fromLatin1(kAutoUpdateKey), enabled);
  emit autoUpdateEnabledChanged();
}

bool CallsignDatabaseUpdater::checking() const noexcept { return checking_; }
bool CallsignDatabaseUpdater::downloading() const noexcept {
  return downloading_;
}
bool CallsignDatabaseUpdater::updateAvailable() const noexcept {
  return update_available_;
}
const QString &CallsignDatabaseUpdater::statusMessage() const noexcept {
  return status_message_;
}
QString CallsignDatabaseUpdater::lastCheckedText() const {
  return last_checked_.isValid() ? last_checked_.toLocalTime().toString(
                                       QStringLiteral("yyyy-MM-dd HH:mm"))
                                 : QStringLiteral("Never checked");
}
const QString &CallsignDatabaseUpdater::installedFilePath() const noexcept {
  return installed_file_path_;
}
const QString &CallsignDatabaseUpdater::installedVersion() const noexcept {
  return installed_modified_;
}
const QString &CallsignDatabaseUpdater::availableVersion() const noexcept {
  return available_modified_;
}

void CallsignDatabaseUpdater::setStatus(QString message) {
  status_message_ = std::move(message);
}

void CallsignDatabaseUpdater::checkForUpdates() {
  if (checking_ || downloading_)
    return;
  if (!managed_enabled_) {
    setStatus(QStringLiteral("Enable the managed callsign database first."));
    emit stateChanged();
    return;
  }
  install_after_check_ = false;
  beginMetadataRequest();
}

void CallsignDatabaseUpdater::updateDatabase() {
  if (checking_ || downloading_)
    return;
  if (!managed_enabled_) {
    setStatus(QStringLiteral("Enable the managed callsign database first."));
    emit stateChanged();
    return;
  }
  if (available_hash_.isEmpty()) {
    install_after_check_ = true;
    beginMetadataRequest();
    return;
  }
  beginArtifactRequest();
}

void CallsignDatabaseUpdater::checkAndInstallIfDue() {
  if (!managed_enabled_ || !auto_update_enabled_ || checking_ || downloading_)
    return;
  if (last_checked_.isValid()) {
    const qint64 elapsed =
        last_checked_.secsTo(QDateTime::currentDateTimeUtc());
    if (elapsed >= 0 && elapsed < 24 * 60 * 60)
      return;
  }
  install_after_check_ = true;
  beginMetadataRequest();
}

void CallsignDatabaseUpdater::beginMetadataRequest() {
  if (!isAllowedProviderUrl(provider_origin_)) {
    setStatus(QStringLiteral(
        "Callsign database provider must use its configured HTTPS origin."));
    emit stateChanged();
    return;
  }
  checking_ = true;
  update_available_ = false;
  metadata_attempts_ = 0;
  setStatus(QStringLiteral("Checking the callsign database…"));
  emit stateChanged();
  requestMetadata(provider_origin_.resolved(QStringLiteral("/api/v1/files")),
                  kMaximumRedirects);
}

void CallsignDatabaseUpdater::requestMetadata(const QUrl &url,
                                              const int redirects_remaining) {
  if (!managed_enabled_) {
    finishCheckFailure(QStringLiteral("Managed callsign database disabled."));
    return;
  }
  metadata_payload_.clear();
  metadata_oversized_ = false;
  ++metadata_attempts_;
  auto *reply = network_->get(providerRequest(url));
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    if (!drainBounded(reply, &metadata_payload_, kMaximumMetadataBytes)) {
      metadata_oversized_ = true;
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, redirects_remaining] {
            handleMetadataReply(reply, redirects_remaining);
          });
}

void CallsignDatabaseUpdater::handleMetadataReply(
    QNetworkReply *reply, const int redirects_remaining) {
  if (!managed_enabled_) {
    reply->deleteLater();
    finishCheckFailure(QStringLiteral("Managed callsign database disabled."));
    return;
  }
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QUrl redirect = redirectedUrl(reply);
  if (!redirect.isEmpty()) {
    reply->deleteLater();
    if (redirects_remaining <= 0 || !isAllowedRedirectUrl(redirect)) {
      finishCheckFailure(QStringLiteral(
          "Callsign database check rejected an unsafe redirect."));
      return;
    }
    requestMetadata(redirect, redirects_remaining - 1);
    return;
  }
  if (metadata_oversized_) {
    reply->deleteLater();
    finishCheckFailure(QStringLiteral(
        "Callsign database metadata exceeded the 1 MiB safety limit."));
    return;
  }
  if (reply->error() != QNetworkReply::NoError || status != 200) {
    const bool retry = metadata_attempts_ < kMaximumAttempts &&
                       isTransientFailure(reply->error(), status);
    const QString error = reply->errorString();
    reply->deleteLater();
    if (retry) {
      setStatus(QStringLiteral(
          "Callsign database service is temporarily unavailable; retrying…"));
      emit stateChanged();
      QTimer::singleShot(retryDelayMs(metadata_attempts_), this, [this] {
        requestMetadata(
            provider_origin_.resolved(QStringLiteral("/api/v1/files")),
            kMaximumRedirects);
      });
      return;
    }
    finishCheckFailure(QStringLiteral("Callsign database check failed: ") +
                       error);
    return;
  }
  if (!isAllowedProviderUrl(reply->url())) {
    reply->deleteLater();
    finishCheckFailure(QStringLiteral(
        "Callsign database metadata came from an unexpected origin."));
    return;
  }

  if (!drainBounded(reply, &metadata_payload_, kMaximumMetadataBytes))
    metadata_oversized_ = true;
  reply->deleteLater();
  if (metadata_oversized_) {
    finishCheckFailure(QStringLiteral(
        "Callsign database metadata exceeded the 1 MiB safety limit."));
    return;
  }
  const QJsonDocument document = QJsonDocument::fromJson(metadata_payload_);
  metadata_payload_.clear();
  if (!document.isArray()) {
    finishCheckFailure(
        QStringLiteral("Callsign database metadata was malformed."));
    return;
  }

  QJsonObject selected;
  int matches = 0;
  for (const auto &value : document.array()) {
    if (!value.isObject())
      continue;
    const auto object = value.toObject();
    if (object.value(QStringLiteral("name")).toString() ==
        QString::fromLatin1(kDatabaseName)) {
      selected = object;
      ++matches;
    }
  }
  const qint64 size = selected.value(QStringLiteral("size")).toInteger(-1);
  const QString hash =
      normalizedEtag(selected.value(QStringLiteral("etag")).toString());
  const QString modified =
      selected.value(QStringLiteral("modified")).toString();
  if (matches != 1 || size <= 0 || size > kMaximumBytes || hash.isEmpty() ||
      !isValidProviderTimestamp(modified)) {
    finishCheckFailure(QStringLiteral(
        "Callsign database metadata did not contain one valid MASTER.SCP."));
    return;
  }

  available_hash_ = hash;
  available_modified_ = modified;
  available_size_ = size;
  last_checked_ = QDateTime::currentDateTimeUtc();
  QSettings settings;
  settings.setValue(QString::fromLatin1(kLastCheckedKey),
                    last_checked_.toString(Qt::ISODate));

  const QString actual_hash = localFileHash();
  update_available_ = actual_hash != available_hash_;
  checking_ = false;
  if (!update_available_) {
    persistSuccessfulState(available_hash_, available_modified_);
    finishUpToDate();
    return;
  }
  setStatus(actual_hash.isEmpty()
                ? QStringLiteral("Callsign database is ready to download.")
                : QStringLiteral("A newer callsign database is available."));
  emit stateChanged();
  if (install_after_check_)
    beginArtifactRequest();
}

void CallsignDatabaseUpdater::beginArtifactRequest() {
  if (available_hash_.isEmpty() || available_size_ <= 0) {
    finishDownloadFailure(QStringLiteral(
        "Check for a callsign database update before downloading."));
    return;
  }
  checking_ = false;
  downloading_ = true;
  artifact_attempts_ = 0;
  artifact_payload_.clear();
  artifact_oversized_ = false;
  setStatus(QStringLiteral("Downloading the callsign database…"));
  emit stateChanged();
  const QUrl url = provider_origin_.resolved(
      QStringLiteral("/downloads/") + QString::fromLatin1(kDatabaseName));
  requestArtifact(url, kMaximumRedirects);
}

void CallsignDatabaseUpdater::requestArtifact(const QUrl &url,
                                              const int redirects_remaining) {
  if (!managed_enabled_) {
    finishDownloadFailure(
        QStringLiteral("Managed callsign database disabled."));
    return;
  }
  artifact_payload_.clear();
  artifact_oversized_ = false;
  ++artifact_attempts_;
  auto request = providerRequest(url);
  if (!installed_hash_.isEmpty()) {
    request.setRawHeader("If-None-Match", QByteArrayLiteral("\"") +
                                              installed_hash_.toLatin1() +
                                              QByteArrayLiteral("\""));
  }
  auto *reply = network_->get(request);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
    if (!drainBounded(reply, &artifact_payload_, kMaximumBytes)) {
      artifact_oversized_ = true;
      reply->abort();
    }
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, redirects_remaining] {
            handleArtifactReply(reply, redirects_remaining);
          });
}

void CallsignDatabaseUpdater::handleArtifactReply(
    QNetworkReply *reply, const int redirects_remaining) {
  if (!managed_enabled_) {
    reply->deleteLater();
    finishDownloadFailure(
        QStringLiteral("Managed callsign database disabled."));
    return;
  }
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QUrl redirect = redirectedUrl(reply);
  if (!redirect.isEmpty()) {
    reply->deleteLater();
    if (redirects_remaining <= 0 || !isAllowedRedirectUrl(redirect)) {
      finishDownloadFailure(QStringLiteral(
          "Callsign database download rejected an unsafe redirect."));
      return;
    }
    requestArtifact(redirect, redirects_remaining - 1);
    return;
  }
  if (artifact_oversized_) {
    reply->deleteLater();
    finishDownloadFailure(QStringLiteral(
        "Callsign database download exceeded the 32 MiB safety limit."));
    return;
  }
  if (status == 304) {
    reply->deleteLater();
    if (localFileHash() == installed_hash_ &&
        installed_hash_ == available_hash_) {
      persistSuccessfulState(available_hash_, available_modified_);
      finishUpToDate();
    } else {
      finishDownloadFailure(QStringLiteral(
          "The server reported an unchanged database, but no matching local "
          "copy is available."));
    }
    return;
  }
  if (reply->error() != QNetworkReply::NoError || status != 200) {
    const bool retry = artifact_attempts_ < kMaximumAttempts &&
                       isTransientFailure(reply->error(), status);
    const QString error = reply->errorString();
    reply->deleteLater();
    if (retry) {
      setStatus(QStringLiteral(
          "Callsign database download was interrupted; retrying…"));
      emit stateChanged();
      QTimer::singleShot(retryDelayMs(artifact_attempts_), this, [this] {
        const QUrl url = provider_origin_.resolved(
            QStringLiteral("/downloads/") + QString::fromLatin1(kDatabaseName));
        requestArtifact(url, kMaximumRedirects);
      });
      return;
    }
    finishDownloadFailure(
        QStringLiteral("Callsign database download failed: ") + error);
    return;
  }
  if (!isAllowedProviderUrl(reply->url())) {
    reply->deleteLater();
    finishDownloadFailure(QStringLiteral(
        "Callsign database download came from an unexpected origin."));
    return;
  }
  if (!drainBounded(reply, &artifact_payload_, kMaximumBytes))
    artifact_oversized_ = true;
  reply->deleteLater();
  if (artifact_oversized_ || artifact_payload_.size() != available_size_) {
    finishDownloadFailure(QStringLiteral(
        "Callsign database download size did not match its metadata."));
    return;
  }
  const QString actual_hash = QString::fromLatin1(
      QCryptographicHash::hash(artifact_payload_, QCryptographicHash::Sha256)
          .toHex());
  if (actual_hash != available_hash_) {
    finishDownloadFailure(QStringLiteral(
        "Callsign database checksum verification failed; the download was "
        "discarded."));
    return;
  }
  QString error;
  if (!installPayload(artifact_payload_, &error)) {
    finishDownloadFailure(std::move(error));
    return;
  }
  artifact_payload_.clear();
  persistSuccessfulState(available_hash_, available_modified_);
  downloading_ = false;
  update_available_ = false;
  install_after_check_ = false;
  setStatus(QStringLiteral("Callsign database updated (%1).")
                .arg(versionText(installed_modified_)));
  emit stateChanged();
  emit databaseInstalled(installed_file_path_);
}

bool CallsignDatabaseUpdater::installPayload(const QByteArray &payload,
                                             QString *error_message) {
  cwassistant::core::OfflineCallsignDatabase validation;
  const auto imported = validation.importText(std::string_view(
      payload.constData(), static_cast<std::size_t>(payload.size())));
  if (!imported.accepted || imported.inserted_records == 0U ||
      imported.capacity_reached) {
    *error_message = QStringLiteral(
        "Callsign database contents failed validation; the existing copy was "
        "kept.");
    return false;
  }
  if (managed_file_path_.isEmpty()) {
    *error_message = QStringLiteral(
        "No writable application-data location is available for the managed "
        "callsign database.");
    return false;
  }
  const QFileInfo destination(managed_file_path_);
  if (!QDir().mkpath(destination.absolutePath())) {
    *error_message = QStringLiteral(
        "Could not create the managed callsign database directory.");
    return false;
  }
  QSaveFile file(managed_file_path_);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(payload) != payload.size() || !file.commit()) {
    file.cancelWriting();
    *error_message = QStringLiteral(
        "Could not atomically install the callsign database; the existing "
        "copy was kept.");
    return false;
  }
  installed_file_path_ = managed_file_path_;
  return true;
}

QString CallsignDatabaseUpdater::localFileHash() const {
  QFile file(managed_file_path_);
  if (!file.exists() || file.size() <= 0 || file.size() > kMaximumBytes ||
      !file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray payload = file.readAll();
  if (file.error() != QFileDevice::NoError || payload.size() != file.size())
    return {};
  cwassistant::core::OfflineCallsignDatabase validation;
  const auto imported = validation.importText(std::string_view(
      payload.constData(), static_cast<std::size_t>(payload.size())));
  if (!imported.accepted || imported.inserted_records == 0U ||
      imported.capacity_reached) {
    return {};
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

void CallsignDatabaseUpdater::persistSuccessfulState(const QString &hash,
                                                     const QString &modified) {
  installed_hash_ = hash;
  installed_modified_ = modified;
  QSettings settings;
  settings.setValue(QString::fromLatin1(kInstalledHashKey), installed_hash_);
  settings.setValue(QString::fromLatin1(kInstalledModifiedKey),
                    installed_modified_);
}

void CallsignDatabaseUpdater::finishCheckFailure(QString message) {
  checking_ = false;
  install_after_check_ = false;
  metadata_payload_.clear();
  metadata_oversized_ = false;
  setStatus(std::move(message));
  emit stateChanged();
}

void CallsignDatabaseUpdater::finishDownloadFailure(QString message) {
  checking_ = false;
  downloading_ = false;
  install_after_check_ = false;
  artifact_payload_.clear();
  artifact_oversized_ = false;
  setStatus(std::move(message));
  emit stateChanged();
}

void CallsignDatabaseUpdater::finishUpToDate() {
  checking_ = false;
  downloading_ = false;
  update_available_ = false;
  install_after_check_ = false;
  setStatus(QStringLiteral("Callsign database is up to date (%1).")
                .arg(versionText(installed_modified_)));
  emit stateChanged();
}

bool CallsignDatabaseUpdater::isTransientFailure(
    const QNetworkReply::NetworkError error, const int http_status) noexcept {
  if (http_status == 408 || http_status == 425 || http_status == 429 ||
      (http_status >= 500 && http_status <= 599)) {
    return true;
  }
  switch (error) {
  case QNetworkReply::ConnectionRefusedError:
  case QNetworkReply::RemoteHostClosedError:
  case QNetworkReply::HostNotFoundError:
  case QNetworkReply::TimeoutError:
  case QNetworkReply::TemporaryNetworkFailureError:
  case QNetworkReply::NetworkSessionFailedError:
  case QNetworkReply::UnknownNetworkError:
  case QNetworkReply::UnknownProxyError:
  case QNetworkReply::UnknownServerError:
    return true;
  default:
    return false;
  }
}

int CallsignDatabaseUpdater::retryDelayMs(
    const int completed_attempts) noexcept {
  if (completed_attempts <= 1)
    return 500;
  return completed_attempts == 2 ? 1'500 : 3'000;
}

QString CallsignDatabaseUpdater::normalizedEtag(QString etag) {
  etag = etag.trimmed();
  if (etag.startsWith(QStringLiteral("W/"), Qt::CaseInsensitive)) {
    etag.remove(0, 2);
  }
  if (etag.size() >= 2 && etag.front() == QLatin1Char('"') &&
      etag.back() == QLatin1Char('"')) {
    etag = etag.mid(1, etag.size() - 2);
  }
  static const QRegularExpression sha256(QStringLiteral("^[0-9a-fA-F]{64}$"));
  return sha256.match(etag).hasMatch() ? etag.toLower() : QString{};
}

QString CallsignDatabaseUpdater::versionText(const QString &modified) {
  return isValidProviderTimestamp(modified) ? modified.left(10)
                                            : QStringLiteral("unknown version");
}

bool CallsignDatabaseUpdater::isAllowedProviderUrl(
    const QUrl &url) const noexcept {
  const QUrl &origin = provider_origin_;
  return url.isValid() && url.scheme() == QStringLiteral("https") &&
         origin.scheme() == QStringLiteral("https") &&
         url.host().compare(origin.host(), Qt::CaseInsensitive) == 0 &&
         (url.port(-1) == origin.port(-1));
}

bool CallsignDatabaseUpdater::isAllowedRedirectUrl(
    const QUrl &url) const noexcept {
  return isAllowedProviderUrl(url) && url.userInfo().isEmpty();
}

} // namespace cwassistant::desktop
