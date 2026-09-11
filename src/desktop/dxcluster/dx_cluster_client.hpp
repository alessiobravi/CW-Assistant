#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <vector>

#include "cwassistant/core/cw_spot_registry.hpp"
#include "dxcluster/dx_cluster_servers.hpp"

namespace cwassistant::desktop {

using CwClusterSpotBatch = std::vector<cwassistant::core::CwSpot>;

// One parsed cluster line.
//
// A line that is not a spot -- a banner, a prompt, a talk message, a server
// notice -- is not an error, it is simply not a spot, and the two have to stay
// distinguishable or a noisy server would look like a broken parser.
struct DxClusterLine {
  bool is_spot{false};
  cwassistant::core::CwSpot spot;
  // Set when the line asks for the login callsign. Cluster software words this
  // differently on every implementation, so the prompt is recognised rather
  // than assumed, and the callsign is sent only in answer to one.
  bool is_login_prompt{false};
};

// A read-only telnet client for the DX cluster and reverse-beacon networks.
//
// Why telnet at all, when a spot provider already exists: the cluster network
// speaks telnet and nothing else. The public HTTPS feeds are aggregators, and
// the Reverse Beacon Network -- the one source here that is itself a receiver
// rather than a person -- publishes no JSON interface at all. Its live stream
// is telnet or nothing.
//
// What this sends, stated plainly because it is the one place CW Buddy is not
// silent: a cluster login requires a callsign, unencrypted, and this client
// sends the operator's callsign in answer to the server's prompt, followed by
// the configured setup commands for that server. It sends nothing else, ever.
// It issues no spots, announces nothing, answers no talk message, and replies
// to no command. The operator's callsign reaching a third party is the whole
// cost of the feature and the settings page says so before it connects.
//
// What it will not do is unchanged from the HTTPS provider: a spot is
// corroboration. It may never supply a callsign the decoder did not produce,
// rewrite a decoded character, or reach the transmit path in any form.
//
// Clusters are volunteer infrastructure shared by thousands of operators. One
// connection is held at a time, a failed connection backs off instead of
// retrying in a loop, and a server that closes the link is left alone for
// longer each time rather than being hammered.
class DxClusterClient final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

 public:
  explicit DxClusterClient(QObject* parent = nullptr);
  ~DxClusterClient() override;

  [[nodiscard]] bool enabled() const noexcept;
  void setEnabled(bool enabled);
  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] const QString& statusMessage() const noexcept;
  [[nodiscard]] int consecutiveFailures() const noexcept;

  // Selecting a different server, or changing the callsign, drops any live
  // connection and reconnects with the new setting when enabled.
  void setServer(const DxClusterServer& server);
  [[nodiscard]] const DxClusterServer& server() const noexcept;
  // The station's own callsign. Empty disables connection outright: a cluster
  // cannot be joined anonymously, and inventing a callsign would put a false
  // identity on somebody else's machine.
  void setLoginCallsign(const QString& callsign);
  [[nodiscard]] const QString& loginCallsign() const noexcept;

  // The band the receiver is on, as an ADIF band name -- "20m", "40m" --
  // from cwassistant::core::adif_band_from_frequency(). Empty means unknown.
  //
  // A server's login commands may contain the token {BAND}, which is replaced
  // with this before the command is sent, so a filter can be expressed once in
  // the server list and still follow the radio. Verified against DXSpider
  // today: `accept/spots 0 on 20m/cw` is accepted and reported back by
  // `sh/filter` as `filter0 accept on 20m/cw`.
  //
  // Changing the band re-sends every command containing the token, and only
  // those, because the operator asked for the filter to track the receiver and
  // a cluster that keeps sending the whole planet is not filtered at all. A
  // command without the token is setup, sent once at login.
  //
  // Server-side filtering does not replace filtering what arrives. The reverse
  // beacon network accepts no filter commands on its telnet port, so its feed
  // is filtered here or not at all.
  void setBandFilter(const QString& band_name);
  [[nodiscard]] const QString& bandFilter() const noexcept;

  // The whole line parse: no socket, no clock, no member state.
  //
  // received_ns dates a spot whose own timestamp cannot be read, and anchors
  // the HHMMz stamp the cluster sends, which carries no date at all. A stamp
  // that would land in the future is read as belonging to the previous day,
  // because a spot cannot have been heard after it arrived.
  [[nodiscard]] static DxClusterLine parseLine(
      const QString& line, std::uint64_t received_ns,
      cwassistant::core::CwSpotSource source);

  // A callsign acceptable to send as a login. Rejects anything that is not a
  // plausible amateur callsign so that a typo, a pasted sentence, or an
  // injected control sequence cannot be written to a stranger's server.
  [[nodiscard]] static bool isAcceptableLoginCallsign(const QString& callsign);

  // Spots are delivered in batches on a short timer rather than one line at a
  // time. A busy skimmer feed runs at several spots a second and a signal per
  // line would cost more in queued delivery than the parse itself.
  static constexpr int kBatchIntervalMs = 1'000;
  static constexpr int kMinimumReconnectSeconds = 15;
  static constexpr int kMaximumReconnectSeconds = 600;
  // A line longer than this is discarded rather than buffered. No spot is
  // remotely this long, and a server sending an unbounded line without a
  // newline must not be able to grow this process's memory without limit.
  static constexpr int kMaximumLineBytes = 4'096;
  // Silence longer than this means the link is dead even though the socket
  // still believes it is open, which is the usual way a cluster connection
  // fails. Every cluster sends something well inside this.
  static constexpr int kIdleTimeoutSeconds = 300;

 signals:
  // One batch of newly parsed spots. Delivery is a report, never an
  // instruction: the receiver decides what, if anything, it corroborates.
  void spotsReceived(const cwassistant::desktop::CwClusterSpotBatch& spots);
  void stateChanged();

 private:
  void openConnection();
  void closeConnection();
  void scheduleReconnect();
  void handleReadyRead();
  void handleLine(const QString& line);
  void flushBatch();
  void setStatus(QString message);
  void sendLine(const QString& text);
  // Login commands with {BAND} resolved. A command whose token cannot be
  // resolved, because the band is unknown, is not sent at all: a filter
  // command with a hole in it is a syntax error on somebody else's server.
  [[nodiscard]] QStringList resolvedCommands(bool band_dependent_only) const;

  QTcpSocket* socket_{nullptr};
  QTimer reconnect_timer_;
  QTimer batch_timer_;
  QTimer idle_timer_;
  DxClusterServer server_;
  QString login_callsign_;
  QString band_filter_;
  QByteArray buffer_;
  CwClusterSpotBatch pending_;
  bool enabled_{false};
  bool logged_in_{false};
  int consecutive_failures_{0};
  QString status_message_{QStringLiteral("DX cluster is off.")};
};

}  // namespace cwassistant::desktop
