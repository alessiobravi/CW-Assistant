#include "dxcluster/dx_cluster_servers.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QStandardPaths>
#include <QVariantMap>

#include <cstdlib>
#include <utility>

namespace cwassistant::desktop {
namespace {

using cwassistant::core::CwSpotSource;

constexpr char kFileName[] = "dx-cluster-servers.txt";
constexpr char kResourcePath[] = ":/dictionaries/dx-cluster-servers.txt";

// The floor, not a copy of the shipped file.
//
// A packaged build whose resource fails to load must still offer the operator
// somewhere to connect rather than an empty dropdown, which is the failure the
// Morse alphabet already had once. Three entries are enough for that: the
// reverse-beacon feed, which is the only source here that is itself a
// receiver, and two clusters on different software so one node being down is
// not the end of it. Nobody should extend this to track
// `dictionaries/dx-cluster-servers.txt` -- two lists kept in step by hand is
// how they stop being in step. The shipped file is the list; this is the
// guarantee that something is offered when the shipped file cannot be read.
constexpr char kBuiltinServers[] =
    "Reverse Beacon Network (CW/RTTY)|telnet.reversebeacon.net|7000|rbn|"
    "|Skimmers worldwide, reporting CW with signal-to-noise and speed.\n"
    "VE7CC (CC Cluster, Vancouver)|dxc.ve7cc.net|23|cluster"
    "|SET/SKIMMER;SET/NOFT8;SET/NOFT4"
    "|CC Cluster node. Skimmer spots on, FT8 and FT4 suppressed.\n"
    "DXFun (Spain)|dxfun.com|8000|cluster|"
    "|Long-running European node with a large user base.\n";

bool isHostCharacter(const QChar character) noexcept {
  const char16_t value = character.unicode();
  return (value >= u'a' && value <= u'z') || (value >= u'A' && value <= u'Z') ||
      (value >= u'0' && value <= u'9') || value == u'-';
}

// A host has to be a bare hostname, because that is the only thing that can be
// handed to a socket. Rejecting everything else here is not pedantry: a field
// carrying a scheme, a port of its own, whitespace, or `user:password@host`
// would be read as a name by nothing and is far more likely to be an operator
// pasting a URL -- or something worse pasted into their file -- than a machine
// that exists.
bool isPlausibleHost(const QString& host) noexcept {
  if (host.isEmpty() || host.size() > 253) return false;
  qsizetype label_length = 0;
  for (qsizetype index = 0; index < host.size(); ++index) {
    const QChar character = host.at(index);
    if (character == u'.') {
      // An empty label means a leading dot or `..`, neither of which resolves.
      if (label_length == 0) return false;
      label_length = 0;
      continue;
    }
    if (!isHostCharacter(character)) return false;
    // A label may contain a hyphen but may not begin or end with one.
    if (character == u'-' && label_length == 0) return false;
    if (character == u'-' && index + 1 < host.size() &&
        host.at(index + 1) == u'.') {
      return false;
    }
    ++label_length;
  }
  return label_length > 0;
}

bool hasVisibleText(const QString& text) noexcept {
  for (const QChar character : text) {
    if (!character.isSpace()) return true;
  }
  return false;
}

// The operator's editable dictionary directory: the same one the CW
// vocabularies are seeded into on first run, and the same override the core
// loader honours, so there is one place on disk an operator has to know about.
QDir operatorDictionaryDirectory() {
  const char* override_directory = std::getenv("CWA_DICTIONARY_DIR");
  if (override_directory != nullptr && *override_directory != '\0') {
    return QDir(QString::fromLocal8Bit(override_directory));
  }
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppDataLocation) +
              QStringLiteral("/dictionaries"));
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
  return file.readAll();
}

}  // namespace

bool DxClusterServer::isValid() const noexcept {
  return hasVisibleText(name) && port != 0 && isPlausibleHost(host);
}

DxClusterServerList DxClusterServerList::fromText(const QString& text) {
  DxClusterServerList list;
  const QStringList lines = text.split(u'\n');
  for (const QString& raw_line : lines) {
    // trimmed() also removes the carriage return of a file written on Windows,
    // which an operator editing this on either platform will produce.
    const QString line = raw_line.trimmed();
    if (line.isEmpty() || line.startsWith(u'#')) continue;

    const QStringList fields = line.split(u'|');
    // Name, host, port and source are the line; login commands and note are
    // optional because a server may need neither.
    if (fields.size() < 4) continue;

    DxClusterServer server;
    server.name = fields.at(0).trimmed();
    server.host = fields.at(1).trimmed();

    bool port_read = false;
    const uint port = fields.at(2).trimmed().toUInt(&port_read);
    if (!port_read || port == 0U || port > 65'535U) continue;
    server.port = static_cast<std::uint16_t>(port);

    const QString source = fields.at(3).trimmed().toLower();
    if (source == QLatin1String("rbn")) {
      server.source = CwSpotSource::ReverseBeacon;
    } else if (source == QLatin1String("cluster")) {
      server.source = CwSpotSource::Cluster;
    } else {
      // Not a guess worth making. The two sources are weighed differently, and
      // labelling a human-spot feed as a receiver would let a typed callsign
      // carry the weight of a measured one.
      continue;
    }

    if (fields.size() > 4) {
      const QStringList commands = fields.at(4).split(u';');
      for (const QString& command : commands) {
        const QString trimmed = command.trimmed();
        if (trimmed.isEmpty()) continue;
        // A carriage return or newline inside a command is dropped, not
        // trimmed. Commands are written to a socket one line at a time, so an
        // interior CR does not stay inside the command: the server reads
        // everything after it as a further command of its own. This file is
        // operator-editable text, and `trimmed()` only touches the ends, so
        // without this an edited line reaches a stranger's machine as two.
        if (trimmed.contains(u'\r') || trimmed.contains(u'\n')) continue;
        server.login_commands.append(trimmed);
      }
    }
    // Everything after the fifth separator is the note, rejoined, so a '|'
    // inside prose does not silently truncate what the operator wrote.
    if (fields.size() > 5) {
      server.note = QStringList(fields.mid(5)).join(u'|').trimmed();
    }

    // A line that does not describe a reachable machine is dropped on its own.
    // The rest of the file still loads: one bad edit must never be the reason
    // an operator is left with no servers at all.
    if (!server.isValid()) continue;
    list.servers_.push_back(std::move(server));
  }
  return list;
}

DxClusterServerList DxClusterServerList::load() {
  const QDir directory = operatorDictionaryDirectory();
  const QString editable = directory.filePath(QString::fromLatin1(kFileName));

  // The operator's copy wins when it parses to something. "Parses to
  // something" rather than "exists" on purpose: a file truncated by a failed
  // write, or emptied by an editor, must not be able to take the server list
  // away, and the shipped copy behind it is always readable.
  const QByteArray operator_copy = readFile(editable);
  if (!operator_copy.isEmpty()) {
    DxClusterServerList parsed = fromText(QString::fromUtf8(operator_copy));
    if (!parsed.isEmpty()) return parsed;
  }

  const QByteArray bundled = readFile(QString::fromLatin1(kResourcePath));
  if (!bundled.isEmpty()) {
    DxClusterServerList parsed = fromText(QString::fromUtf8(bundled));
    if (!parsed.isEmpty()) {
      // Seed the editable copy so the file the operator is told to edit is
      // actually there to edit, and so an unusable one is replaced by what is
      // in force. A usable copy returned above and is never overwritten.
      if (QDir().mkpath(directory.absolutePath())) {
        QFile seed(editable);
        if (seed.open(QIODevice::WriteOnly)) seed.write(bundled);
      }
      return parsed;
    }
  }

  return fromText(QString::fromUtf8(kBuiltinServers));
}

const std::vector<DxClusterServer>& DxClusterServerList::servers()
    const noexcept {
  return servers_;
}

bool DxClusterServerList::isEmpty() const noexcept { return servers_.empty(); }

QVariantList DxClusterServerList::toVariantList() const {
  QVariantList entries;
  entries.reserve(static_cast<qsizetype>(servers_.size()));
  for (const DxClusterServer& server : servers_) {
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), server.name);
    entry.insert(QStringLiteral("host"), server.host);
    entry.insert(QStringLiteral("port"), static_cast<int>(server.port));
    entry.insert(QStringLiteral("source"),
                 server.source == CwSpotSource::ReverseBeacon
                     ? QStringLiteral("rbn")
                     : QStringLiteral("cluster"));
    entry.insert(QStringLiteral("note"), server.note);
    entries.append(entry);
  }
  return entries;
}

int DxClusterServerList::indexOf(const QString& host,
                                 const std::uint16_t port) const noexcept {
  for (std::size_t index = 0; index < servers_.size(); ++index) {
    const DxClusterServer& server = servers_[index];
    // Hostnames are case-insensitive, so a stored selection typed in either
    // case is still the offered entry rather than a custom one.
    if (server.port == port &&
        server.host.compare(host, Qt::CaseInsensitive) == 0) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

}  // namespace cwassistant::desktop
