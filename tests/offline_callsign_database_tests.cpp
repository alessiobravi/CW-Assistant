#include <iostream>
#include <string>

#include "cwassistant/core/offline_callsign_database.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void testMasterAndHistoryImport() {
  using cwassistant::core::OfflineCallsignDatabase;
  OfflineCallsignDatabase database;
  const auto imported = database.importText(
      "# generated master file\n"
      "!!Order!!,Call,Name\n"
      "em90zmv\n"
      "IK3EYN extra-master-data\n"
      "4X5LL,Operator,City\n"
      "W1AW;ARRL;CT\n"
      "EM90ZMV,duplicate\n"
      "FOO1,invalid-digit-position\n"
      "A1B2,invalid-separated-digits\n"
      "not-a-call,ignored\n"
      "; comment\n");
  expect(imported.accepted && imported.inserted_records == 4U &&
             imported.duplicate_records == 1U && database.size() == 4U,
         "master and comma/semicolon history records import and deduplicate");
  expect(database.contains(" em90zmv ") && database.contains("IK3EYN") &&
             database.contains("4x5ll") && database.contains("W1AW"),
         "exact lookup uses normalized callsigns");
  expect(!database.contains("EM90Z?V") && !database.contains("N0CALL"),
         "exact lookup neither interprets wildcards nor invents nonmatches");
  expect(!database.contains("FOO1") && !database.contains("A1B2"),
         "structurally invalid normalized tokens do not consume records");
}

void testBoundedImport() {
  using cwassistant::core::OfflineCallsignDatabase;
  using cwassistant::core::OfflineCallsignDatabaseLimits;
  OfflineCallsignDatabase oversized({.maximum_input_bytes = 8U});
  const auto rejected = oversized.importText("EM90ZMV\nW1AW\n");
  expect(!rejected.accepted && oversized.size() == 0U,
         "oversized input is rejected without a partial import");

  OfflineCallsignDatabase bounded({.maximum_input_bytes = 1'024U,
                                   .maximum_records = 2U,
                                   .maximum_line_bytes = 16U});
  const auto result = bounded.importText(
      "K1ABC\nW1AW\nVE3XYZ\nTHIS-LINE-IS-FAR-TOO-LONG\n");
  expect(result.accepted && result.capacity_reached &&
             result.inserted_records == 2U && result.overlong_lines == 1U &&
             bounded.size() == 2U,
         "record and line limits bound imported state");
}

void testWildcardAndEditDistanceRanking() {
  using cwassistant::core::CallsignDifferenceKind;
  using cwassistant::core::OfflineCallsignDatabase;
  OfflineCallsignDatabase database;
  static_cast<void>(database.importText(
      "EM90ZMV\nEM91ZMV\nEM90ZM\nEN90ZMV\nZM90ZMV\n"));

  const auto wildcard = database.query("EM9?ZMV", 0U, 8U);
  expect(wildcard.size() == 2U && wildcard[0].callsign == "EM90ZMV" &&
             wildcard[1].callsign == "EM91ZMV",
         "a question mark matches one character with deterministic ordering");
  expect(wildcard[0].edit_distance == 0U &&
             wildcard[0].differences.size() == 1U &&
             wildcard[0].differences[0].kind ==
                 CallsignDifferenceKind::wildcard_match &&
             wildcard[0].differences[0].query_index == 3U,
         "wildcard evidence identifies the exact differing character");

  const auto edited = database.query("EM90ZMV", 1U, 4U);
  expect(edited.size() == 4U && edited[0].callsign == "EM90ZMV" &&
             edited[0].edit_distance == 0U &&
             edited[1].callsign == "EM90ZM" &&
             edited[2].callsign == "EM91ZMV" &&
             edited[3].callsign == "EN90ZMV",
         "exact, insertion/deletion, and substitution matches rank "
         "deterministically");
  expect(edited[1].differences.size() == 1U &&
             edited[1].differences[0].kind ==
                 CallsignDifferenceKind::deletion,
         "edit results retain per-character alignment evidence");
  expect(edited[2].differences.size() == 1U &&
             edited[2].differences[0].kind ==
                 CallsignDifferenceKind::substitution &&
             edited[2].differences[0].query_character == '0' &&
             edited[2].differences[0].callsign_character == '1',
         "substitution evidence identifies both characters");

  const auto inserted = database.query("EM90ZM", 1U, 3U);
  expect(inserted.size() == 2U && inserted[1].callsign == "EM90ZMV" &&
             inserted[1].differences.size() == 1U &&
             inserted[1].differences[0].kind ==
                 CallsignDifferenceKind::insertion &&
             inserted[1].differences[0].callsign_character == 'V',
         "insertion evidence identifies the candidate-only character");

  const auto limited = database.query("EM90ZMV", 4U, 2U);
  expect(limited.size() == 2U,
         "caller result limit bounds fuzzy output");

  OfflineCallsignDatabase configured(
      {.maximum_query_results = 2U, .maximum_edit_distance = 1U});
  static_cast<void>(configured.importText(
      "EM90ZMV\nEM91ZMV\nEM90ZM\nEN90ZMV\n"));
  expect(configured.query("EM90ZMV", 9U, 20U).size() == 2U,
         "configured distance and result limits bound fuzzy output");
}

}  // namespace

int main() {
  testMasterAndHistoryImport();
  testBoundedImport();
  testWildcardAndEditDistanceRanking();
  if (failures == 0) {
    std::cout << "Offline callsign database tests passed\n";
    return 0;
  }
  std::cerr << failures << " offline callsign database test(s) failed\n";
  return 1;
}
