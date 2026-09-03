#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>

#include "updates/callsign_database_updater.hpp"

namespace {

struct ScriptedResponse {
  int status{200};
  QByteArray body;
  QNetworkReply::NetworkError error{QNetworkReply::NoError};
  QString error_text;
  QUrl redirect;
};

class ScriptedReply final : public QNetworkReply {
public:
  ScriptedReply(const QNetworkAccessManager::Operation operation,
                const QNetworkRequest &request, ScriptedResponse response,
                QObject *parent)
      : QNetworkReply(parent), body_(std::move(response.body)) {
    setOperation(operation);
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    setHeader(QNetworkRequest::ContentLengthHeader, body_.size());
    if (!response.redirect.isEmpty()) {
      setAttribute(QNetworkRequest::RedirectionTargetAttribute,
                   response.redirect);
    }
    if (response.error != QNetworkReply::NoError) {
      setError(response.error, response.error_text.isEmpty()
                                   ? QStringLiteral("scripted network error")
                                   : response.error_text);
    }
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0, this, [this] {
      if (!body_.isEmpty())
        emit readyRead();
      if (isFinished())
        return;
      setFinished(true);
      emit finished();
    });
  }

  void abort() override {
    if (isFinished())
      return;
    setError(QNetworkReply::OperationCanceledError,
             QStringLiteral("scripted request aborted"));
    setFinished(true);
    QTimer::singleShot(0, this, [this] { emit finished(); });
  }

  [[nodiscard]] qint64 bytesAvailable() const override {
    return body_.size() - offset_ + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char *destination, const qint64 maximum) override {
    if (offset_ >= body_.size())
      return -1;
    const qint64 count = std::min(maximum, body_.size() - offset_);
    std::memcpy(destination, body_.constData() + offset_,
                static_cast<std::size_t>(count));
    offset_ += count;
    return count;
  }

private:
  QByteArray body_;
  qint64 offset_{0};
};

class ScriptedNetworkAccessManager final : public QNetworkAccessManager {
public:
  struct ObservedRequest {
    Operation operation;
    QNetworkRequest request;
  };

  void enqueue(ScriptedResponse response) {
    responses_.push_back(std::move(response));
  }

  [[nodiscard]] const std::vector<ObservedRequest> &requests() const noexcept {
    return requests_;
  }
  [[nodiscard]] int unexpectedRequests() const noexcept {
    return unexpected_requests_;
  }

protected:
  QNetworkReply *createRequest(Operation operation,
                               const QNetworkRequest &request,
                               QIODevice *) override {
    requests_.push_back({operation, request});
    ScriptedResponse response;
    if (responses_.empty()) {
      ++unexpected_requests_;
      response.status = 0;
      response.error = QNetworkReply::ProtocolUnknownError;
      response.error_text = QStringLiteral("no scripted response");
    } else {
      response = std::move(responses_.front());
      responses_.pop_front();
    }
    return new ScriptedReply(operation, request, std::move(response), this);
  }

private:
  std::deque<ScriptedResponse> responses_;
  std::vector<ObservedRequest> requests_;
  int unexpected_requests_{0};
};

int failures = 0;

void expect(const bool condition, const char *message) {
  if (condition)
    return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

bool waitUntil(const std::function<bool()> &predicate,
               const int timeout_ms = 1'000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QString sha256(const QByteArray &payload) {
  return QString::fromLatin1(
      QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QByteArray metadata(const QByteArray &payload,
                    const QString &expected_hash = {},
                    const qint64 declared_size = -1) {
  return QStringLiteral(
             R"([{"name":"MASTER.SCP","size":%1,"etag":"%2","modified":"2026-09-02T00:03:57.263277242Z"}])")
      .arg(declared_size >= 0 ? declared_size : payload.size())
      .arg(expected_hash.isEmpty() ? sha256(payload) : expected_hash)
      .toUtf8();
}

bool writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
         file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void clearSettings() {
  QSettings settings;
  settings.clear();
  settings.sync();
}

void testDisabledModeMakesNoRequest(const QString &path) {
  clearSettings();
  ScriptedNetworkAccessManager network;
  cwassistant::desktop::CallsignDatabaseUpdater updater(
      &network, QUrl(QStringLiteral("https://provider.test")), path);
  updater.checkAndInstallIfDue();
  updater.checkForUpdates();
  expect(network.requests().empty(),
         "disabled managed mode performs no network request");
}

void testAutomaticOptOutMakesNoRequest(const QString &path) {
  clearSettings();
  ScriptedNetworkAccessManager network;
  cwassistant::desktop::CallsignDatabaseUpdater updater(
      &network, QUrl(QStringLiteral("https://provider.test")), path);
  updater.setManagedEnabled(true);
  updater.setAutoUpdateEnabled(false);
  updater.checkAndInstallIfDue();
  expect(network.requests().empty(),
         "automatic-update opt-out performs no scheduled request");
}

void testDisablingStopsScheduledRetry(const QString &path) {
  clearSettings();
  ScriptedNetworkAccessManager network;
  network.enqueue({.status = 0,
                   .error = QNetworkReply::TimeoutError,
                   .error_text = QStringLiteral("offline")});
  cwassistant::desktop::CallsignDatabaseUpdater updater(
      &network, QUrl(QStringLiteral("https://provider.test")), path);
  updater.setManagedEnabled(true);
  updater.checkForUpdates();
  expect(waitUntil([&] {
           return network.requests().size() == 1 &&
                  updater.statusMessage().contains(QStringLiteral("retrying"),
                                                   Qt::CaseInsensitive);
         }),
         "first failure schedules a bounded retry");
  updater.setManagedEnabled(false);
  expect(waitUntil([&] { return !updater.checking(); }),
         "disabling managed mode cancels the scheduled retry state");
  expect(network.requests().size() == 1,
         "no retry request is issued after managed mode is disabled");
}

void testDiscoveryAndDownload(const QString &path) {
  clearSettings();
  QFile::remove(path);
  const QByteArray payload(
      "!!Order,1,1\r\n#\r\n# Super Check Partial\r\n"
      "# Release 2026.09.02\r\nW1AW\r\nEM90ZMV\r\nEA1EYL\r\n");
  ScriptedNetworkAccessManager network;
  network.enqueue({.status = 200, .body = metadata(payload)});
  network.enqueue({.status = 200, .body = payload});
  cwassistant::desktop::CallsignDatabaseUpdater updater(
      &network, QUrl(QStringLiteral("https://provider.test")), path);
  updater.setManagedEnabled(true);
  int installed = 0;
  QObject::connect(
      &updater,
      &cwassistant::desktop::CallsignDatabaseUpdater::databaseInstalled,
      &updater, [&installed](const QString &) { ++installed; });
  updater.checkAndInstallIfDue();

  expect(waitUntil([&] { return installed == 1; }),
         "valid discovery and payload install completes");
  expect(readFile(path) == payload, "installed bytes match verified payload");
  expect(updater.installedFilePath() == path,
         "successful install exposes managed file path");
  expect(updater.installedVersion() ==
             QStringLiteral("2026-09-02T00:03:57.263277242Z"),
         "successful install persists provider version");
  expect(!updater.updateAvailable() && !updater.checking() &&
             !updater.downloading(),
         "successful install reaches an idle up-to-date state");
  expect(network.requests().size() == 2 &&
             network.requests()[0].request.url() ==
                 QUrl(QStringLiteral("https://provider.test/api/v1/files")) &&
             network.requests()[1].request.url() ==
                 QUrl(QStringLiteral(
                     "https://provider.test/downloads/MASTER.SCP")),
         "updater uses only injected discovery and download URLs");
  expect(network.unexpectedRequests() == 0,
         "success flow performs no unscripted request");
}

void testConditionalRequestAndNotModified(const QString &path) {
  clearSettings();
  const QByteArray existing("W1AW\nEM90ZMV\n");
  expect(writeFile(path, existing), "conditional fixture is written");
  ScriptedNetworkAccessManager network;
  network.enqueue({.status = 200, .body = metadata(existing)});
  network.enqueue({.status = 304});
  cwassistant::desktop::CallsignDatabaseUpdater updater(
      &network, QUrl(QStringLiteral("https://provider.test")), path);
  updater.setManagedEnabled(true);
  updater.checkForUpdates();
  expect(waitUntil([&] {
           return network.requests().size() == 1 && !updater.checking();
         }),
         "matching discovery reports up to date");
  updater.updateDatabase();
  expect(waitUntil([&] {
           return network.requests().size() == 2 && !updater.downloading();
         }),
         "conditional 304 completes");
  const QByteArray validator = QByteArrayLiteral("\"") +
                               sha256(existing).toLatin1() +
                               QByteArrayLiteral("\"");
  expect(network.requests()[1].request.rawHeader("If-None-Match") == validator,
         "artifact request sends the installed SHA-256 as ETag validator");
  expect(readFile(path) == existing,
         "304 response leaves the installed database unchanged");
  expect(updater.statusMessage().contains(QStringLiteral("up to date")),
         "304 response reports up-to-date state");
  expect(network.unexpectedRequests() == 0,
         "conditional flow performs no unscripted request");
}

void testRejectedPayloadsPreserveExistingFile(const QString &path) {
  const QByteArray existing("W1AW\nOLD1A\n");

  clearSettings();
  expect(writeFile(path, existing), "checksum fixture is written");
  const QByteArray expected("K1ABC\n");
  const QByteArray corrupted("N0XYZ\n");
  ScriptedNetworkAccessManager checksum_network;
  checksum_network.enqueue(
      {.status = 200, .body = metadata(corrupted, sha256(expected))});
  checksum_network.enqueue({.status = 200, .body = corrupted});
  cwassistant::desktop::CallsignDatabaseUpdater checksum_updater(
      &checksum_network, QUrl(QStringLiteral("https://provider.test")), path);
  checksum_updater.setManagedEnabled(true);
  checksum_updater.updateDatabase();
  expect(waitUntil([&] {
           return checksum_network.requests().size() == 2 &&
                  !checksum_updater.downloading();
         }),
         "checksum mismatch finishes without installing");
  expect(checksum_updater.statusMessage().contains(QStringLiteral("checksum"),
                                                   Qt::CaseInsensitive),
         "checksum mismatch is visible");
  expect(readFile(path) == existing,
         "checksum mismatch preserves the existing file");

  clearSettings();
  expect(writeFile(path, existing), "validation fixture is written");
  const QByteArray malformed("# comments only\nNOT_A_CALL\n");
  ScriptedNetworkAccessManager validation_network;
  validation_network.enqueue({.status = 200, .body = metadata(malformed)});
  validation_network.enqueue({.status = 200, .body = malformed});
  cwassistant::desktop::CallsignDatabaseUpdater validation_updater(
      &validation_network, QUrl(QStringLiteral("https://provider.test")), path);
  validation_updater.setManagedEnabled(true);
  validation_updater.updateDatabase();
  expect(waitUntil([&] {
           return validation_network.requests().size() == 2 &&
                  !validation_updater.downloading();
         }),
         "invalid database contents finish without installing");
  expect(validation_updater.statusMessage().contains(
             QStringLiteral("validation"), Qt::CaseInsensitive),
         "content validation failure is visible");
  expect(readFile(path) == existing,
         "content validation failure preserves the existing file");
}

void testLimitsRedirectAndOfflineFallback(const QString &path) {
  const QByteArray existing("W1AW\nOLD1A\n");

  clearSettings();
  expect(writeFile(path, existing), "size-limit fixture is written");
  ScriptedNetworkAccessManager size_network;
  size_network.enqueue({.status = 200,
                        .body = metadata("X", QString(64, QLatin1Char('a')),
                                         32LL * 1'024LL * 1'024LL + 1)});
  cwassistant::desktop::CallsignDatabaseUpdater size_updater(
      &size_network, QUrl(QStringLiteral("https://provider.test")), path);
  size_updater.setManagedEnabled(true);
  size_updater.updateDatabase();
  expect(waitUntil([&] { return !size_updater.checking(); }),
         "oversized discovery metadata is rejected");
  expect(size_network.requests().size() == 1 && readFile(path) == existing,
         "oversized metadata cannot start a download or replace local data");

  clearSettings();
  expect(writeFile(path, existing), "download-limit fixture is written");
  const QByteArray oversized_payload(32 * 1'024 * 1'024 + 1, 'X');
  ScriptedNetworkAccessManager download_limit_network;
  download_limit_network.enqueue(
      {.status = 200, .body = metadata("X", QString(64, QLatin1Char('a')), 1)});
  download_limit_network.enqueue({.status = 200, .body = oversized_payload});
  cwassistant::desktop::CallsignDatabaseUpdater download_limit_updater(
      &download_limit_network, QUrl(QStringLiteral("https://provider.test")),
      path);
  download_limit_updater.setManagedEnabled(true);
  download_limit_updater.updateDatabase();
  expect(waitUntil(
             [&] {
               return download_limit_network.requests().size() == 2 &&
                      !download_limit_updater.downloading();
             },
             2'000),
         "streamed payload over the byte cap is aborted");
  expect(download_limit_updater.statusMessage().contains(
             QStringLiteral("32 MiB")) &&
             readFile(path) == existing,
         "streaming cap failure preserves the existing database");

  clearSettings();
  ScriptedNetworkAccessManager redirect_network;
  redirect_network.enqueue(
      {.status = 302,
       .redirect = QUrl(QStringLiteral("https://different.test/files"))});
  cwassistant::desktop::CallsignDatabaseUpdater redirect_updater(
      &redirect_network, QUrl(QStringLiteral("https://provider.test")), path);
  redirect_updater.setManagedEnabled(true);
  redirect_updater.checkForUpdates();
  expect(waitUntil([&] { return !redirect_updater.checking(); }),
         "cross-origin redirect is rejected");
  expect(redirect_network.requests().size() == 1 &&
             redirect_updater.statusMessage().contains(
                 QStringLiteral("redirect"), Qt::CaseInsensitive),
         "unsafe redirect never reaches another origin");

  clearSettings();
  expect(writeFile(path, existing), "offline fixture is written");
  ScriptedNetworkAccessManager offline_network;
  for (int attempt = 0; attempt < 3; ++attempt) {
    offline_network.enqueue({.status = 0,
                             .error = QNetworkReply::TimeoutError,
                             .error_text = QStringLiteral("offline")});
  }
  cwassistant::desktop::CallsignDatabaseUpdater offline_updater(
      &offline_network, QUrl(QStringLiteral("https://provider.test")), path);
  offline_updater.setManagedEnabled(true);
  offline_updater.checkForUpdates();
  expect(waitUntil(
             [&] {
               return offline_network.requests().size() == 3 &&
                      !offline_updater.checking();
             },
             4'000),
         "transient offline failure stops after bounded retries");
  expect(readFile(path) == existing &&
             offline_updater.installedFilePath() == path,
         "offline failure retains the usable local database");
  expect(offline_network.unexpectedRequests() == 0,
         "offline retry flow remains fully scripted");
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("CW Assistant Tests"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("invalid.test"));
  QCoreApplication::setApplicationName(
      QStringLiteral("Callsign Database Updater Test"));
  QCoreApplication::setApplicationVersion(QStringLiteral("test"));
  QStandardPaths::setTestModeEnabled(true);

  QTemporaryDir directory;
  if (!directory.isValid())
    return 90;
  const QString path = directory.filePath(QStringLiteral("MASTER.SCP"));

  testDisabledModeMakesNoRequest(path);
  testAutomaticOptOutMakesNoRequest(path);
  testDisablingStopsScheduledRetry(path);
  testDiscoveryAndDownload(path);
  testConditionalRequestAndNotModified(path);
  testRejectedPayloadsPreserveExistingFile(path);
  testLimitsRedirectAndOfflineFallback(path);

  clearSettings();
  QFile::remove(path);
  if (failures == 0) {
    std::cout << "Callsign database updater tests passed\n";
    return 0;
  }
  std::cerr << failures << " callsign database updater test(s) failed\n";
  return 1;
}
