#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <vector>

#include "cwassistant/core/cw_spot_registry.hpp"

namespace cwassistant::desktop {

using CwSpotBatch = std::vector<cwassistant::core::CwSpot>;

// Nanoseconds since the Unix epoch.
//
// Every timestamp that reaches CwSpotRegistry -- the observation times parsed
// out of a feed and the "now" passed to expire() and near() -- has to be read
// from one clock, or a spot's age is the difference between two unrelated
// origins and the retention window means nothing. A steady clock cannot be
// used: an observation time arrives from a remote station as a wall-clock
// instant, and only a wall clock can be compared with it.
[[nodiscard]] std::uint64_t currentUnixTimeNs() noexcept;

// What one parsed response produced. A body is accepted or rejected whole,
// so a caller that sees accepted == false must apply nothing from it; the
// per-record counts describe only what happened inside an accepted body.
struct DxSpotBatch {
  bool accepted{false};
  // Empty when the body was accepted. Otherwise it says, in operator-facing
  // words, why the whole body was discarded.
  QString rejection_reason;
  CwSpotBatch spots;
  int accepted_records{0};
  // Records inside an accepted body that were individually unusable -- no
  // callsign, no frequency, or a frequency outside the radio spectrum. They
  // are counted rather than guessed at, because a spot is corroboration and a
  // repaired one corroborates nothing.
  int rejected_records{0};
};

// Polls a read-only HTTPS endpoint for DX spots and publishes them as
// normalised core CwSpot values.
//
// This class is strictly receive-only in both directions. It issues nothing
// but GET requests, carries no credentials, and its output feeds a corroboration
// store: a spot may say that somebody else heard a station on a frequency, and
// it may never replace, rewrite, or auto-fill a decoded callsign, nor reach the
// transmit path in any form.
//
// Accepted document shape -- a JSON array of spot objects, or an object whose
// "spots" member is that array, because both are common in the wild:
//
//   [
//     {
//       "callsign":    "DL1ABC",        // required
//       "frequencyHz": 14025300,        // hertz, exact
//       "frequency":   14025.3,         // kilohertz, the cluster convention
//       "time":        "2026-09-11T12:34:56Z",
//       "spotter":     "OH6BG-1",
//       "source":      "rbn"            // or "cluster"
//     }
//   ]
//
// Either a hertz key or a kilohertz key satisfies the frequency requirement.
// Unknown members are ignored, so a richer feed still parses.
class DxSpotProvider final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
  Q_PROPERTY(bool fetching READ fetching NOTIFY stateChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

 public:
  explicit DxSpotProvider(QObject* parent = nullptr);
  // The injected-manager constructor exists so tests can drive the fetch loop
  // without a network. The injected manager must outlive this object.
  DxSpotProvider(QNetworkAccessManager* network, QObject* parent = nullptr);

  [[nodiscard]] bool enabled() const noexcept;
  void setEnabled(bool enabled);
  [[nodiscard]] const QUrl& endpoint() const noexcept;
  void setEndpoint(const QUrl& endpoint);
  [[nodiscard]] int refreshSeconds() const noexcept;
  void setRefreshSeconds(int seconds);
  [[nodiscard]] bool fetching() const noexcept;
  [[nodiscard]] const QString& statusMessage() const noexcept;
  [[nodiscard]] int consecutiveFailures() const noexcept;

  // Fetches once, outside the poll cadence, if the provider is enabled and no
  // request is already in flight. Two overlapping fetches of the same feed
  // would only cost the provider bandwidth it does not own.
  Q_INVOKABLE void refreshNow();

  // The whole parse, with no network, no clock, and no member state, so that
  // every acceptance and rejection rule can be exercised directly.
  // received_ns dates a record whose own timestamp is missing or unreadable,
  // and bounds one whose timestamp is in the future.
  [[nodiscard]] static DxSpotBatch parseSpotDocument(
      const QByteArray& payload, std::uint64_t received_ns);

  // An endpoint has to be HTTPS and free of embedded credentials. The check
  // is public so a settings page can tell the operator before the first poll
  // that what was typed will never be contacted.
  [[nodiscard]] static bool isAcceptableEndpoint(const QUrl& url) noexcept;

  static constexpr qint64 kMaximumBodyBytes = 1LL * 1'024LL * 1'024LL;
  // A body carrying more spots than the registry can hold is refused whole
  // rather than truncated, because truncation is a partial application of a
  // body that was never valid for this consumer.
  static constexpr int kMaximumRecords = 4'096;
  static constexpr int kMinimumRefreshSeconds = 30;
  static constexpr int kMaximumRefreshSeconds = 600;

 signals:
  // One successfully parsed, non-empty batch. Delivery is a report, never an
  // instruction: the receiver decides what, if anything, it corroborates.
  void spotsReceived(const cwassistant::desktop::CwSpotBatch& spots);
  void stateChanged();

 private:
  static constexpr int kMaximumRedirects = 4;
  // Deliberately shorter than the shortest permitted poll interval, so a
  // stalled request always ends before the next one is due and the provider
  // cannot accumulate connections against a feed that has stopped answering.
  static constexpr int kTransferTimeoutMs = 15'000;

  void startPolling();
  void stopPolling();
  void requestSpots(const QUrl& url, int redirects_remaining);
  void handleReply(QNetworkReply* reply, int redirects_remaining);
  void finishFailure(QString message);
  void setStatus(QString message);
  [[nodiscard]] QNetworkRequest spotRequest(const QUrl& url) const;
  [[nodiscard]] bool isAllowedRedirectUrl(const QUrl& url) const noexcept;

  QNetworkAccessManager* network_{nullptr};
  QTimer poll_timer_;
  QUrl endpoint_;
  bool enabled_{false};
  int refresh_seconds_{120};
  QNetworkReply* active_reply_{nullptr};
  QByteArray payload_;
  bool oversized_{false};
  int consecutive_failures_{0};
  // Conditional-request state, so an unchanged feed costs one 304 rather than
  // a whole body every poll.
  QString entity_tag_;
  QString last_modified_;
  QString status_message_{QStringLiteral("DX spots are off.")};
};

}  // namespace cwassistant::desktop
