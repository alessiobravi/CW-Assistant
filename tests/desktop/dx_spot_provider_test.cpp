// Source contract: a DX spot is corroboration and never authority, and the
// path that carries one into this application has to be safe before it is
// useful.
//
// Four things are worth pinning down. The operator's preference has to persist
// per profile with the documented defaults and stay inside its documented
// bounds, because a refresh interval or a match tolerance read back out of
// range would be applied to somebody else's server or to the operator's own
// display. A feed body is accepted or refused whole: a truncated, malformed,
// or oversized response must contribute nothing at all rather than a partial
// list, because a spot list that is half a list looks exactly like a short
// band. A record that does not carry both a callsign and a frequency is
// dropped rather than repaired, since a repaired spot corroborates nothing.
// And the whole path stays receive-only: one GET, no credentials, and no token
// of the transmit or keying vocabulary anywhere near it.
//
// The parser half runs for real -- parseSpotDocument is deliberately free of
// network, clock and member state so that every acceptance and rejection rule
// can be exercised directly. The settings and controller halves are text-level
// contracts, because that wiring lives in Qt objects and a QML document the
// dependency-free suite cannot instantiate.
//
// Required build wiring (a Qt-linked test, beside cwa_callsign_database_updater_test
// in src/desktop/CMakeLists.txt):
//
//   qt_add_executable(cwa_dx_spot_provider_test
//     ${PROJECT_SOURCE_DIR}/tests/desktop/dx_spot_provider_test.cpp
//     dxcluster/dx_spot_provider.cpp
//     dxcluster/dx_spot_provider.hpp)
//   target_link_libraries(cwa_dx_spot_provider_test PRIVATE
//     cwa_core Qt6::Core Qt6::Network)
//   target_include_directories(cwa_dx_spot_provider_test PRIVATE
//     ${CMAKE_CURRENT_SOURCE_DIR})
//   target_compile_definitions(cwa_dx_spot_provider_test PRIVATE
//     CWA_APP_SETTINGS_HPP_PATH="..." CWA_APP_SETTINGS_CPP_PATH="..."
//     CWA_REPLAY_CONTROLLER_HPP_PATH="..." CWA_REPLAY_CONTROLLER_CPP_PATH="..."
//     CWA_DX_SPOT_PROVIDER_CPP_PATH="...")

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "dxcluster/dx_spot_provider.hpp"

namespace {

using cwassistant::core::CwSpotSource;
using cwassistant::desktop::DxSpotBatch;
using cwassistant::desktop::DxSpotProvider;

// A fixed instant to parse against, so nothing in this file depends on when it
// is run. Any spot timestamped after it is, from the parser's point of view,
// in the future.
constexpr std::uint64_t kReceivedNs = 1'757'000'000'000'000'000ULL;

bool contains(const std::string& value, const std::string& expected) {
  return value.find(expected) != std::string::npos;
}

void normalizeLineEndings(std::string& value) {
  value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
}

bool readSource(const char* path, std::string& contents) {
  std::ifstream source(path, std::ios::binary);
  if (!source) return false;
  contents.assign(std::istreambuf_iterator<char>{source},
                  std::istreambuf_iterator<char>{});
  normalizeLineEndings(contents);
  return true;
}

DxSpotBatch parse(const char* json) {
  return DxSpotProvider::parseSpotDocument(QByteArray(json), kReceivedNs);
}

std::uint64_t nanosecondsFor(const char* iso_utc) {
  const QDateTime moment =
      QDateTime::fromString(QString::fromLatin1(iso_utc), Qt::ISODate);
  return static_cast<std::uint64_t>(moment.toMSecsSinceEpoch()) * 1'000'000ULL;
}

// ---------------------------------------------------------------------------
// The settings half.
// ---------------------------------------------------------------------------

// The eight preferences carry the documented defaults on the member itself, so
// a profile that has never been written still starts from a station that
// contacts nobody.
bool declaresTheDocumentedDefaults(const std::string& header) {
  return contains(header, "bool dx_spots_enabled_{false};") &&
         contains(header, "bool dx_spots_reverse_beacon_{false};") &&
         contains(header, "bool dx_spots_cluster_{false};") &&
         contains(header, "QString dx_spots_endpoint_;") &&
         contains(header, "int dx_spots_refresh_seconds_{120};") &&
         contains(header, "int dx_spots_retention_minutes_{15};") &&
         contains(header, "int dx_spots_tolerance_hz_{250};") &&
         contains(header, "bool dx_spots_show_labels_{true};");
}

// QML binds these names. A renamed property silently unbinds a control, which
// looks exactly like a control that does nothing.
bool exposesTheBoundPropertyNames(const std::string& header) {
  return contains(header, "Q_PROPERTY(bool dxSpotsEnabled READ dxSpotsEnabled") &&
         contains(header, "Q_PROPERTY(bool dxSpotsReverseBeacon READ dxSpotsReverseBeacon") &&
         contains(header, "Q_PROPERTY(bool dxSpotsCluster READ dxSpotsCluster") &&
         contains(header, "Q_PROPERTY(QString dxSpotsEndpoint READ dxSpotsEndpoint") &&
         contains(header, "Q_PROPERTY(int dxSpotsRefreshSeconds READ dxSpotsRefreshSeconds") &&
         contains(header, "Q_PROPERTY(int dxSpotsRetentionMinutes READ dxSpotsRetentionMinutes") &&
         contains(header, "Q_PROPERTY(int dxSpotsToleranceHz READ dxSpotsToleranceHz") &&
         contains(header, "Q_PROPERTY(bool dxSpotsShowLabels READ dxSpotsShowLabels");
}

// Each bounded number is clamped where it is set, where it is committed, and
// where it is restored. A settings file can be edited by hand, so the restore
// path is not a formality.
bool boundsEveryNumberOnEveryPath(const std::string& implementation) {
  const bool setters =
      contains(implementation, "std::clamp(value, 30, 600)") &&
      contains(implementation, "std::clamp(value, 1, 60)") &&
      contains(implementation, "std::clamp(value, 50, 1'000)");
  const bool applied =
      contains(implementation,
               "std::clamp(dx_spots_refresh_seconds_, 30, 600)") &&
      contains(implementation,
               "std::clamp(dx_spots_retention_minutes_, 1, 60)") &&
      contains(implementation,
               "std::clamp(dx_spots_tolerance_hz_, 50, 1'000)");
  const bool restored =
      contains(implementation,
               "QStringLiteral(\"dxSpots/refreshSeconds\")), 120)") &&
      contains(implementation,
               "QStringLiteral(\"dxSpots/retentionMinutes\")), 15)") &&
      contains(implementation,
               "QStringLiteral(\"dxSpots/toleranceHz\")), 250)");
  return setters && applied && restored;
}

// Stored under the profile's own keys and restored with the documented
// defaults, or the preference reverts on the next launch.
bool persistsEveryValuePerProfile(const std::string& implementation) {
  for (const std::string& key :
       {std::string{"dxSpots/enabled"}, std::string{"dxSpots/reverseBeacon"},
        std::string{"dxSpots/cluster"}, std::string{"dxSpots/endpoint"},
        std::string{"dxSpots/refreshSeconds"},
        std::string{"dxSpots/retentionMinutes"},
        std::string{"dxSpots/toleranceHz"},
        std::string{"dxSpots/showLabels"}}) {
    if (!contains(implementation,
                  "storageKey(QStringLiteral(\"" + key + "\"))")) {
      return false;
    }
  }
  return contains(implementation,
                  "QStringLiteral(\"dxSpots/enabled\")), false)") &&
         contains(implementation,
                  "QStringLiteral(\"dxSpots/reverseBeacon\")), false)") &&
         contains(implementation,
                  "QStringLiteral(\"dxSpots/cluster\")), false)") &&
         contains(implementation,
                  "QStringLiteral(\"dxSpots/showLabels\")), true)");
}

// A reset returns the operator to a station that contacts nobody, rather than
// to one that keeps polling an address they have just cleared.
bool resetsToTheDocumentedDefaults(const std::string& implementation) {
  return contains(implementation, "dx_spots_enabled_ = false;") &&
         contains(implementation, "dx_spots_reverse_beacon_ = false;") &&
         contains(implementation, "dx_spots_cluster_ = false;") &&
         contains(implementation, "dx_spots_endpoint_.clear();") &&
         contains(implementation, "dx_spots_refresh_seconds_ = 120;") &&
         contains(implementation, "dx_spots_retention_minutes_ = 15;") &&
         contains(implementation, "dx_spots_tolerance_hz_ = 250;") &&
         contains(implementation, "dx_spots_show_labels_ = true;");
}

// ---------------------------------------------------------------------------
// The controller half.
// ---------------------------------------------------------------------------

// One read-only list, notified on change, carrying exactly the keys the
// display reads. A missing key reads in QML as an undefined value rather than
// an error, so the delegate would simply draw nothing.
bool publishesTheSpotModel(const std::string& header,
                           const std::string& implementation) {
  const bool declared =
      contains(header,
               "Q_PROPERTY(QVariantList dxSpots READ dxSpots "
               "NOTIFY dxSpotsChanged)") &&
      contains(header, "void dxSpotsChanged();") &&
      contains(header, "[[nodiscard]] const QVariantList& dxSpots()");
  if (!declared) return false;
  for (const std::string& key :
       {std::string{"callsign"}, std::string{"frequencyHz"},
        std::string{"displayFrequencyHz"}, std::string{"reverseBeacon"},
        std::string{"cluster"}, std::string{"ageSeconds"},
        std::string{"observations"}}) {
    if (!contains(implementation,
                  "entry.insert(QStringLiteral(\"" + key + "\")")) {
      return false;
    }
  }
  // The published frequency goes through the controller's own mapping, so a
  // spot and a decoded signal are placed on one axis rather than two.
  return contains(implementation, "rfFrequencyToDisplayHz(rf_hz)") &&
         contains(implementation, "emit dxSpotsChanged();");
}

// Everything the registry holds is expired against the same clock the parser
// dates observations with. Two clocks would make a spot's age the difference
// between two unrelated origins.
bool expiresAgainstOneClock(const std::string& implementation) {
  return contains(implementation, "dx_spot_registry_.expire(now_ns)") &&
         contains(implementation,
                  "const std::uint64_t now_ns = currentUnixTimeNs();");
}

std::string dxSpotSection(const std::string& implementation) {
  const auto begin =
      implementation.find("void ReplayController::ensureDxSpotProvider()");
  const auto end =
      implementation.find("void ReplayController::setMonitorMode(", begin);
  if (begin == std::string::npos || end == std::string::npos) return {};
  return implementation.substr(begin, end - begin);
}

// ---------------------------------------------------------------------------
// Receive-only.
// ---------------------------------------------------------------------------

// The provider issues one verb and no other. Anything that can carry a body
// upstream, and anything from the transmit or keying vocabulary, is absent by
// construction rather than by policy.
bool staysReceiveOnly(const std::string& provider, const std::string& section) {
  if (!contains(provider, "network_->get(")) return false;
  for (const std::string& forbidden :
       {std::string{"->post("}, std::string{"->put("},
        std::string{"->deleteResource("}, std::string{"->sendCustomRequest("},
        std::string{"Ptt"}, std::string{"ptt"}, std::string{"transmit"},
        std::string{"Transmit"}, std::string{"keyDown"}}) {
    if (contains(provider, forbidden) || contains(section, forbidden))
      return false;
  }
  return true;
}

// No credential ever leaves this station for a public read-only feed, and an
// address that embeds one is refused before a request is built.
bool carriesNoCredentials(const std::string& provider) {
  return contains(provider, "url.userInfo().isEmpty()") &&
         contains(provider, "QNetworkRequest::CookieLoadControlAttribute") &&
         contains(provider, "QNetworkRequest::AuthenticationReuseAttribute") &&
         !contains(provider, "setRawHeader(\"Authorization\"");
}

// ---------------------------------------------------------------------------
// The parser, run for real.
// ---------------------------------------------------------------------------

// A body that is not a list of spot objects contributes nothing. Each of these
// is refused whole, with no spot surviving from it.
bool refusesAMalformedBody() {
  const char* bodies[] = {
      "",
      "not json at all",
      "{\"unexpected\": 1}",
      "[",
      "\"a string document\"",
      "{\"spots\": {\"callsign\": \"DL1ABC\"}}",
  };
  for (const char* body : bodies) {
    const DxSpotBatch batch = parse(body);
    if (batch.accepted) return false;
    if (!batch.spots.empty()) return false;
    if (batch.rejection_reason.isEmpty()) return false;
  }
  return true;
}

// A body larger than the safety limit is refused before it is parsed, so a
// feed cannot spend this application's memory by answering at length.
bool refusesAnOversizedBody() {
  QByteArray oversized("[");
  oversized.append(QByteArray(
      static_cast<qsizetype>(DxSpotProvider::kMaximumBodyBytes), ' '));
  oversized.append("]");
  const DxSpotBatch batch =
      DxSpotProvider::parseSpotDocument(oversized, kReceivedNs);
  return !batch.accepted && batch.spots.empty();
}

// More spots than the store can hold is refused whole rather than truncated.
// Truncation would be a partial application of a body that was never valid.
bool refusesAnOverlongList() {
  QByteArray body("[");
  for (int index = 0; index <= DxSpotProvider::kMaximumRecords; ++index) {
    if (index > 0) body.append(',');
    body.append("{\"callsign\":\"DL1ABC\",\"frequency\":14025.3}");
  }
  body.append("]");
  const DxSpotBatch batch =
      DxSpotProvider::parseSpotDocument(body, kReceivedNs);
  return !batch.accepted && batch.spots.empty();
}

// A record missing either required field is dropped and counted. The body
// around it is still good, so the spots that are complete still arrive.
bool dropsARecordWithoutCallsignOrFrequency() {
  const DxSpotBatch batch = parse(
      "[{\"frequency\": 14025.3},"
      " {\"callsign\": \"DL1ABC\"},"
      " {\"callsign\": \"\", \"frequency\": 14025.3},"
      " {\"callsign\": \"DL1ABC\", \"frequency\": \"not a number\"},"
      " \"a bare string\","
      " {\"callsign\": \"G4XYZ\", \"frequency\": 7025.0}]");
  return batch.accepted && batch.spots.size() == 1U &&
         batch.accepted_records == 1 && batch.rejected_records == 5 &&
         batch.spots.front().callsign == "G4XYZ";
}

// A callsign that does not look like one is dropped rather than repaired.
// Presenting a mangled call beside a decoded one invites the operator to trust
// it, and a wrong callsign shown confidently is worse than none.
bool dropsAnImplausibleCallsign() {
  const DxSpotBatch batch = parse(
      "[{\"callsign\": \"NODIGITS\", \"frequency\": 14025.3},"
      " {\"callsign\": \"12345\", \"frequency\": 14025.3},"
      " {\"callsign\": \"K1\", \"frequency\": 14025.3},"
      " {\"callsign\": \"DL1ABC<script>\", \"frequency\": 14025.3},"
      " {\"callsign\": \"/DL1ABC\", \"frequency\": 14025.3},"
      " {\"callsign\": \"DL1ABC/P\", \"frequency\": 14025.3}]");
  return batch.accepted && batch.spots.size() == 1U &&
         batch.rejected_records == 5 &&
         batch.spots.front().callsign == "DL1ABC/P";
}

// A frequency outside the radio spectrum is a unit error or a corrupted
// record, and an overlay drawn at the wrong end of the band is worse than an
// empty one.
bool dropsAnImpossibleFrequency() {
  const DxSpotBatch batch = parse(
      "[{\"callsign\": \"DL1ABC\", \"frequencyHz\": 0},"
      " {\"callsign\": \"DL1ABC\", \"frequencyHz\": -14025300},"
      " {\"callsign\": \"DL1ABC\", \"frequencyHz\": 400},"
      " {\"callsign\": \"DL1ABC\", \"frequencyHz\": 4e14},"
      " {\"callsign\": \"DL1ABC\", \"frequencyHz\": 14025300}]");
  return batch.accepted && batch.spots.size() == 1U &&
         batch.rejected_records == 4;
}

// Hertz where the member says hertz, kilohertz everywhere else. The bare
// "frequency" member is the cluster convention and has always been kilohertz.
bool readsBothFrequencyUnits() {
  const DxSpotBatch batch = parse(
      "[{\"callsign\": \"DL1ABC\", \"frequencyHz\": 14025300},"
      " {\"callsign\": \"G4XYZ\", \"frequency\": 14025.3},"
      " {\"callsign\": \"W1AW\", \"frequencyKhz\": 14025.3},"
      " {\"callsign\": \"JA1ZZZ\", \"frequency\": \"14025.3\"}]");
  if (!batch.accepted || batch.spots.size() != 4U) return false;
  for (const auto& spot : batch.spots) {
    if (spot.frequency_hz < 14'025'299.0 || spot.frequency_hz > 14'025'301.0)
      return false;
  }
  return true;
}

// A reverse-beacon report is a receiver hearing a signal and a cluster spot is
// a person saying they heard one. An unnamed source is taken as the second:
// claiming machine provenance on no grounds would present the stronger kind of
// evidence for free.
bool separatesTheTwoKindsOfReport() {
  const DxSpotBatch batch = parse(
      "[{\"callsign\": \"DL1ABC\", \"frequency\": 14025.3, \"source\": \"rbn\"},"
      " {\"callsign\": \"G4XYZ\", \"frequency\": 14025.3,"
      "  \"source\": \"Reverse Beacon Network\"},"
      " {\"callsign\": \"W1AW\", \"frequency\": 14025.3, \"source\": \"cluster\"},"
      " {\"callsign\": \"JA1ZZZ\", \"frequency\": 14025.3},"
      " {\"callsign\": \"VK2AAA\", \"frequency\": 14025.3,"
      "  \"source\": \"something new\"}]");
  if (!batch.accepted || batch.spots.size() != 5U) return false;
  return batch.spots[0].source == CwSpotSource::ReverseBeacon &&
         batch.spots[1].source == CwSpotSource::ReverseBeacon &&
         batch.spots[2].source == CwSpotSource::Cluster &&
         batch.spots[3].source == CwSpotSource::Cluster &&
         batch.spots[4].source == CwSpotSource::Cluster;
}

// A missing or unreadable timestamp dates the spot to the moment the batch
// arrived, and a spotter whose clock runs fast cannot publish a spot that
// outlives the retention window it is measured against.
bool datesEverySpotSafely() {
  const DxSpotBatch batch = parse(
      "[{\"callsign\": \"DL1ABC\", \"frequency\": 14025.3,"
      "  \"time\": \"2025-01-02T03:04:05Z\"},"
      " {\"callsign\": \"G4XYZ\", \"frequency\": 14025.3,"
      "  \"time\": \"2099-01-02T03:04:05Z\"},"
      " {\"callsign\": \"W1AW\", \"frequency\": 14025.3,"
      "  \"time\": \"yesterday afternoon\"},"
      " {\"callsign\": \"JA1ZZZ\", \"frequency\": 14025.3}]");
  if (!batch.accepted || batch.spots.size() != 4U) return false;
  return batch.spots[0].observed_ns == nanosecondsFor("2025-01-02T03:04:05Z") &&
         batch.spots[1].observed_ns == kReceivedNs &&
         batch.spots[2].observed_ns == kReceivedNs &&
         batch.spots[3].observed_ns == kReceivedNs;
}

// A richer feed still parses. Members this application does not understand are
// ignored rather than treated as a reason to refuse the record, and the
// wrapper-object shape is as common as the bare array.
bool toleratesUnknownFieldsAndWrappers() {
  const DxSpotBatch batch = parse(
      "{\"generatedAt\": \"2025-01-02T03:04:05Z\", \"page\": 1,"
      " \"spots\": [{\"callsign\": \" dl1abc \", \"frequency\": 14025.3,"
      "              \"spotter\": \"OH6BG-1\", \"snrDb\": 21,"
      "              \"continent\": \"EU\", \"mode\": \"CW\"}]}");
  return batch.accepted && batch.spots.size() == 1U &&
         batch.spots.front().callsign == "DL1ABC" &&
         batch.spots.front().spotter == "OH6BG-1";
}

// An endpoint has to be HTTPS and free of embedded credentials before a
// request is ever built from it.
bool refusesAnUnsafeEndpoint() {
  const bool accepted = DxSpotProvider::isAcceptableEndpoint(
      QUrl(QStringLiteral("https://spots.example.org/api/v1/spots"),
           QUrl::StrictMode));
  if (!accepted) return false;
  for (const QString& rejected :
       {QStringLiteral("http://spots.example.org/spots"),
        QStringLiteral("https://embedded:credentials@spots.example.org/spots"),
        QStringLiteral("ftp://spots.example.org/spots"),
        QStringLiteral("https:///spots"), QString{}}) {
    if (DxSpotProvider::isAcceptableEndpoint(QUrl(rejected, QUrl::StrictMode)))
      return false;
  }
  return true;
}

}  // namespace

int main() {
  std::string settings_header;
  if (!readSource(CWA_APP_SETTINGS_HPP_PATH, settings_header)) return 1;
  std::string settings_source;
  if (!readSource(CWA_APP_SETTINGS_CPP_PATH, settings_source)) return 2;
  std::string controller_header;
  if (!readSource(CWA_REPLAY_CONTROLLER_HPP_PATH, controller_header)) return 3;
  std::string controller_source;
  if (!readSource(CWA_REPLAY_CONTROLLER_CPP_PATH, controller_source)) return 4;
  std::string provider_source;
  if (!readSource(CWA_DX_SPOT_PROVIDER_CPP_PATH, provider_source)) return 5;

  const std::string section = dxSpotSection(controller_source);
  if (section.empty()) return 6;

  if (!declaresTheDocumentedDefaults(settings_header)) return 7;
  if (!exposesTheBoundPropertyNames(settings_header)) return 8;
  if (!boundsEveryNumberOnEveryPath(settings_source)) return 9;
  if (!persistsEveryValuePerProfile(settings_source)) return 10;
  if (!resetsToTheDocumentedDefaults(settings_source)) return 11;

  if (!publishesTheSpotModel(controller_header, controller_source)) return 12;
  if (!expiresAgainstOneClock(controller_source)) return 13;

  if (!staysReceiveOnly(provider_source, section)) return 14;
  if (!carriesNoCredentials(provider_source)) return 15;

  if (!refusesAMalformedBody()) return 16;
  if (!refusesAnOversizedBody()) return 17;
  if (!refusesAnOverlongList()) return 18;
  if (!dropsARecordWithoutCallsignOrFrequency()) return 19;
  if (!dropsAnImplausibleCallsign()) return 20;
  if (!dropsAnImpossibleFrequency()) return 21;
  if (!readsBothFrequencyUnits()) return 22;
  if (!separatesTheTwoKindsOfReport()) return 23;
  if (!datesEverySpotSafely()) return 24;
  if (!toleratesUnknownFieldsAndWrappers()) return 25;
  if (!refusesAnUnsafeEndpoint()) return 26;

  return 0;
}
