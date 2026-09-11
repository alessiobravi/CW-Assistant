// Source contract: a cluster line arrives from somebody else's machine, and
// everything this application does with one has to survive the shapes a real
// server actually sends.
//
// Four things are worth pinning down. A spot has to be read by its fields
// rather than by its columns: the reverse-beacon line format is a printf
// layout, not a fixed grid, and a skimmer with a long callsign -- EA2RCF-4-#
// against SE5E-# -- shifts every column after its own. A column-slicing
// parser drops exactly those lines, which does not look like a bug: the feed
// still works, the operator simply never sees a spot from that skimmer again.
// Server chatter is not a spot and not an error either, because a talkative
// server would otherwise look like a broken parser. The HHMMz stamp a cluster
// sends carries no date at all, so it is anchored to the moment the line
// arrived and can never be read as later than that, since a spot cannot have
// been heard after it arrived. And the login callsign is the one string this
// application writes to a stranger's socket, so it is validated by the
// program's single definition of callsign syntax plus a refusal of anything
// carrying CR or LF -- a newline inside it would send everything after it to
// that server as a command line of its own.
//
// All of this runs for real: parseLine and isAcceptableLoginCallsign are
// static and free of socket, clock and member state, so every acceptance and
// rejection rule can be exercised directly.
//
// One more thing the operator can lose silently: the server-side band filter.
// A login command carrying the token {BAND} follows the receiver, and the two
// ways to get that wrong are both quiet. Substituting nothing while the band
// is unknown writes `accept/spots 0 on /cw` to a stranger's server, which is a
// syntax error nobody here ever sees; re-sending the one-time setup commands
// on every band change is avoidable noise on a volunteer's machine. Command
// resolution is private, as it should be, so this half is observed where it
// is actually visible -- on the wire, against a fake cluster on the loopback
// interface.
//
// The lines below were captured live from telnet.reversebeacon.net:7000, and
// the DXSpider filter command from gb7djk.dxcluster.net, which accepted
// `accept/spots 0 on 20m/cw` and echoed it back from `sh/filter` as
// `filter0 accept on 20m/cw`.
//
// Required build wiring (a Qt-linked test, beside cwa_dx_spot_provider_test in
// src/desktop/CMakeLists.txt):
//
//   qt_add_executable(cwa_dx_cluster_client_test
//     ${PROJECT_SOURCE_DIR}/tests/desktop/dx_cluster_client_test.cpp
//     dxcluster/dx_cluster_client.cpp
//     dxcluster/dx_cluster_client.hpp
//     dxcluster/dx_cluster_servers.cpp
//     dxcluster/dx_cluster_servers.hpp)
//   target_link_libraries(cwa_dx_cluster_client_test PRIVATE
//     cwa_core Qt6::Core Qt6::Network)
//   target_include_directories(cwa_dx_cluster_client_test PRIVATE
//     ${CMAKE_CURRENT_SOURCE_DIR})
//   add_test(NAME cwa_dx_cluster_client_test COMMAND cwa_dx_cluster_client_test)
//   set_tests_properties(cwa_dx_cluster_client_test PROPERTIES TIMEOUT 60)
//
// dx_cluster_servers.cpp is not optional here: the client calls
// DxClusterServer::isValid(), which is defined there. The loopback half needs
// Qt6::Network, and the whole file runs in about half a second.

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cw_spot_registry.hpp"
#include "dxcluster/dx_cluster_client.hpp"

namespace {

using cwassistant::core::CallsignPolicy;
using cwassistant::core::CwSpotSource;
using cwassistant::core::kMaximumSpotFrequencyHz;
using cwassistant::core::kMinimumSpotFrequencyHz;
using cwassistant::desktop::DxClusterLine;
using cwassistant::desktop::DxClusterClient;
using cwassistant::desktop::DxClusterServer;

// A fixed instant to parse against, so nothing in this file depends on when it
// is run: 2025-09-11T18:10:00Z, six minutes after the 1804Z stamp the captured
// lines carry. Any stamp later than 18:10 is, from the parser's point of view,
// in the future.
constexpr std::uint64_t kReceivedNs = 1'757'614'200'000'000'000ULL;

// Four real reverse-beacon lines, exactly as they arrived.
//
// The third is the one that matters. `EA2RCF-4-#:` is four characters longer
// than `SE5E-#:`, which pushes the frequency, the callsign and everything
// after them to the right. The format is printf padding around variable-width
// fields, not a grid.
constexpr char kShortSpotter[] =
    "DX de SE5E-#:    1827.50  RW3M           CW     7 dB  21 WPM  CQ      1804Z";
constexpr char kMediumSpotter[] =
    "DX de PI4CC-#:   3539.00  SP4JFR         CW    17 dB  17 WPM  CQ      1804Z";
constexpr char kLongSpotter[] =
    "DX de EA2RCF-4-#:  7017.60  GX3NJA         CW    11 dB  22 WPM  CQ      1804Z";
constexpr char kHighBandSpotter[] =
    "DX de HB9EMP-#: 14007.90  EH8PDA         CW    24 dB  35 WPM  CQ      1804Z";

// A human spot: a person typing what they heard, with a comment where the
// skimmer puts its measurements and no dB or WPM anywhere.
constexpr char kHumanSpot[] =
    "DX de OH2BH:     14025.0  JA1XYZ       Loud in EU                     1804Z";

std::uint64_t nanosecondsFor(const char* iso_utc) {
  const QDateTime moment =
      QDateTime::fromString(QString::fromLatin1(iso_utc), Qt::ISODate);
  return static_cast<std::uint64_t>(moment.toMSecsSinceEpoch()) * 1'000'000ULL;
}

DxClusterLine parse(const QString& line,
                    CwSpotSource source = CwSpotSource::ReverseBeacon,
                    std::uint64_t received_ns = kReceivedNs) {
  return DxClusterClient::parseLine(line, received_ns, source);
}

DxClusterLine parse(const char* line,
                    CwSpotSource source = CwSpotSource::ReverseBeacon,
                    std::uint64_t received_ns = kReceivedNs) {
  return parse(QString::fromLatin1(line), source, received_ns);
}

bool nearHz(double actual, double expected) {
  const double difference = actual > expected ? actual - expected
                                              : expected - actual;
  return difference < 0.5;
}

// Anything outside printable ASCII has no business in a field this
// application stores, displays, or compares against a decoded callsign.
bool hasControlBytes(const std::string& value) {
  for (const unsigned char character : value) {
    if (character < 0x20U || character >= 0x7FU) return true;
  }
  return false;
}

bool isSpotFor(const DxClusterLine& line, const char* callsign,
               const char* spotter, double frequency_hz) {
  return line.is_spot && !line.is_login_prompt &&
         line.spot.callsign == callsign && line.spot.spotter == spotter &&
         nearHz(line.spot.frequency_hz, frequency_hz) &&
         line.spot.observed_ns <= kReceivedNs;
}

// A reverse-beacon line carrying whatever frequency is asked for, in the
// kilohertz the cluster network publishes.
QString spotLineAtKilohertz(double kilohertz) {
  return QStringLiteral("DX de SE5E-#:   ") +
         QString::number(kilohertz, 'f', 2) +
         QStringLiteral("  RW3M           CW     7 dB  21 WPM  CQ      1804Z");
}

// The parser this file exists to rule out, written the way it usually is: the
// reverse-beacon layout read as fixed columns, which is correct for every
// line whose spotter callsign is short enough. Returns false exactly where
// such a parser gives up on a line.
bool columnSlicingParse(const char* raw, QString* callsign, double* khz) {
  const QString line = QString::fromLatin1(raw);
  bool converted = false;
  const double parsed = line.mid(16, 10).trimmed().toDouble(&converted);
  if (!converted) return false;
  const QString sliced = line.mid(26, 13).trimmed();
  if (sliced.isEmpty()) return false;
  *callsign = sliced;
  *khz = parsed;
  return true;
}

// ---------------------------------------------------------------------------
// Spots.
// ---------------------------------------------------------------------------

// The plain case, field by field. Frequency is hertz on the spot even though
// the cluster publishes kilohertz, because everything downstream -- the
// registry, the tolerance match, the overlay -- is in hertz, and a spot left
// in kilohertz lands a thousand times too low without ever looking wrong.
bool parsesAPlainSkimmerSpot() {
  const DxClusterLine line = parse(kShortSpotter);
  if (!isSpotFor(line, "RW3M", "SE5E-#", 1'827'500.0)) return false;
  return !hasControlBytes(line.spot.callsign) &&
         !hasControlBytes(line.spot.spotter);
}

// The source is what the operator connected to, not something guessed from
// the text. A cluster spot relabelled as a reverse-beacon report would claim
// a receiver heard the station when a person said so, which is the stronger
// kind of evidence for free.
bool takesTheSourceFromTheCaller() {
  return parse(kShortSpotter, CwSpotSource::ReverseBeacon).spot.source ==
             CwSpotSource::ReverseBeacon &&
         parse(kShortSpotter, CwSpotSource::Cluster).spot.source ==
             CwSpotSource::Cluster;
}

// Every captured line, including the long-spotter one. If this fails on the
// third line alone, the operator has silently lost every spot from every
// skimmer whose callsign carries a suffix -- a whole class of stations, with
// no error anywhere to say so.
bool parsesEveryRealLineIncludingTheMisalignedOne() {
  return isSpotFor(parse(kShortSpotter), "RW3M", "SE5E-#", 1'827'500.0) &&
         isSpotFor(parse(kMediumSpotter), "SP4JFR", "PI4CC-#", 3'539'000.0) &&
         isSpotFor(parse(kLongSpotter), "GX3NJA", "EA2RCF-4-#",
                   7'017'600.0) &&
         isSpotFor(parse(kHighBandSpotter), "EH8PDA", "HB9EMP-#",
                   14'007'900.0);
}

// The demonstration that the test above discriminates rather than merely
// passing. A column-slicing parser reads three of the four captured lines
// perfectly and drops the fourth, which is why the defect survives casual
// testing: pick your sample lines from one skimmer and everything looks fine.
bool columnSlicingParserDropsTheMisalignedLine() {
  QString callsign;
  double khz = 0.0;
  if (!columnSlicingParse(kShortSpotter, &callsign, &khz)) return false;
  if (callsign != QLatin1String("RW3M") || !nearHz(khz, 1827.50)) return false;
  if (!columnSlicingParse(kMediumSpotter, &callsign, &khz)) return false;
  if (callsign != QLatin1String("SP4JFR")) return false;
  if (!columnSlicingParse(kHighBandSpotter, &callsign, &khz)) return false;
  if (callsign != QLatin1String("EH8PDA")) return false;
  // And on the misaligned line it reads the spotter's own colon as part of
  // the frequency and gives up entirely.
  return !columnSlicingParse(kLongSpotter, &callsign, &khz);
}

// Kilohertz in, hertz out, with the decimals intact. 14007.90 is not 14007,
// and a spot rounded to the kilohertz would fall outside the tolerance that
// matches it to a decoded signal.
bool readsFrequencyInHertz() {
  return nearHz(parse(kHighBandSpotter).spot.frequency_hz, 14'007'900.0) &&
         nearHz(parse(kLongSpotter).spot.frequency_hz, 7'017'600.0) &&
         nearHz(parse(kShortSpotter).spot.frequency_hz, 1'827'500.0);
}

// The `-#` marks a skimmer rather than a person, and the `-4` says which
// receiver of that station's array heard it. Trimming either turns several
// independent receivers into one indistinguishable source, which is the
// difference between corroboration and a repeated claim.
bool keepsTheSkimmerSuffixOnTheSpotter() {
  return parse(kShortSpotter).spot.spotter == "SE5E-#" &&
         parse(kLongSpotter).spot.spotter == "EA2RCF-4-#" &&
         parse(kHumanSpot).spot.spotter == "OH2BH";
}

// A human spot carries free text where the skimmer puts its measurements.
// Requiring dB and WPM to be present would quietly discard every spot made by
// a person, which is half the network.
bool parsesAHumanSpotWithAFreeTextComment() {
  return isSpotFor(parse(kHumanSpot, CwSpotSource::Cluster), "JA1XYZ",
                   "OH2BH", 14'025'000.0);
}

// ---------------------------------------------------------------------------
// Everything a server sends that is not a spot.
// ---------------------------------------------------------------------------

// Not a spot, and not an error. A cluster talks constantly -- banners, user
// counts, notices, its own advice about FT8 -- and treating chatter as a
// parse failure would make a healthy server indistinguishable from a broken
// parser, which is how a working feed gets switched off.
bool treatsServerChatterAsNotASpot() {
  for (const char* chatter :
       {"Local users: 518", "Please enter your call: ",
        "To see FT8 spots you MUST enter SET/FT8", "", "   ",
        "WWV de W0MU <18Z> : SFI=163, A=9, K=3, No Storms -> No Storms",
        "DX de nobody in particular"}) {
    const DxClusterLine line = parse(chatter);
    if (line.is_spot) return false;
    // Nothing partial survives a line that is not a spot.
    if (!line.spot.callsign.empty()) return false;
  }
  return true;
}

// The callsign is sent in answer to a prompt and never volunteered, so the
// prompt has to be recognised rather than assumed -- and recognised narrowly.
// A line that merely mentions logging in is not an invitation to write the
// operator's callsign into a channel where anyone can read it, and a spot
// mistaken for a prompt would have the client answering every line it saw.
bool recognisesOnlyRealLoginPrompts() {
  for (const char* prompt : {"Please enter your call: ", "login: "}) {
    const DxClusterLine line = parse(prompt);
    if (!line.is_login_prompt || line.is_spot) return false;
  }
  for (const char* ordinary :
       {"Your last login was at 1802Z from 192.0.2.10",
        "To see FT8 spots you MUST enter SET/FT8", "Local users: 518",
        kShortSpotter, kLongSpotter, ""}) {
    if (parse(ordinary).is_login_prompt) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// The clock.
// ---------------------------------------------------------------------------

// A cluster stamp is four digits and a Z. It says nothing about the date, so
// it is anchored to the day the line arrived; and a stamp that would land
// after the line arrived belongs to yesterday, because a spot cannot have
// been heard after it was reported. Read forward instead, a spot arriving
// just after midnight would be dated nearly a day into the future and would
// outlive every retention window measured against it.
bool anchorsTheClockStampToTheDayItArrived() {
  const DxClusterLine recent = parse(kShortSpotter);
  if (!recent.is_spot) return false;
  if (recent.spot.observed_ns != nanosecondsFor("2025-09-11T18:04:00Z"))
    return false;

  // 2330Z has not happened yet on the day this line arrived.
  const DxClusterLine future = parse(
      "DX de SE5E-#:    1827.50  RW3M           CW     7 dB  21 WPM  CQ      "
      "2330Z");
  if (!future.is_spot) return false;
  if (future.spot.observed_ns != nanosecondsFor("2025-09-10T23:30:00Z"))
    return false;
  if (future.spot.observed_ns > kReceivedNs) return false;

  // The boundary: a stamp at exactly the arrival minute is today, not
  // yesterday.
  const DxClusterLine boundary = parse(
      "DX de SE5E-#:    1827.50  RW3M           CW     7 dB  21 WPM  CQ      "
      "1810Z");
  return boundary.is_spot &&
         boundary.spot.observed_ns == nanosecondsFor("2025-09-11T18:10:00Z");
}

// A stamp that cannot be read dates the spot to the moment it arrived, the
// same convention the HTTPS provider follows. Dropping the spot instead would
// throw away a good callsign and a good frequency over a field this
// application only uses to age it out.
bool datesAnUnreadableStampToArrival() {
  for (const char* line :
       {"DX de SE5E-#:    1827.50  RW3M           CW     7 dB  21 WPM  CQ"
        "      9999Z",
        "DX de SE5E-#:    1827.50  RW3M           CW     7 dB  21 WPM  CQ"}) {
    const DxClusterLine parsed = parse(line);
    if (!parsed.is_spot) return false;
    if (parsed.spot.observed_ns != kReceivedNs) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Refusals.
// ---------------------------------------------------------------------------

// The callsign syntax rules live in one place for the whole program, and a
// telnet feed does not get a second, looser definition. A mangled call shown
// confidently beside a decoded one invites the operator to trust it, which is
// worse than showing nothing.
bool refusesAnImplausibleSpottedCallsign() {
  for (const char* line :
       {"DX de SE5E-#:    1827.50  NODIGITS       CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    1827.50  1234567        CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    1827.50  K1             CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    1827.50  /RW3M          CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    1827.50  RW3M/          CW     7 dB  21 WPM  CQ"
        "      1804Z"}) {
    const DxClusterLine parsed = parse(line);
    if (parsed.is_spot) return false;
  }
  // And the shape the policy does accept still arrives, so the rule above is
  // a refusal of malformed calls rather than of portable operation.
  return isSpotFor(
      parse("DX de SE5E-#:    1827.50  RW3M/P         CW     7 dB  21 WPM  CQ"
            "      1804Z"),
      "RW3M/P", "SE5E-#", 1'827'500.0);
}

// A frequency outside the radio spectrum is a unit error or a corrupted line.
// An overlay drawn at the wrong end of the spectrum is worse than an empty
// one, and a zero would sit in the registry matching nothing forever.
//
// The bounds are the program's single definition of what a spot may claim,
// not a copy of them: the HTTPS provider and this client asked the same
// question with two different answers once already, and a test that restated
// the numbers here would let them drift apart again without failing.
bool refusesAnImpossibleFrequency() {
  for (const char* line :
       {"DX de SE5E-#:    0.00  RW3M           CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    -7025.00  RW3M           CW     7 dB  21 WPM  CQ"
        "      1804Z",
        "DX de SE5E-#:    nonsense  RW3M           CW     7 dB  21 WPM  CQ"
        "      1804Z"}) {
    if (parse(line).is_spot) return false;
  }
  // A kilohertz either side of each bound, so the rule is the shared one
  // rather than an accident of where the test happened to look.
  if (parse(spotLineAtKilohertz((kMinimumSpotFrequencyHz - 1'000.0) / 1'000.0))
          .is_spot) {
    return false;
  }
  if (parse(spotLineAtKilohertz(kMaximumSpotFrequencyHz / 100.0)).is_spot)
    return false;
  if (!parse(spotLineAtKilohertz((kMinimumSpotFrequencyHz + 1'000.0) / 1'000.0))
           .is_spot) {
    return false;
  }
  return true;
}

// A line cut off by a dropped connection contributes nothing rather than half
// a spot. Half a spot is indistinguishable from a complete one once it is in
// the registry.
bool refusesATruncatedLine() {
  for (const char* line :
       {"DX de", "DX de SE5E-#:", "DX de SE5E-#:    1827.50",
        "DX de SE5E-#:    1827.50  ",
        "DX de SE5E-#:  RW3M           CW     7 dB  21 WPM"}) {
    if (parse(line).is_spot) return false;
  }
  return true;
}

// A spot says who reported it. That is what makes it corroboration rather
// than an assertion from nowhere: the operator judging whether to trust a
// callsign beside a decoded one is judging the receiver that heard it, and
// two reports from one skimmer are not two receivers agreeing. A line whose
// spotter field is empty is malformed -- no cluster software sends `DX de :`
// -- and accepting it manufactures a report with no source.
bool everySpotCarriesAProvenance() {
  for (const char* line :
       {kShortSpotter, kMediumSpotter, kLongSpotter, kHighBandSpotter,
        kHumanSpot}) {
    const DxClusterLine parsed = parse(line);
    if (!parsed.is_spot || parsed.spot.spotter.empty()) return false;
  }
  for (const char* line :
       {"DX de :    1827.50  RW3M           CW     7 dB  21 WPM  CQ  1804Z",
        "DX de    1827.50  RW3M           CW     7 dB  21 WPM  CQ      1804Z"}) {
    if (parse(line).is_spot) return false;
  }
  return true;
}

// Telnet ends its lines with CRLF, so the carriage return is on every line a
// cluster ever sends. A parser that lets it through puts an invisible control
// byte inside the last field it reads, and one that refuses the line outright
// refuses the entire feed.
bool survivesACarriageReturnTail() {
  const DxClusterLine plain = parse(kShortSpotter);
  const DxClusterLine tailed =
      parse(QString::fromLatin1(kShortSpotter) + QLatin1String("\r"));
  if (!tailed.is_spot) return false;
  if (tailed.spot.callsign != plain.spot.callsign) return false;
  if (tailed.spot.spotter != plain.spot.spotter) return false;
  if (!nearHz(tailed.spot.frequency_hz, plain.spot.frequency_hz)) return false;
  if (tailed.spot.observed_ns != plain.spot.observed_ns) return false;
  if (hasControlBytes(tailed.spot.callsign) ||
      hasControlBytes(tailed.spot.spotter)) {
    return false;
  }
  // A bare carriage return is an empty line, not a spot.
  return !parse("\r").is_spot;
}

// A telnet server negotiates options in band, with IAC (0xFF) sequences that
// can land anywhere in the stream. Whether the line is cleaned or refused is
// the implementation's choice; what cannot happen is a control byte reaching
// the registry inside a callsign, where it would compare unequal to the same
// call decoded from the air and print as garbage on the display.
bool keepsTelnetControlBytesOutOfEverySpot() {
  const char kIac[] = {'\xff', '\xfd', '\x18', '\0'};
  const QString iac = QString::fromLatin1(kIac);
  for (const QString& line :
       {iac + QString::fromLatin1(kShortSpotter),
        QString::fromLatin1(kShortSpotter) + iac,
        QString::fromLatin1("DX de SE5E-#:    1827.50  RW3M") + iac +
            QLatin1String("           CW     7 dB  21 WPM  CQ      1804Z"),
        QString::fromLatin1("DX de ") + iac +
            QLatin1String("SE5E-#:    1827.50  RW3M           CW     7 dB  "
                          "21 WPM  CQ      1804Z")}) {
    const DxClusterLine parsed = parse(line);
    if (!parsed.is_spot) continue;
    if (hasControlBytes(parsed.spot.callsign)) return false;
    if (hasControlBytes(parsed.spot.spotter)) return false;
    if (parsed.spot.callsign != "RW3M") return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// The one string this application writes to somebody else's machine.
// ---------------------------------------------------------------------------

// The callsigns an operator actually has. A licence that is refused here is a
// feature the operator cannot use at all.
bool acceptsAPlausibleLoginCallsign() {
  for (const char* callsign :
       {"IU0LFQ", "W1AW", "IU0LFQ/P", "VE7CC", "iu0lfq", " IU0LFQ ",
        "DL1ABC/QRP"}) {
    if (!DxClusterClient::isAcceptableLoginCallsign(
            QString::fromLatin1(callsign))) {
      return false;
    }
  }
  return true;
}

// What the program's callsign policy refuses, this refuses too. A typo, an
// empty field, or a pasted line of prose would be written to a stranger's
// server under the operator's name, and the server would answer with an error
// the operator never sees.
//
// `VE7CC-1` is refused deliberately: the hyphenated SSID form is not a
// callsign under CallsignPolicy::normalize, which is the program's single
// definition of callsign syntax, and a spotting or login path does not get a
// second, looser one.
bool refusesALoginCallsignThePolicyRejects() {
  for (const char* callsign :
       {"", "   ", "\t", "K1", "CQ", "ABC", "12345", "/W1AW", "W1AW/",
        "W1AW//P", "VE7CC-1", "W1AW DE IU0LFQ", "please enter your call",
        "W1AW\x01", "W1AW\x7f", "THISCALLSIGNISFARTOOLONG"}) {
    if (DxClusterClient::isAcceptableLoginCallsign(
            QString::fromLatin1(callsign))) {
      return false;
    }
  }
  return true;
}

// The security-relevant assertion in this file.
//
// This string is written to a socket as one line. An embedded CR or LF would
// end that line early and hand everything after it to a stranger's server as
// a command of its own -- a station the operator never chose to send, under
// the operator's callsign, on a machine belonging to somebody else.
//
// The program's callsign policy is not enough on its own here, and the second
// half of this test says why: `normalize` trims trailing whitespace, which
// includes CR and LF, so it accepts "W1AW\r\n" and hands back a clean "W1AW".
// A client that validated with `normalize` alone and then sent the operator's
// original string would pass every other test in this file and still put a
// newline on the wire.
bool refusesALoginCallsignCarryingCrOrLf() {
  for (const char* callsign :
       {"W1AW\r", "W1AW\n", "W1AW\r\n", "\nW1AW", "\r\nW1AW",
        "W1AW\r\nSET/SKIMMER", "W1AW\nSH/DX", "W1AW\nBYE"}) {
    if (DxClusterClient::isAcceptableLoginCallsign(
            QString::fromLatin1(callsign))) {
      return false;
    }
  }
  // The gap this test exists to close: the shared policy, by itself, says yes
  // to a callsign with a newline stuck on the end.
  return CallsignPolicy::normalize("W1AW\r\n").has_value();
}

// Everything else this accepts or refuses is the program's one callsign
// definition, not a private rule invented here. Two definitions would drift,
// and the looser one would always be the one facing the network.
bool agreesWithTheOneCallsignPolicy() {
  for (const char* callsign :
       {"IU0LFQ", "W1AW", "IU0LFQ/P", "iu0lfq", "VE7CC", "VE7CC-1", "K1",
        "ABC", "12345", "/W1AW", "W1AW/", "", "   ", "DL1ABC/QRP",
        "W1AW DE IU0LFQ", "A1A", "4X4AAA", "THISCALLSIGNISFARTOOLONG"}) {
    const bool accepted = DxClusterClient::isAcceptableLoginCallsign(
        QString::fromLatin1(callsign));
    const bool policy = CallsignPolicy::normalize(callsign).has_value();
    if (accepted != policy) return false;
  }
  return true;
}


// ---------------------------------------------------------------------------
// The band filter, observed on the wire.
// ---------------------------------------------------------------------------

// A cluster on the loopback interface that remembers everything said to it.
//
// The resolution of {BAND} is private, and it should stay private: it is an
// implementation detail of how this client talks to a server. What is not an
// implementation detail is what ends up on the socket, so that is what is
// checked here. The fake node answers the way DXSpider does -- a banner, a
// login prompt, a welcome once the callsign arrives -- and records every line
// the client writes.
class FakeCluster {
 public:
  bool start() {
    QObject::connect(&listener_, &QTcpServer::newConnection, [this] {
      QTcpSocket* connection = listener_.nextPendingConnection();
      if (connection == nullptr) return;
      if (peer_ != nullptr) {
        connection->close();
        connection->deleteLater();
        return;
      }
      peer_ = connection;
      QObject::connect(peer_, &QTcpSocket::readyRead, [this] { drain(); });
      peer_->write("Hello, this is the GB7TLH DXSpider node\r\n");
      peer_->write("login: ");
      peer_->flush();
    });
    return listener_.listen(QHostAddress::LocalHost, 0);
  }

  [[nodiscard]] quint16 port() const { return listener_.serverPort(); }
  [[nodiscard]] bool connected() const { return peer_ != nullptr; }
  [[nodiscard]] const QStringList& received() const { return received_; }

  ~FakeCluster() {
    if (peer_ != nullptr) peer_->close();
    listener_.close();
  }

 private:
  void drain() {
    buffer_.append(peer_->readAll());
    for (int newline = buffer_.indexOf('\n'); newline >= 0;
         newline = buffer_.indexOf('\n')) {
      QByteArray line = buffer_.left(newline);
      buffer_.remove(0, newline + 1);
      while (line.endsWith('\r')) line.chop(1);
      received_.append(QString::fromLatin1(line));
      if (!welcomed_) {
        // The first line a client sends is its callsign, in answer to the
        // prompt. Everything after it is the configured setup.
        welcomed_ = true;
        peer_->write("Hello and welcome, this is GB7TLH\r\n");
        peer_->flush();
      }
    }
  }

  QTcpServer listener_;
  QTcpSocket* peer_{nullptr};
  QStringList received_;
  QByteArray buffer_;
  bool welcomed_{false};
};

template <typename Predicate>
void pumpUntil(Predicate satisfied, int milliseconds) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < milliseconds) {
    QCoreApplication::processEvents(
        QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 25);
    if (satisfied()) return;
  }
}

DxClusterServer loopbackServer(quint16 port, const QStringList& commands) {
  DxClusterServer server;
  server.name = QStringLiteral("Loopback node");
  server.host = QStringLiteral("127.0.0.1");
  server.port = port;
  server.source = CwSpotSource::Cluster;
  server.login_commands = commands;
  server.note = QStringLiteral("A fake cluster for this test.");
  return server;
}

// The verified DXSpider case. `accept/spots 0 on {BAND}/cw` on 20 m is
// `accept/spots 0 on 20m/cw`, which is the exact string that server accepted.
// An unsubstituted token would be rejected as an unknown band and the
// operator would get the whole planet's spots with no sign anything failed.
bool resolvesTheBandTokenOnTheWire() {
  FakeCluster cluster;
  if (!cluster.start()) return false;
  DxClusterClient client;
  client.setServer(loopbackServer(
      cluster.port(), QStringList{QStringLiteral("SET/SKIMMER"),
                                  QStringLiteral("accept/spots 0 on {BAND}/cw")}));
  client.setLoginCallsign(QStringLiteral("IU0LFQ"));
  client.setBandFilter(QStringLiteral("20m"));
  client.setEnabled(true);
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(
            QLatin1String("accept/spots 0 on 20m/cw"));
      },
      4'000);
  client.setEnabled(false);

  if (!cluster.connected()) return false;
  if (!cluster.received().contains(QLatin1String("IU0LFQ"))) return false;
  if (!cluster.received().contains(QLatin1String("SET/SKIMMER"))) return false;
  if (!cluster.received().contains(QLatin1String("accept/spots 0 on 20m/cw")))
    return false;
  return cluster.received().filter(QStringLiteral("{BAND}")).isEmpty();
}

// AR-Cluster names a band by its bare number and refuses the ADIF suffix.
//
// Watched live on NC7J: `set/dx filter band=20m and mode=cw` came back as
// "ERROR - The requested DX filter did not pass validation and was rejected",
// while `set/dx filter band=20 and mode=cw` was accepted and reported back as
// `Filter: band = 20 and mode = cw`. DXSpider wants the opposite -- `20m` --
// so the two tokens are not interchangeable and a single one would leave half
// the shipped servers unfiltered while looking configured.
//
// The failure this guards against is invisible in use: the command is sent,
// the server answers with an error the operator never sees, and the feed
// arrives unfiltered while Settings shows a band filter in force.
bool resolvesTheBandNumberTokenForArCluster() {
  FakeCluster cluster;
  if (!cluster.start()) return false;
  DxClusterClient client;
  client.setServer(loopbackServer(
      cluster.port(),
      QStringList{QStringLiteral("set/dx filter band={BANDNUM} and mode=cw")}));
  client.setLoginCallsign(QStringLiteral("IU0LFQ"));
  client.setBandFilter(QStringLiteral("20m"));
  client.setEnabled(true);
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(
            QLatin1String("set/dx filter band=20 and mode=cw"));
      },
      4'000);
  client.setEnabled(false);

  if (!cluster.received().contains(
          QLatin1String("set/dx filter band=20 and mode=cw"))) {
    return false;
  }
  // The suffix is the whole difference, so its absence is the assertion.
  if (!cluster.received().filter(QStringLiteral("band=20m")).isEmpty()) {
    return false;
  }
  return cluster.received().filter(QStringLiteral("{BANDNUM}")).isEmpty();
}

// The quiet one. While the band is unknown there is nothing to put in the
// hole, and a command with a hole in it -- `accept/spots 0 on /cw` -- is a
// syntax error written to somebody else's machine under the operator's
// callsign. Not sending it is the only correct answer: the unfiltered feed is
// merely noisy, while a rejected filter command leaves the operator believing
// a filter is in place that is not.
bool skipsATokenCommandWhileTheBandIsUnknown() {
  FakeCluster cluster;
  if (!cluster.start()) return false;
  DxClusterClient client;
  client.setServer(loopbackServer(
      cluster.port(), QStringList{QStringLiteral("SET/SKIMMER"),
                                  QStringLiteral("accept/spots 0 on {BAND}/cw")}));
  client.setLoginCallsign(QStringLiteral("IU0LFQ"));
  // No band: the receiver has not said where it is.
  client.setEnabled(true);
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(QLatin1String("SET/SKIMMER"));
      },
      4'000);
  // Give anything else the client intends to send time to arrive.
  pumpUntil([] { return false; }, 500);
  client.setEnabled(false);

  if (!cluster.connected()) return false;
  // The setup command still goes: it does not depend on the band.
  if (!cluster.received().contains(QLatin1String("SET/SKIMMER"))) return false;
  // Nothing derived from the token does, in any form.
  if (!cluster.received().filter(QStringLiteral("{BAND}")).isEmpty())
    return false;
  return cluster.received().filter(QStringLiteral("accept/spots")).isEmpty();
}

// A band change re-sends the filter and nothing else. Re-sending SET/SKIMMER
// every time the operator turns the VFO is avoidable traffic on volunteer
// infrastructure, and on some cluster software a repeated setup command
// answers with an error line for every repeat.
bool resendsOnlyTheBandDependentCommandOnABandChange() {
  FakeCluster cluster;
  if (!cluster.start()) return false;
  DxClusterClient client;
  client.setServer(loopbackServer(
      cluster.port(), QStringList{QStringLiteral("SET/SKIMMER"),
                                  QStringLiteral("accept/spots 0 on {BAND}/cw")}));
  client.setLoginCallsign(QStringLiteral("IU0LFQ"));
  client.setBandFilter(QStringLiteral("20m"));
  client.setEnabled(true);
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(
            QLatin1String("accept/spots 0 on 20m/cw"));
      },
      4'000);

  client.setBandFilter(QStringLiteral("40m"));
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(
            QLatin1String("accept/spots 0 on 40m/cw"));
      },
      2'000);

  client.setBandFilter(QStringLiteral("15m"));
  pumpUntil(
      [&cluster] {
        return cluster.received().contains(
            QLatin1String("accept/spots 0 on 15m/cw"));
      },
      2'000);
  client.setEnabled(false);

  if (!cluster.connected()) return false;
  if (!cluster.received().contains(QLatin1String("accept/spots 0 on 20m/cw")))
    return false;
  if (!cluster.received().contains(QLatin1String("accept/spots 0 on 40m/cw")))
    return false;
  if (!cluster.received().contains(QLatin1String("accept/spots 0 on 15m/cw")))
    return false;
  // The setup command was sent once, at login, and not once per band.
  if (cluster.received().count(QLatin1String("SET/SKIMMER")) != 1) return false;
  return cluster.received().count(QLatin1String("IU0LFQ")) == 1;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // The anchor instant this whole file reasons about, checked rather than
  // trusted: an arithmetic slip here would make every timestamp assertion
  // below agree with the wrong day.
  if (kReceivedNs != nanosecondsFor("2025-09-11T18:10:00Z")) return 1;

  if (!parsesAPlainSkimmerSpot()) return 2;
  if (!takesTheSourceFromTheCaller()) return 3;
  if (!parsesEveryRealLineIncludingTheMisalignedOne()) return 4;
  if (!columnSlicingParserDropsTheMisalignedLine()) return 5;
  if (!readsFrequencyInHertz()) return 6;
  if (!keepsTheSkimmerSuffixOnTheSpotter()) return 7;
  if (!parsesAHumanSpotWithAFreeTextComment()) return 8;

  if (!treatsServerChatterAsNotASpot()) return 9;
  if (!recognisesOnlyRealLoginPrompts()) return 10;

  if (!anchorsTheClockStampToTheDayItArrived()) return 11;
  if (!datesAnUnreadableStampToArrival()) return 12;

  if (!refusesAnImplausibleSpottedCallsign()) return 13;
  if (!refusesAnImpossibleFrequency()) return 14;
  if (!refusesATruncatedLine()) return 15;
  if (!everySpotCarriesAProvenance()) return 16;
  if (!survivesACarriageReturnTail()) return 17;
  if (!keepsTelnetControlBytesOutOfEverySpot()) return 18;

  if (!acceptsAPlausibleLoginCallsign()) return 19;
  if (!refusesALoginCallsignThePolicyRejects()) return 20;
  if (!refusesALoginCallsignCarryingCrOrLf()) return 21;
  if (!agreesWithTheOneCallsignPolicy()) return 22;

  if (!resolvesTheBandTokenOnTheWire()) return 23;
  if (!resolvesTheBandNumberTokenForArCluster()) return 26;
  if (!skipsATokenCommandWhileTheBandIsUnknown()) return 24;
  if (!resendsOnlyTheBandDependentCommandOnABandChange()) return 25;

  return 0;
}
