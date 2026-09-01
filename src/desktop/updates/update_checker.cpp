#include "update_checker.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

#include <array>

namespace cwassistant::desktop {
namespace {

constexpr auto kManifestUrl =
    "https://github.com/alessiobravi/CW-Assistant/releases/download/"
    "continuous/latest.json";

[[nodiscard]] std::array<int, 3> parseVersion(const QString& text) noexcept {
  std::array<int, 3> version{0, 0, 0};
  static const QRegularExpression pattern(
      QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$"));
  const auto match = pattern.match(text);
  if (!match.hasMatch()) return version;
  version[0] = match.captured(1).toInt();
  version[1] = match.captured(2).toInt();
  version[2] = match.captured(3).toInt();
  return version;
}

[[nodiscard]] bool isNewerVersion(const QString& candidate,
                                  const QString& current) noexcept {
  return parseVersion(candidate) > parseVersion(current);
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      current_version_(QCoreApplication::applicationVersion()) {
  QSettings settings;
  auto_check_enabled_ =
      settings.value(QStringLiteral("updates/autoCheckEnabled"), true)
          .toBool();
  const auto last_checked_text =
      settings.value(QStringLiteral("updates/lastCheckedIso")).toString();
  if (!last_checked_text.isEmpty()) {
    last_checked_ = QDateTime::fromString(last_checked_text, Qt::ISODate);
  }
}

bool UpdateChecker::autoCheckEnabled() const noexcept {
  return auto_check_enabled_;
}
void UpdateChecker::setAutoCheckEnabled(const bool value) {
  if (auto_check_enabled_ == value) return;
  auto_check_enabled_ = value;
  QSettings settings;
  settings.setValue(QStringLiteral("updates/autoCheckEnabled"), value);
  emit autoCheckEnabledChanged();
}
bool UpdateChecker::checking() const noexcept { return checking_; }
bool UpdateChecker::updateAvailable() const noexcept {
  return update_available_;
}
const QString& UpdateChecker::currentVersion() const noexcept {
  return current_version_;
}
const QString& UpdateChecker::latestVersion() const noexcept {
  return latest_version_;
}
QString UpdateChecker::lastCheckedText() const {
  return last_checked_.isValid()
             ? last_checked_.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
             : QStringLiteral("Never checked");
}
const QString& UpdateChecker::statusMessage() const noexcept {
  return status_message_;
}
bool UpdateChecker::downloading() const noexcept { return downloading_; }
double UpdateChecker::downloadProgress() const noexcept {
  return download_progress_;
}
bool UpdateChecker::downloadVerified() const noexcept {
  return download_verified_;
}
const QString& UpdateChecker::downloadedFilePath() const noexcept {
  return downloaded_file_path_;
}
bool UpdateChecker::platformSupported() const noexcept {
  return !platformArtifactKey().isEmpty();
}

void UpdateChecker::setStatus(QString message) {
  status_message_ = std::move(message);
}

QString UpdateChecker::platformArtifactKey() {
#if defined(Q_OS_WIN)
  return QStringLiteral("windows11-x64-installer");
#elif defined(Q_OS_MACOS)
  return QSysInfo::currentCpuArchitecture().contains(QStringLiteral("arm"))
             ? QStringLiteral("macos-arm64")
             : QStringLiteral("macos-x64");
#elif defined(Q_OS_LINUX)
  return QFile::exists(QStringLiteral("/etc/debian_version"))
             ? QStringLiteral("debian-ubuntu-x64")
             : QStringLiteral("linux-x64-portable");
#else
  return QString();
#endif
}

void UpdateChecker::checkForUpdates() {
  if (checking_) return;
  checking_ = true;
  setStatus(QStringLiteral("Checking for updates…"));
  emit stateChanged();
  auto* reply = network_.get(QNetworkRequest(QUrl(QString::fromLatin1(
      kManifestUrl))));
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { handleManifestReply(reply); });
}

void UpdateChecker::handleManifestReply(QNetworkReply* reply) {
  checking_ = false;
  if (reply->error() != QNetworkReply::NoError) {
    setStatus(QStringLiteral("Update check failed: ") + reply->errorString());
    emit stateChanged();
    reply->deleteLater();
    return;
  }
  const auto document = QJsonDocument::fromJson(reply->readAll());
  reply->deleteLater();
  if (!document.isObject() ||
      !document.object().contains(QStringLiteral("version"))) {
    setStatus(QStringLiteral(
        "Update check failed: the published manifest was malformed"));
    emit stateChanged();
    return;
  }
  last_manifest_ = document.object();
  latest_version_ = last_manifest_.value(QStringLiteral("version")).toString();
  update_available_ = isNewerVersion(latest_version_, current_version_);
  last_checked_ = QDateTime::currentDateTime();
  QSettings settings;
  settings.setValue(QStringLiteral("updates/lastCheckedIso"),
                    last_checked_.toString(Qt::ISODate));
  setStatus(update_available_
                ? QStringLiteral("Update available: %1 (you have %2)")
                      .arg(latest_version_, current_version_)
                : QStringLiteral("Up to date (%1)").arg(current_version_));
  emit stateChanged();
}

void UpdateChecker::downloadUpdate() {
  if (downloading_) return;
  if (last_manifest_.isEmpty()) {
    setStatus(QStringLiteral("Check for updates first"));
    emit stateChanged();
    return;
  }
  const auto key = platformArtifactKey();
  if (key.isEmpty()) {
    setStatus(QStringLiteral(
        "Guided downloads are not available for this platform yet"));
    emit stateChanged();
    return;
  }
  const auto artifact_url =
      last_manifest_.value(QStringLiteral("artifacts")).toObject()
          .value(key)
          .toString();
  const auto checksums_url =
      last_manifest_.value(QStringLiteral("checksums")).toString();
  if (artifact_url.isEmpty() || checksums_url.isEmpty()) {
    setStatus(QStringLiteral(
        "The published manifest has no download for this platform"));
    emit stateChanged();
    return;
  }
  downloading_ = true;
  download_progress_ = 0.0;
  download_verified_ = false;
  downloaded_file_path_.clear();
  pending_artifact_url_ = artifact_url;
  setStatus(QStringLiteral("Downloading checksums…"));
  emit stateChanged();

  // Fetched sequentially, not concurrently: the artifact reply must not be
  // able to finish before pending_checksums_text_ is populated, since
  // handleArtifactReply() needs it to verify the download.
  auto* checksums_reply = network_.get(QNetworkRequest(QUrl(checksums_url)));
  connect(checksums_reply, &QNetworkReply::finished, this,
          [this, checksums_reply] { handleChecksumsReply(checksums_reply); });
}

void UpdateChecker::handleChecksumsReply(QNetworkReply* reply) {
  if (reply->error() != QNetworkReply::NoError) {
    reply->deleteLater();
    finishDownload(false,
                   QStringLiteral("Checksum fetch failed: ") +
                       reply->errorString());
    return;
  }
  pending_checksums_text_ = reply->readAll();
  reply->deleteLater();

  setStatus(QStringLiteral("Downloading update…"));
  emit stateChanged();
  auto* artifact_reply =
      network_.get(QNetworkRequest(QUrl(pending_artifact_url_)));
  connect(artifact_reply, &QNetworkReply::downloadProgress, this,
          [this](const qint64 received, const qint64 total) {
            if (total > 0) {
              download_progress_ = static_cast<double>(received) /
                                   static_cast<double>(total);
              emit stateChanged();
            }
          });
  connect(artifact_reply, &QNetworkReply::finished, this,
          [this, artifact_reply] { handleArtifactReply(artifact_reply); });
}

void UpdateChecker::handleArtifactReply(QNetworkReply* reply) {
  if (reply->error() != QNetworkReply::NoError) {
    finishDownload(false, QStringLiteral("Download failed: ") +
                              reply->errorString());
    reply->deleteLater();
    return;
  }
  const auto file_name =
      QFileInfo(QUrl(pending_artifact_url_).path()).fileName();
  const auto payload = reply->readAll();
  reply->deleteLater();

  const auto expected_hash = expectedHashFor(pending_checksums_text_, file_name);
  const auto local_hash =
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
  if (expected_hash.isEmpty()) {
    finishDownload(false, QStringLiteral(
                              "Could not find a published checksum for %1")
                              .arg(file_name));
    return;
  }
  if (QString::fromLatin1(local_hash).compare(expected_hash,
                                              Qt::CaseInsensitive) != 0) {
    finishDownload(false, QStringLiteral(
                              "Checksum mismatch for %1 — download was "
                              "corrupted or tampered with, discarded")
                              .arg(file_name));
    return;
  }

  auto download_dir =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (download_dir.isEmpty() || !QDir(download_dir).exists()) {
    download_dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(download_dir);
  }
  const auto save_path = QDir(download_dir).filePath(file_name);
  QFile file(save_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(payload) != payload.size()) {
    finishDownload(false,
                   QStringLiteral("Could not save the download to %1")
                       .arg(save_path));
    return;
  }
  file.close();
  downloaded_file_path_ = save_path;
  finishDownload(true, QStringLiteral("Downloaded and verified %1")
                            .arg(file_name));
}

void UpdateChecker::finishDownload(const bool success, QString message) {
  downloading_ = false;
  download_verified_ = success;
  if (!success) {
    download_progress_ = 0.0;
    if (!downloaded_file_path_.isEmpty()) {
      QFile::remove(downloaded_file_path_);
      downloaded_file_path_.clear();
    }
  } else {
    download_progress_ = 1.0;
  }
  setStatus(std::move(message));
  emit stateChanged();
}

QString UpdateChecker::expectedHashFor(const QByteArray& checksums_text,
                                       const QString& file_name) {
  const auto lines = QString::fromUtf8(checksums_text)
                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const auto& line : lines) {
    const auto trimmed = line.trimmed();
    const auto separator = trimmed.indexOf(QLatin1Char(' '));
    if (separator <= 0) continue;
    const auto hash = trimmed.left(separator);
    auto name = trimmed.mid(separator).trimmed();
    if (name.startsWith(QLatin1Char('*'))) name.remove(0, 1);
    if (name == file_name) return hash;
  }
  return {};
}

void UpdateChecker::openDownloadedFile() {
  if (downloaded_file_path_.isEmpty() || !download_verified_) return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(downloaded_file_path_));
}

void UpdateChecker::revealDownloadFolder() {
  if (downloaded_file_path_.isEmpty()) return;
  QDesktopServices::openUrl(
      QUrl::fromLocalFile(QFileInfo(downloaded_file_path_).absolutePath()));
}

}  // namespace cwassistant::desktop
