#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

namespace cwassistant::desktop {

// Downloads and atomically installs the managed Super Check Partial
// MASTER.SCP cache. The installed file remains advisory decoder input: this
// class has no access to channel verification, transcript, or radio control.
class CallsignDatabaseUpdater final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool managedEnabled READ managedEnabled WRITE setManagedEnabled
                 NOTIFY managedEnabledChanged)
  Q_PROPERTY(bool autoUpdateEnabled READ autoUpdateEnabled WRITE
                 setAutoUpdateEnabled NOTIFY autoUpdateEnabledChanged)
  Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
  Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
  Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
  Q_PROPERTY(QString lastCheckedText READ lastCheckedText NOTIFY stateChanged)
  Q_PROPERTY(
      QString installedFilePath READ installedFilePath NOTIFY stateChanged)
  Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY stateChanged)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)

public:
  explicit CallsignDatabaseUpdater(QObject *parent = nullptr);
  // The injected manager/origin constructor exists for deterministic tests.
  // Production uses the canonical HTTPS provider through the constructor
  // above. The injected manager must outlive this object.
  CallsignDatabaseUpdater(QNetworkAccessManager *network, QUrl provider_origin,
                          QString managed_file_path, QObject *parent = nullptr);

  [[nodiscard]] bool managedEnabled() const noexcept;
  void setManagedEnabled(bool enabled);
  [[nodiscard]] bool autoUpdateEnabled() const noexcept;
  void setAutoUpdateEnabled(bool enabled);
  [[nodiscard]] bool checking() const noexcept;
  [[nodiscard]] bool downloading() const noexcept;
  [[nodiscard]] bool updateAvailable() const noexcept;
  [[nodiscard]] const QString &statusMessage() const noexcept;
  [[nodiscard]] QString lastCheckedText() const;
  [[nodiscard]] const QString &installedFilePath() const noexcept;
  [[nodiscard]] const QString &installedVersion() const noexcept;
  [[nodiscard]] const QString &availableVersion() const noexcept;

  Q_INVOKABLE void checkForUpdates();
  Q_INVOKABLE void updateDatabase();
  // Intended for one delayed startup call. It honors the persisted opt-out and
  // performs no request more often than once per 24 hours.
  Q_INVOKABLE void checkAndInstallIfDue();

signals:
  void stateChanged();
  void managedEnabledChanged();
  void autoUpdateEnabledChanged();
  void databaseInstalled(const QString &path);

private:
  static constexpr qint64 kMaximumBytes = 32LL * 1'024LL * 1'024LL;
  static constexpr qint64 kMaximumMetadataBytes = 1LL * 1'024LL * 1'024LL;
  static constexpr int kMaximumAttempts = 3;
  static constexpr int kMaximumRedirects = 4;

  void beginMetadataRequest();
  void requestMetadata(const QUrl &url, int redirects_remaining);
  void handleMetadataReply(QNetworkReply *reply, int redirects_remaining);
  void beginArtifactRequest();
  void requestArtifact(const QUrl &url, int redirects_remaining);
  void handleArtifactReply(QNetworkReply *reply, int redirects_remaining);
  void finishCheckFailure(QString message);
  void finishDownloadFailure(QString message);
  void finishUpToDate();
  void setStatus(QString message);
  void persistSuccessfulState(const QString &hash, const QString &modified);
  [[nodiscard]] bool installPayload(const QByteArray &payload,
                                    QString *error_message);
  [[nodiscard]] QString localFileHash() const;
  [[nodiscard]] static bool
  isTransientFailure(QNetworkReply::NetworkError error,
                     int http_status) noexcept;
  [[nodiscard]] static int retryDelayMs(int completed_attempts) noexcept;
  [[nodiscard]] static QString normalizedEtag(QString etag);
  [[nodiscard]] static QString versionText(const QString &modified);
  [[nodiscard]] bool isAllowedProviderUrl(const QUrl &url) const noexcept;
  [[nodiscard]] bool isAllowedRedirectUrl(const QUrl &url) const noexcept;

  QNetworkAccessManager *network_{nullptr};
  QUrl provider_origin_;
  bool managed_enabled_{false};
  bool auto_update_enabled_{true};
  bool checking_{false};
  bool downloading_{false};
  bool update_available_{false};
  bool install_after_check_{false};
  bool metadata_oversized_{false};
  bool artifact_oversized_{false};
  QString status_message_{QStringLiteral("Not checked yet")};
  QDateTime last_checked_;
  QString managed_file_path_;
  QString installed_file_path_;
  QString installed_hash_;
  QString installed_modified_;
  QString available_hash_;
  QString available_modified_;
  qint64 available_size_{0};
  QByteArray metadata_payload_;
  QByteArray artifact_payload_;
  int metadata_attempts_{0};
  int artifact_attempts_{0};
};

} // namespace cwassistant::desktop
