// Source contract: the server list is data, not code, and an operator is
// invited to edit it. Everything here follows from that.
//
// The file that ships has to parse -- it is the list every operator starts
// from, and a typo in it would be shipped as an empty dropdown. A line that
// does not parse has to be skipped without taking the rest of the file with
// it, because the operator who edits one entry and gets it wrong must still
// find every other server where they left it; a parser that stops at the
// first bad line turns a one-character mistake into a feature that has
// vanished. What is accepted has to be a server this application can actually
// be a well-behaved guest on: a host with a scheme, a space, or embedded
// credentials in it is not a hostname, and a port outside the range is not a
// port. And the source kind has to survive the round trip, because a
// reverse-beacon report is a receiver hearing a signal while a cluster spot
// is a person saying they heard one, and relabelling one as the other claims
// the stronger kind of evidence for free. A login command is stored exactly
// as written, `{BAND}` token and all: the substitution belongs to the client,
// which is the only thing that knows what band the receiver is on, and a
// loader that helpfully touched the token would leave the client nothing to
// substitute.
//
// All of this runs for real: fromText takes the text directly, so every
// acceptance and rejection rule can be exercised without a filesystem, and
// the shipped file is read from the source tree so that what is tested is
// what is installed.
//
// Required build wiring (a Qt-linked test, beside cwa_dx_spot_provider_test in
// src/desktop/CMakeLists.txt):
//
//   qt_add_executable(cwa_dx_cluster_servers_test
//     ${PROJECT_SOURCE_DIR}/tests/desktop/dx_cluster_servers_test.cpp
//     dxcluster/dx_cluster_servers.cpp
//     dxcluster/dx_cluster_servers.hpp)
//   target_link_libraries(cwa_dx_cluster_servers_test PRIVATE
//     cwa_core Qt6::Core)
//   target_include_directories(cwa_dx_cluster_servers_test PRIVATE
//     ${CMAKE_CURRENT_SOURCE_DIR})
//   target_compile_definitions(cwa_dx_cluster_servers_test PRIVATE
//     CWA_DX_CLUSTER_SERVERS_TXT_PATH="${PROJECT_SOURCE_DIR}/dictionaries/dx-cluster-servers.txt")
//   add_test(NAME cwa_dx_cluster_servers_test COMMAND cwa_dx_cluster_servers_test)
//   set_tests_properties(cwa_dx_cluster_servers_test PROPERTIES TIMEOUT 15)

#include <QLatin1String>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "dxcluster/dx_cluster_servers.hpp"

namespace {

using cwassistant::core::CwSpotSource;
using cwassistant::desktop::DxClusterServer;
using cwassistant::desktop::DxClusterServerList;

bool readFile(const char* path, std::string& contents) {
  std::ifstream source(path, std::ios::binary);
  if (!source) return false;
  contents.assign(std::istreambuf_iterator<char>{source},
                  std::istreambuf_iterator<char>{});
  return true;
}

DxClusterServerList parse(const char* text) {
  return DxClusterServerList::fromText(QString::fromUtf8(text));
}

const DxClusterServer* findHost(const DxClusterServerList& list,
                                const char* host) {
  for (const DxClusterServer& server : list.servers()) {
    if (server.host.compare(QLatin1String(host), Qt::CaseInsensitive) == 0)
      return &server;
  }
  return nullptr;
}

DxClusterServer workingServer() {
  DxClusterServer server;
  server.name = QStringLiteral("A node");
  server.host = QStringLiteral("dxc.example.net");
  server.port = 7'300;
  server.source = CwSpotSource::Cluster;
  server.login_commands = QStringList{QStringLiteral("SET/SKIMMER")};
  server.note = QStringLiteral("A note.");
  return server;
}

bool hasControlCharacters(const QString& value) {
  for (const QChar character : value) {
    if (character.unicode() < 0x20 || character.unicode() == 0x7F) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// The file that ships.
// ---------------------------------------------------------------------------

// Every entry in the installed list parses and is usable. This is the list an
// operator who has never opened the file gets, so a mistake here is not a
// degraded experience, it is a feature that does nothing on first run.
bool theShippedListParses(const std::string& shipped) {
  const DxClusterServerList list =
      DxClusterServerList::fromText(QString::fromUtf8(shipped.c_str()));
  if (list.isEmpty()) return false;
  // The six entries the list has always offered. More may be added; none of
  // these may quietly stop parsing.
  for (const char* host :
       {"telnet.reversebeacon.net", "dxc.ve7cc.net", "dxc.nc7j.com",
        "dxfun.com", "dxc.w3lpl.net", "gb7djk.dxcluster.net"}) {
    if (findHost(list, host) == nullptr) return false;
  }
  if (list.servers().size() < 6U) return false;
  for (const DxClusterServer& server : list.servers()) {
    if (!server.isValid()) return false;
    if (server.name.isEmpty() || server.host.isEmpty()) return false;
    if (server.port == 0) return false;
    if (hasControlCharacters(server.host)) return false;
    for (const QString& command : server.login_commands) {
      if (command.isEmpty() || hasControlCharacters(command)) return false;
    }
  }
  return true;
}

// The one machine source in the list is labelled as one, and the five human
// ones are not. The distinction is the whole reason two kinds exist.
bool theShippedListLabelsItsSourcesCorrectly(const std::string& shipped) {
  const DxClusterServerList list =
      DxClusterServerList::fromText(QString::fromUtf8(shipped.c_str()));
  const DxClusterServer* rbn = findHost(list, "telnet.reversebeacon.net");
  if (rbn == nullptr) return false;
  if (rbn->source != CwSpotSource::ReverseBeacon) return false;
  if (rbn->port != 7'000) return false;
  if (!rbn->login_commands.isEmpty()) return false;

  int reverse_beacon = 0;
  for (const DxClusterServer& server : list.servers()) {
    if (server.source == CwSpotSource::ReverseBeacon) ++reverse_beacon;
    // Port 7001 is the RBN's FT8 stream. Nothing in this application can use
    // an FT8 spot, and offering the port would join a feed whose traffic is
    // discarded on arrival.
    if (server.host.compare(QLatin1String("telnet.reversebeacon.net"),
                            Qt::CaseInsensitive) == 0 &&
        server.port == 7'001) {
      return false;
    }
  }
  return reverse_beacon == 1;
}

// The setup commands a server needs are part of its description, and they are
// what makes the feed useful: without SET/SKIMMER a CC Cluster node withholds
// exactly the skimmer spots a CW decoder wants, and without SET/NOFT8 it
// sends a stream of spots this application throws away.
bool theShippedListCarriesItsLoginCommands(const std::string& shipped) {
  const DxClusterServerList list =
      DxClusterServerList::fromText(QString::fromUtf8(shipped.c_str()));
  const DxClusterServer* ve7cc = findHost(list, "dxc.ve7cc.net");
  if (ve7cc == nullptr) return false;
  if (ve7cc->source != CwSpotSource::Cluster) return false;
  if (ve7cc->port != 23) return false;
  const QStringList expected{QStringLiteral("SET/SKIMMER"),
                             QStringLiteral("SET/NOFT8"),
                             QStringLiteral("SET/NOFT4")};
  return ve7cc->login_commands.mid(0, expected.size()) == expected;
}

// The DXSpider entry carries its band filter as a token, stored verbatim.
//
// `accept/spots 0 on {BAND}/cw` is the command that server accepted and
// echoed back from `sh/filter` as `filter0 accept on 20m/cw`. The loader's
// job is to hand it over untouched: it does not know what band the receiver
// is on, and a loader that expanded, trimmed, or case-folded the token would
// leave the client a command with nothing to substitute and the operator an
// unfiltered feed with no sign anything failed.
bool theShippedListKeepsTheBandTokenVerbatim(const std::string& shipped) {
  const DxClusterServerList list =
      DxClusterServerList::fromText(QString::fromUtf8(shipped.c_str()));
  const DxClusterServer* spider = findHost(list, "gb7djk.dxcluster.net");
  if (spider == nullptr) return false;
  if (spider->source != CwSpotSource::Cluster) return false;
  if (spider->port != 7'300) return false;
  const QStringList expected{QStringLiteral("accept/spots 0 on {BAND}/cw")};
  if (spider->login_commands != expected) return false;
  // The RBN accepts no filter commands on its telnet port, so an entry that
  // carried one would be sending commands into a stream that answers none.
  const DxClusterServer* rbn = findHost(list, "telnet.reversebeacon.net");
  return rbn != nullptr && rbn->login_commands.isEmpty();
}

// ---------------------------------------------------------------------------
// The parse itself.
// ---------------------------------------------------------------------------

// The two words that decide how much a spot from a server is worth. An
// unrecognised third word is not quietly taken as either.
bool readsTheSourceKind() {
  const DxClusterServerList list = parse(
      "Beacons|rbn.example.net|7000|rbn||A skimmer feed.\n"
      "People|dxc.example.net|7300|cluster||A human node.\n");
  if (list.servers().size() != 2U) return false;
  return list.servers()[0].source == CwSpotSource::ReverseBeacon &&
         list.servers()[1].source == CwSpotSource::Cluster;
}

// The token survives the parse for any entry, not just the shipped one. An
// operator adding a DXSpider node of their own gets the same treatment.
bool keepsTheBandTokenIntact() {
  const DxClusterServerList list = parse(
      "Spider|spider.example.net|7300|cluster|"
      "SET/SKIMMER;accept/spots 0 on {BAND}/cw|x\n");
  if (list.servers().size() != 1U) return false;
  const QStringList expected{QStringLiteral("SET/SKIMMER"),
                             QStringLiteral("accept/spots 0 on {BAND}/cw")};
  return list.servers()[0].login_commands == expected;
}

// Commands are one field separated by semicolons, so a server needing three
// of them stays one line an operator can read and edit.
bool splitsLoginCommandsOnSemicolons() {
  const DxClusterServerList list = parse(
      "Three|a.example.net|7300|cluster|SET/SKIMMER;SET/NOFT8;SET/NOFT4|x\n"
      "None|b.example.net|7300|cluster||x\n");
  if (list.servers().size() != 2U) return false;
  const QStringList expected{QStringLiteral("SET/SKIMMER"),
                             QStringLiteral("SET/NOFT8"),
                             QStringLiteral("SET/NOFT4")};
  return list.servers()[0].login_commands == expected &&
         list.servers()[1].login_commands.isEmpty();
}

// The file is documentation as much as data: two thirds of the shipped one is
// comment explaining what joining a server costs. Comments and blank lines
// are not entries.
bool skipsCommentsAndBlankLines() {
  const DxClusterServerList list = parse(
      "# A comment line\n"
      "\n"
      "   \n"
      "# name | host | port | source | login commands | note\n"
      "Real|dxc.example.net|7300|cluster||The only entry.\n"
      "\n"
      "# trailing comment\n");
  return list.servers().size() == 1U &&
         list.servers()[0].host == QLatin1String("dxc.example.net");
}

// The rule the whole format rests on. An operator edits one line, gets it
// wrong, and must still find every other server where they left it. A parser
// that stops at the first bad line turns a one-character mistake into a
// dropdown that has emptied itself, with nothing on screen to say why.
bool keepsEveryGoodLineAfterABadOne() {
  const DxClusterServerList list = parse(
      "First|first.example.net|7300|cluster||Before the mistake.\n"
      "Nonsense with no separators at all\n"
      "Second|second.example.net|7300|cluster||After the mistake.\n"
      "Missing fields|third.example.net\n"
      "Third|third.example.net|7300|cluster||After another mistake.\n"
      "|||||\n"
      "Fourth|fourth.example.net|7000|rbn||Last.\n");
  if (list.servers().size() != 4U) return false;
  return list.servers()[0].host == QLatin1String("first.example.net") &&
         list.servers()[1].host == QLatin1String("second.example.net") &&
         list.servers()[2].host == QLatin1String("third.example.net") &&
         list.servers()[3].host == QLatin1String("fourth.example.net") &&
         list.servers()[3].source == CwSpotSource::ReverseBeacon;
}

// A port that is not a port is not repaired into one. A text field read
// loosely into sixteen bits wraps: 65536 becomes 0 and 65559 becomes 23,
// which would have the application connecting somewhere nobody asked for.
bool refusesABadPort() {
  const DxClusterServerList list = parse(
      "Zero|a.example.net|0|cluster||x\n"
      "Negative|b.example.net|-1|cluster||x\n"
      "TooBig|c.example.net|65536|cluster||x\n"
      "WayTooBig|d.example.net|99999|cluster||x\n"
      "Wrapped|e.example.net|65559|cluster||x\n"
      "Words|f.example.net|telnet|cluster||x\n"
      "Empty|g.example.net||cluster||x\n"
      "Float|h.example.net|73.5|cluster||x\n"
      "Good|i.example.net|7300|cluster||x\n");
  return list.servers().size() == 1U &&
         list.servers()[0].host == QLatin1String("i.example.net") &&
         list.servers()[0].port == 7'300;
}

// An unrecognised source word is refused rather than defaulted. Defaulting
// would silently file a machine's reports under a person's name or the
// reverse, and the operator would never be told which.
bool refusesAnUnknownSource() {
  const DxClusterServerList list = parse(
      "Ft8|a.example.net|7001|ft8||x\n"
      "Beacon|b.example.net|7000|beacon||x\n"
      "Empty|c.example.net|7000|||x\n"
      "Nonsense|d.example.net|7000|reverse beacon network||x\n"
      "Good|e.example.net|7000|rbn||x\n");
  return list.servers().size() == 1U &&
         list.servers()[0].host == QLatin1String("e.example.net");
}

// There is nothing to connect to without a host, and a line without one is a
// half-finished edit rather than a server.
bool refusesAMissingHost() {
  const DxClusterServerList list = parse(
      "NoHost||7300|cluster||x\n"
      "SpacesOnly|   |7300|cluster||x\n"
      "TooFewFields|a.example.net|7300\n"
      "NoName||7300|cluster||x\n"
      "Good|b.example.net|7300|cluster||x\n");
  return list.servers().size() == 1U &&
         list.servers()[0].host == QLatin1String("b.example.net");
}

// A hand-edited line is padded for readability. The padding is not part of
// the host: a trailing space would be carried into a DNS lookup that fails
// for a reason the operator cannot see on screen.
bool trimsTheFieldsOfAHandEditedLine() {
  const DxClusterServerList list = parse(
      "  Padded node  |  dxc.example.net  |  7300  |  cluster  |  "
      "SET/SKIMMER ; SET/NOFT8  |  A note.  \n");
  if (list.servers().size() != 1U) return false;
  const DxClusterServer& server = list.servers()[0];
  const QStringList expected{QStringLiteral("SET/SKIMMER"),
                             QStringLiteral("SET/NOFT8")};
  return server.host == QLatin1String("dxc.example.net") &&
         server.name == QLatin1String("Padded node") && server.port == 7'300 &&
         server.login_commands == expected && server.isValid();
}

// A login command is written to somebody else's socket as one line, so a
// carriage return inside one ends that line early and sends whatever follows
// it as a command of its own -- under the operator's callsign, on a machine
// belonging to a volunteer.
//
// The line split does not catch this. The file is split on newlines, so a
// line feed cannot survive into a field, but a bare carriage return in the
// middle of a line is not a line break to this parser and is not whitespace
// at either end for trimming to remove. It travels intact from a text file an
// operator is invited to edit all the way to a stranger's socket, which is
// exactly the kind of path that looks harmless in review.
//
// Dropping the command or dropping the line are both fine. What cannot happen
// is handing one back with the carriage return still in it.
bool neverCarriesALineBreakIntoALoginCommand() {
  const DxClusterServerList list = parse(
      "Injected|a.example.net|7300|cluster|SET/SKIMMER\rBYE|x\n"
      "Leading|b.example.net|7300|cluster|\rSH/DX;SET/SKIMMER|x\n"
      "Good|c.example.net|7300|cluster|SET/SKIMMER|x\n");
  for (const DxClusterServer& server : list.servers()) {
    for (const QString& command : server.login_commands) {
      if (command.contains(u'\r') || command.contains(u'\n')) return false;
    }
  }
  // And the well-formed entry beside them still loads.
  const DxClusterServer* good = findHost(list, "c.example.net");
  return good != nullptr &&
         good->login_commands == QStringList{QStringLiteral("SET/SKIMMER")};
}

// ---------------------------------------------------------------------------
// Finding a stored selection again.
// ---------------------------------------------------------------------------

// Hostnames are case-insensitive, and a selection stored in one case must
// still be recognised as one of the offered entries in another. Recognised as
// absent, it would be presented to the operator as a custom server they never
// entered.
bool indexOfIsCaseInsensitiveOnTheHost() {
  const DxClusterServerList list = parse(
      "Beacons|Telnet.ReverseBeacon.NET|7000|rbn||x\n"
      "People|dxc.example.net|7300|cluster||x\n");
  if (list.servers().size() != 2U) return false;
  if (list.indexOf(QStringLiteral("telnet.reversebeacon.net"), 7'000) != 0)
    return false;
  if (list.indexOf(QStringLiteral("TELNET.REVERSEBEACON.NET"), 7'000) != 0)
    return false;
  if (list.indexOf(QStringLiteral("DXC.EXAMPLE.NET"), 7'300) != 1) return false;
  // A different port is a different entry, and an absent host is absent
  // rather than the first entry by accident.
  if (list.indexOf(QStringLiteral("telnet.reversebeacon.net"), 7'001) != -1)
    return false;
  if (list.indexOf(QStringLiteral("nowhere.example.net"), 7'300) != -1)
    return false;
  return list.indexOf(QString(), 7'300) == -1;
}

// ---------------------------------------------------------------------------
// What counts as a server.
// ---------------------------------------------------------------------------

// A hostname and nothing else. A scheme, a space, or a user:password@ prefix
// means the value came from somewhere it should not have -- a pasted URL, a
// half-finished edit -- and connecting to it would either fail obscurely or
// send a credential nobody meant to send.
bool isValidRejectsAnythingThatIsNotAHostname() {
  if (!workingServer().isValid()) return false;
  for (const char* host :
       {"", "   ", "telnet://dxc.example.net", "http://dxc.example.net",
        "dxc.example.net:7300", "user:password@dxc.example.net",
        "user@dxc.example.net", "dxc example.net", "dxc.example.net/path",
        "dxc.example.net\r\n", "dxc.example.net\n", "dxc.example\tnet"}) {
    DxClusterServer server = workingServer();
    server.host = QString::fromUtf8(host);
    if (server.isValid()) return false;
  }
  return true;
}

// A port of zero is not a default, it is an unset field, and a server the
// application cannot reach has no business in the list the operator picks
// from.
bool isValidRejectsAnUnusablePortOrName() {
  DxClusterServer unset = workingServer();
  unset.port = 0;
  if (unset.isValid()) return false;
  DxClusterServer blank = workingServer();
  blank.name.clear();
  if (blank.isValid()) return false;
  DxClusterServer spaces = workingServer();
  spaces.name = QStringLiteral("   ");
  if (spaces.isValid()) return false;
  // An address literal is a hostname a socket can take, and the loopback
  // interface is how the client half of this feature is tested at all.
  DxClusterServer literal = workingServer();
  literal.host = QStringLiteral("127.0.0.1");
  return literal.isValid();
}

}  // namespace

int main() {
  std::string shipped;
  if (!readFile(CWA_DX_CLUSTER_SERVERS_TXT_PATH, shipped)) return 1;

  if (!theShippedListParses(shipped)) return 2;
  if (!theShippedListLabelsItsSourcesCorrectly(shipped)) return 3;
  if (!theShippedListCarriesItsLoginCommands(shipped)) return 4;
  if (!theShippedListKeepsTheBandTokenVerbatim(shipped)) return 5;

  if (!readsTheSourceKind()) return 6;
  if (!keepsTheBandTokenIntact()) return 7;
  if (!splitsLoginCommandsOnSemicolons()) return 8;
  if (!skipsCommentsAndBlankLines()) return 9;
  if (!keepsEveryGoodLineAfterABadOne()) return 10;
  if (!refusesABadPort()) return 11;
  if (!refusesAnUnknownSource()) return 12;
  if (!refusesAMissingHost()) return 13;
  if (!trimsTheFieldsOfAHandEditedLine()) return 14;
  if (!neverCarriesALineBreakIntoALoginCommand()) return 15;

  if (!indexOfIsCaseInsensitiveOnTheHost()) return 16;

  if (!isValidRejectsAnythingThatIsNotAHostname()) return 17;
  if (!isValidRejectsAnUnusablePortOrName()) return 18;

  return 0;
}
