#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <cstdint>
#include <vector>

#include "cwassistant/core/cw_spot_registry.hpp"

namespace cwassistant::desktop {

// One reachable DX cluster or reverse-beacon server.
//
// These are other people's machines, run by volunteers, and every field here
// exists so the application can be a well-behaved guest on them: the name is
// what the operator picks from, the source decides how much weight a spot from
// it carries, and the login commands are the ones that server needs before it
// will send what CW Buddy actually wants.
struct DxClusterServer {
  QString name;
  QString host;
  std::uint16_t port{0};
  cwassistant::core::CwSpotSource source{
      cwassistant::core::CwSpotSource::Cluster};
  // Sent once, after the login callsign is accepted. Cluster software differs
  // in what it sends by default -- some withhold skimmer spots, some send FT8
  // nobody here can use -- so the quiet, correct configuration for a CW
  // decoder is part of the server's description rather than hardcoded.
  QStringList login_commands;
  // Shown beside the entry. Says what the operator is joining, including
  // anything they ought to know before connecting.
  QString note;

  [[nodiscard]] bool isValid() const noexcept;
};

// The operator-editable list of servers.
//
// Compiled-in server lists rot: hosts move, ports change, and a machine that
// has gone away cannot be replaced without shipping a new build. The list is
// data for the same reason the CW vocabularies are, and is read from
// `dictionaries/dx-cluster-servers.txt`.
//
// File format, one server per line, fields separated by '|':
//
//   name | host | port | source | login commands | note
//
// `source` is `rbn` or `cluster`. Login commands are separated by ';' and may
// be empty. Blank lines and lines starting with '#' are ignored. A line that
// does not parse is skipped rather than taking the rest of the file with it,
// because one bad edit must not leave the operator with no servers at all.
class DxClusterServerList {
 public:
  // Reads the operator's copy when one exists, then the shipped resource, then
  // the built-in text. The order matters: an operator who edited the file gets
  // their edit, and one whose file is missing or corrupt still gets a list.
  [[nodiscard]] static DxClusterServerList load();

  // The whole parse, with no filesystem, so every acceptance and rejection
  // rule can be exercised directly.
  [[nodiscard]] static DxClusterServerList fromText(const QString& text);

  [[nodiscard]] const std::vector<DxClusterServer>& servers() const noexcept;
  [[nodiscard]] bool isEmpty() const noexcept;
  // Servers as QML-ready maps: name, host, port, source, note.
  [[nodiscard]] QVariantList toVariantList() const;
  // Index of the first server whose host and port match, or -1. Used to decide
  // whether a stored selection is still one of the offered entries or has
  // become a custom one.
  [[nodiscard]] int indexOf(const QString& host,
                            std::uint16_t port) const noexcept;

 private:
  std::vector<DxClusterServer> servers_;
};

}  // namespace cwassistant::desktop
