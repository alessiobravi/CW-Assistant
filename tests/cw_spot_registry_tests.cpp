#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cw_spot_registry.hpp"

namespace {

int failures = 0;
int fixture_failures = 0;

// A failed expectation means the registry misbehaved. A failed fixture
// precondition means the test itself is built on an assumption that no longer
// holds, which is a different problem and gets its own exit code so a run can
// be told apart at a glance.
constexpr int kExpectationFailureExit = 1;
constexpr int kFixtureFailureExit = 2;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void require_fixture(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FIXTURE: " << message << '\n';
    ++fixture_failures;
  }
}

constexpr std::uint64_t kSecond = 1'000'000'000ULL;
constexpr std::uint64_t kMinute = 60ULL * kSecond;
// The default retention window declared by the registry's own limits. The
// tests restate it so a change to the default is caught here rather than
// silently altering what every timing case means.
constexpr std::uint64_t kDefaultRetentionNs = 15ULL * kMinute;
constexpr double kDefaultToleranceHz = 250.0;
// An arbitrary but comfortably positive origin, so that a case can look
// backwards from "now" without the unsigned clock underflowing.
constexpr std::uint64_t kBaseNs = 3600ULL * kSecond;

cwassistant::core::CwSpot spotFor(
    const std::string_view callsign, const double frequency_hz,
    const std::uint64_t observed_ns,
    const cwassistant::core::CwSpotSource source) {
  // Every field is assigned explicitly: a partial designated initializer trips
  // -Wmissing-field-initializers in the strict GCC build.
  cwassistant::core::CwSpot spot;
  spot.callsign = std::string(callsign);
  spot.frequency_hz = frequency_hz;
  spot.observed_ns = observed_ns;
  spot.spotter = "TEST-SKIMMER";
  spot.source = source;
  return spot;
}

cwassistant::core::CwSpotRegistry::Limits limitsFor(
    const std::size_t maximum_spots, const std::uint64_t retention_ns,
    const double match_tolerance_hz) {
  cwassistant::core::CwSpotRegistry::Limits limits;
  limits.maximum_spots = maximum_spots;
  limits.retention_ns = retention_ns;
  limits.match_tolerance_hz = match_tolerance_hz;
  return limits;
}

// The registry deliberately owns no callsign syntax of its own, so these cases
// only mean what they are supposed to mean while the shared policy still
// agrees about which of them are callsigns.
void check_fixture_assumptions() {
  using cwassistant::core::CallsignPolicy;
  require_fixture(CallsignPolicy::normalize("EA1EYL").has_value(),
                  "the shared policy still accepts the valid fixture call");
  require_fixture(CallsignPolicy::normalize("ea1eyl").value_or("") == "EA1EYL",
                  "the shared policy still upper-cases a callsign");
  require_fixture(!CallsignPolicy::normalize("ABC").has_value(),
                  "the shared policy still rejects a digitless token");
  require_fixture(!CallsignPolicy::normalize("EA1EYL!").has_value(),
                  "the shared policy still rejects punctuation");
  require_fixture(
      kDefaultRetentionNs == cwassistant::core::CwSpotRegistry::Limits{}
                                 .retention_ns,
      "the registry's default retention window is still fifteen minutes");
  require_fixture(
      std::fabs(cwassistant::core::CwSpotRegistry::Limits{}.match_tolerance_hz -
                kDefaultToleranceHz) < 1e-9,
      "the registry's default match tolerance is still 250 Hz");
}

// A spotting feed is untrusted input. Anything that is not a callsign, not a
// real positive frequency, or not current evidence is dropped at the door
// rather than stored and reasoned about later.
void test_malformed_reports_are_ignored() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t now = kBaseNs;

  expect(!registry.add(spotFor("", 14'030'000.0, now, CwSpotSource::Cluster),
                       now),
         "an empty callsign is not a spot");
  expect(!registry.add(spotFor("ABC", 14'030'000.0, now, CwSpotSource::Cluster),
                       now),
         "a token without a digit is not a callsign");
  expect(!registry.add(spotFor("A1", 14'030'000.0, now, CwSpotSource::Cluster),
                       now),
         "a two-character token is too short to be a callsign");
  expect(
      !registry.add(
          spotFor("EA1EYL!", 14'030'000.0, now, CwSpotSource::ReverseBeacon),
          now),
      "punctuation in a callsign is refused rather than stripped");
  expect(!registry.add(
             spotFor("EA1EYL/", 14'030'000.0, now, CwSpotSource::Cluster), now),
         "a trailing portable separator with no modifier is refused");
  expect(!registry.add(spotFor("EA1EYLEA1EYLEA1EY", 14'030'000.0, now,
                               CwSpotSource::Cluster),
                       now),
         "an over-long token is refused before it is stored");

  expect(!registry.add(spotFor("EA1EYL",
                               std::numeric_limits<double>::quiet_NaN(), now,
                               CwSpotSource::Cluster),
                       now),
         "a frequency that is not a number is refused");
  expect(!registry.add(spotFor("EA1EYL",
                               std::numeric_limits<double>::infinity(), now,
                               CwSpotSource::Cluster),
                       now),
         "an infinite frequency is refused");
  expect(!registry.add(spotFor("EA1EYL",
                               -std::numeric_limits<double>::infinity(), now,
                               CwSpotSource::Cluster),
                       now),
         "a negative infinite frequency is refused");
  expect(!registry.add(spotFor("EA1EYL", 0.0, now, CwSpotSource::Cluster), now),
         "a zero frequency is refused");
  expect(!registry.add(spotFor("EA1EYL", -14'030'000.0, now,
                               CwSpotSource::Cluster),
                       now),
         "a negative frequency is refused");

  expect(!registry.add(spotFor("EA1EYL", 14'030'000.0,
                               now - kDefaultRetentionNs - 1ULL,
                               CwSpotSource::Cluster),
                       now),
         "an observation already past the retention window is refused");
  expect(!registry.add(spotFor("EA1EYL", 14'030'000.0,
                               now + kDefaultRetentionNs + 1ULL,
                               CwSpotSource::Cluster),
                       now),
         "an observation stamped further ahead than the whole window is "
         "refused, because expiry could never reach it");

  expect(registry.size() == 0U,
         "nothing malformed reached the store");
  expect(registry.all(now).empty(),
         "a store that refused everything reports nothing");

  // The two ends of the accepted range, which is where an off-by-one in the
  // unsigned clock arithmetic would show up.
  expect(registry.add(spotFor("EA1EYL", 14'030'000.0,
                              now - kDefaultRetentionNs, CwSpotSource::Cluster),
                      now),
         "an observation exactly at the edge of the window is still current");
  expect(registry.add(spotFor("IU0LFQ", 14'031'000.0,
                              now + kDefaultRetentionNs,
                              CwSpotSource::ReverseBeacon),
                      now),
         "modest feed clock skew ahead of local time is tolerated");
  expect(registry.size() == 2U, "both boundary reports were stored");
}

void test_expiry_drops_stale_reports() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t observed = kBaseNs;
  expect(registry.add(spotFor("EA1EYL", 14'030'000.0, observed,
                              CwSpotSource::ReverseBeacon),
                      observed),
         "the expiry fixture is stored");
  expect(registry.add(
             spotFor("IU0LFQ", 14'031'000.0, observed, CwSpotSource::Cluster),
             observed),
         "the second expiry fixture is stored");

  registry.expire(observed + kDefaultRetentionNs);
  expect(registry.size() == 2U,
         "a report exactly at the edge of the window survives expiry");

  const std::uint64_t after_window = observed + kDefaultRetentionNs + 1ULL;
  // Reading is not sweeping. A query has to ignore what has aged out, but a
  // const query must not quietly rewrite the store while it does so.
  expect(registry.all(after_window).empty(),
         "a query past the window reports nothing current");
  expect(!registry.forCallsign("EA1EYL", after_window).has_value(),
         "a lookup past the window finds nothing current");
  expect(registry.nearFrequency(14'030'000.0, after_window).empty(),
         "a frequency query past the window reports nothing current");
  expect(registry.size() == 2U,
         "a const query leaves the stored reports alone");

  registry.expire(after_window);
  expect(registry.size() == 0U, "expiry drops everything past the window");

  // A window of zero is a legitimate configuration and must not turn into an
  // infinite one through unsigned arithmetic.
  CwSpotRegistry immediate(limitsFor(8U, 0ULL, kDefaultToleranceHz));
  expect(immediate.add(spotFor("EA1EYL", 14'030'000.0, kBaseNs,
                               CwSpotSource::Cluster),
                       kBaseNs),
         "a report observed exactly now survives a zero-length window");
  immediate.expire(kBaseNs + 1ULL);
  expect(immediate.size() == 0U,
         "a zero-length window expires a report one nanosecond later");
}

// A network feed decides how many reports arrive; the operator's memory budget
// must not.
void test_the_cap_evicts_the_oldest_report() {
  using namespace cwassistant::core;
  CwSpotRegistry registry(
      limitsFor(3U, kDefaultRetentionNs, kDefaultToleranceHz));

  expect(registry.add(spotFor("EA1EYL", 14'030'000.0, kBaseNs,
                              CwSpotSource::Cluster),
                      kBaseNs),
         "the first report fits under the cap");
  expect(registry.add(spotFor("IU0LFQ", 14'031'000.0, kBaseNs + kSecond,
                              CwSpotSource::Cluster),
                      kBaseNs + kSecond),
         "the second report fits under the cap");
  expect(registry.add(spotFor("DL1NKB", 14'032'000.0, kBaseNs + 2ULL * kSecond,
                              CwSpotSource::Cluster),
                      kBaseNs + 2ULL * kSecond),
         "the third report fills the cap");
  expect(registry.size() == 3U, "the store holds exactly the cap");

  const std::uint64_t newest = kBaseNs + 3ULL * kSecond;
  expect(registry.add(spotFor("K1ABC", 14'033'000.0, newest,
                              CwSpotSource::Cluster),
                      newest),
         "a newer report is accepted at the cap");
  expect(registry.size() == 3U, "the store never grows past the cap");
  expect(!registry.forCallsign("EA1EYL", newest).has_value(),
         "the oldest report is the one evicted");
  expect(registry.forCallsign("K1ABC", newest).has_value(),
         "the newest report is the one kept");
  expect(registry.forCallsign("DL1NKB", newest).has_value(),
         "an untouched report survives someone else's eviction");

  // Reports do not always arrive in order. One that is older than everything
  // already held has nothing to offer a full store of fresher evidence.
  expect(!registry.add(spotFor("W1AW", 14'034'000.0, kBaseNs,
                               CwSpotSource::Cluster),
                       newest),
         "a stale late arrival cannot displace fresher evidence at the cap");
  expect(registry.size() == 3U && !registry.forCallsign("W1AW", newest),
         "the refused late arrival left the store as it was");

  // Refreshing a station already held is not a new occupant, so it must not
  // cost some other station its slot.
  expect(registry.add(spotFor("DL1NKB", 14'032'100.0, newest + kSecond,
                              CwSpotSource::Cluster),
                      newest + kSecond),
         "refreshing a held station is accepted at the cap");
  expect(registry.size() == 3U &&
             registry.forCallsign("K1ABC", newest + kSecond).has_value(),
         "a refresh evicts nobody");

  CwSpotRegistry disabled(
      limitsFor(0U, kDefaultRetentionNs, kDefaultToleranceHz));
  expect(!disabled.add(spotFor("EA1EYL", 14'030'000.0, kBaseNs,
                               CwSpotSource::Cluster),
                       kBaseNs),
         "a cap of zero stores nothing at all");
  expect(disabled.size() == 0U, "a cap of zero stays empty");
}

// The whole point of keeping the two feeds apart: agreement between them is
// visible on one match instead of being split across two.
void test_both_sources_merge_into_one_match() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t heard = kBaseNs;
  const std::uint64_t reported = kBaseNs + 30ULL * kSecond;

  expect(registry.add(spotFor("EA1EYL", 14'030'000.0, heard,
                              CwSpotSource::ReverseBeacon),
                      reported),
         "the reverse-beacon report is stored");
  expect(registry.add(
             spotFor("EA1EYL", 14'030'100.0, reported, CwSpotSource::Cluster),
             reported),
         "the cluster report for the same station is stored");
  expect(registry.size() == 2U,
         "two sources are retained as two separate reports");

  const auto matches = registry.all(reported);
  expect(matches.size() == 1U, "two sources collapse into one station");
  if (matches.size() == 1U) {
    const CwSpotMatch& match = matches.front();
    expect(match.callsign == "EA1EYL", "the match names the station");
    expect(match.reverse_beacon && match.cluster,
           "a station heard by both sources reports both");
    expect(match.observations == 2U,
           "the match counts the reports behind it");
    expect(match.newest_observation_ns == reported,
           "the match carries the newest observation time");
    expect(std::fabs(match.frequency_hz - 14'030'100.0) < 1e-9,
           "the newest report says where the station is now");
  }

  const auto by_callsign = registry.forCallsign("EA1EYL", reported);
  expect(by_callsign.has_value() && by_callsign->reverse_beacon &&
             by_callsign->cluster && by_callsign->observations == 2U,
         "a lookup sees the same merged evidence");

  const auto by_frequency = registry.nearFrequency(14'030'000.0, reported);
  expect(by_frequency.size() == 1U && by_frequency.front().reverse_beacon &&
             by_frequency.front().cluster,
         "a frequency query sees the same merged evidence");

  // Corroboration is per frequency. The same call reported on another band is
  // not evidence that two sources agree about this one.
  CwSpotRegistry split;
  expect(split.add(spotFor("IU0LFQ", 14'030'000.0, heard,
                           CwSpotSource::ReverseBeacon),
                   reported) &&
             split.add(spotFor("IU0LFQ", 7'030'000.0, reported,
                               CwSpotSource::Cluster),
                       reported),
         "the two-band fixture is stored");
  const auto on_twenty = split.nearFrequency(14'030'000.0, reported);
  expect(on_twenty.size() == 1U && on_twenty.front().reverse_beacon &&
             !on_twenty.front().cluster && on_twenty.front().observations == 1U,
         "a frequency query counts only the reports near that frequency");
  const auto everywhere = split.all(reported);
  expect(everywhere.size() == 1U && everywhere.front().reverse_beacon &&
             everywhere.front().cluster &&
             everywhere.front().observations == 2U,
         "the unfiltered view still shows both bands' reports");
}

void test_one_source_refreshes_rather_than_duplicates() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t first = kBaseNs;
  const std::uint64_t second = kBaseNs + 60ULL * kSecond;

  expect(registry.add(spotFor("EA1EYL", 14'030'000.0, first,
                              CwSpotSource::ReverseBeacon),
                      first),
         "the first reverse-beacon report is stored");
  expect(registry.add(spotFor("EA1EYL", 14'035'000.0, second,
                              CwSpotSource::ReverseBeacon),
                      second),
         "the same source reporting again is accepted");
  expect(registry.size() == 1U,
         "one source reporting one station twice stores one report");

  const auto refreshed = registry.forCallsign("EA1EYL", second);
  expect(refreshed.has_value() && refreshed->observations == 1U,
         "a refresh is not a second corroborating report");
  expect(refreshed && std::fabs(refreshed->frequency_hz - 14'035'000.0) < 1e-9,
         "a refresh moves the station to where it was last reported");
  expect(refreshed && refreshed->newest_observation_ns == second,
         "a refresh advances the observation time");

  // Feeds deliver out of order. An older report is still valid input, but it
  // is not news, and it must not drag the station backwards.
  expect(registry.add(spotFor("EA1EYL", 14'030'000.0, first,
                              CwSpotSource::ReverseBeacon),
                      second),
         "an out-of-order report from the same source is accepted");
  const auto unchanged = registry.forCallsign("EA1EYL", second);
  expect(unchanged && unchanged->newest_observation_ns == second &&
             std::fabs(unchanged->frequency_hz - 14'035'000.0) < 1e-9,
         "an out-of-order report does not roll the station back");

  expect(registry.add(
             spotFor("EA1EYL", 14'035'000.0, second, CwSpotSource::Cluster),
             second),
         "the other source is not treated as a refresh");
  expect(registry.size() == 2U,
         "the two sources are kept apart even for one station");
}

void test_frequency_tolerance_and_ordering() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t now = kBaseNs;
  const double centre = 14'030'000.0;

  expect(registry.add(spotFor("EA1EYL", centre + kDefaultToleranceHz, now,
                              CwSpotSource::Cluster),
                      now),
         "the upper-edge fixture is stored");
  expect(registry.add(spotFor("IU0LFQ", centre - kDefaultToleranceHz, now,
                              CwSpotSource::Cluster),
                      now),
         "the lower-edge fixture is stored");
  expect(registry.add(spotFor("DL1NKB", centre + kDefaultToleranceHz + 1.0, now,
                              CwSpotSource::Cluster),
                      now),
         "the just-outside fixture is stored");
  expect(registry.add(spotFor("K1ABC", centre - kDefaultToleranceHz - 1.0, now,
                              CwSpotSource::Cluster),
                      now),
         "the just-below fixture is stored");

  const auto matches = registry.nearFrequency(centre, now);
  expect(matches.size() == 2U,
         "the tolerance includes its own edges and excludes one hertz beyond");
  const auto names_only = [&matches](const std::string_view callsign) {
    for (const CwSpotMatch& match : matches) {
      if (match.callsign == callsign) return true;
    }
    return false;
  };
  expect(names_only("EA1EYL") && names_only("IU0LFQ"),
         "both edge stations are on frequency");
  expect(!names_only("DL1NKB") && !names_only("K1ABC"),
         "neither station past the edge is on frequency");

  expect(registry.nearFrequency(std::numeric_limits<double>::quiet_NaN(), now).empty(),
         "a frequency that is not a number matches nothing");

  // Newest first, because the useful question is who is on frequency now.
  CwSpotRegistry ordered;
  expect(ordered.add(spotFor("EA1EYL", centre, kBaseNs,
                             CwSpotSource::Cluster),
                     kBaseNs + 2ULL * kMinute) &&
             ordered.add(spotFor("IU0LFQ", centre, kBaseNs + kMinute,
                                 CwSpotSource::Cluster),
                         kBaseNs + 2ULL * kMinute) &&
             ordered.add(spotFor("DL1NKB", centre, kBaseNs + 2ULL * kMinute,
                                 CwSpotSource::Cluster),
                         kBaseNs + 2ULL * kMinute),
         "the ordering fixture is stored");
  const auto by_age = ordered.nearFrequency(centre, kBaseNs + 2ULL * kMinute);
  expect(by_age.size() == 3U, "every station on frequency is reported");
  if (by_age.size() == 3U) {
    expect(by_age[0].callsign == "DL1NKB" && by_age[1].callsign == "IU0LFQ" &&
               by_age[2].callsign == "EA1EYL",
           "stations on frequency are ordered newest first");
  }
  const auto everything = ordered.all(kBaseNs + 2ULL * kMinute);
  expect(everything.size() == 3U && everything.front().callsign == "DL1NKB",
         "the unfiltered view uses the same newest-first order");

  // A tolerance that is not a real, non-negative number cannot be allowed to
  // match a whole band, so it falls back to exact agreement.
  CwSpotRegistry nonsense(limitsFor(
      8U, kDefaultRetentionNs, std::numeric_limits<double>::quiet_NaN()));
  expect(nonsense.add(spotFor("EA1EYL", centre, now, CwSpotSource::Cluster),
                      now),
         "the nonsensical-tolerance fixture is stored");
  expect(nonsense.nearFrequency(centre, now).size() == 1U,
         "a nonsensical tolerance still matches the exact frequency");
  expect(nonsense.nearFrequency(centre + 1.0, now).empty(),
         "a nonsensical tolerance does not match the whole band");
}

void test_lookup_is_case_and_spacing_insensitive() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t now = kBaseNs;
  expect(registry.add(
             spotFor("ea1eyl", 14'030'000.0, now, CwSpotSource::Cluster), now),
         "a lower-case report is accepted");

  const auto upper = registry.forCallsign("EA1EYL", now);
  expect(upper.has_value() && upper->callsign == "EA1EYL",
         "a lookup is case-insensitive and reports the normalized callsign");
  expect(registry.forCallsign("ea1eyl", now).has_value(),
         "the original casing finds the same station");
  expect(registry.forCallsign("  Ea1Eyl  ", now).has_value(),
         "surrounding whitespace does not hide a station");
  expect(!registry.forCallsign("K1ABC", now).has_value(),
         "a station nobody reported is absent rather than invented");
  expect(!registry.forCallsign("EA1EYL!", now).has_value(),
         "a malformed lookup finds nothing instead of guessing");
  expect(!registry.forCallsign("", now).has_value(),
         "an empty lookup finds nothing");

  // One station stays one station however many times it is reported in
  // whatever casing.
  expect(registry.add(
             spotFor("Ea1Eyl", 14'030'000.0, now, CwSpotSource::ReverseBeacon),
             now),
         "a differently cased report of the same station is accepted");
  expect(registry.size() == 2U && registry.all(now).size() == 1U,
         "casing never splits one station into two");
}

// A report is evidence about one station at one moment. It must not quietly
// alter the caller's own data, the other stations, or the store's own limits.
void test_a_report_disturbs_nothing_it_was_not_given() {
  using namespace cwassistant::core;
  CwSpotRegistry registry;
  const std::uint64_t now = kBaseNs;

  CwSpot submitted = spotFor("ea1eyl", 14'030'000.0, now,
                             CwSpotSource::ReverseBeacon);
  const CwSpot original = submitted;
  expect(registry.add(submitted, now), "the caller's report is accepted");
  expect(submitted.callsign == original.callsign &&
             submitted.spotter == original.spotter &&
             submitted.source == original.source &&
             submitted.observed_ns == original.observed_ns &&
             std::fabs(submitted.frequency_hz - original.frequency_hz) < 1e-9,
         "the caller's report is copied, never normalized in place");

  expect(registry.add(
             spotFor("IU0LFQ", 14'031'000.0, now, CwSpotSource::Cluster), now),
         "a second station is stored");
  const auto before = registry.forCallsign("IU0LFQ", now);
  expect(before.has_value(), "the second station is present");

  expect(!registry.add(
             spotFor("nonsense", 14'031'000.0, now, CwSpotSource::Cluster),
             now),
         "a malformed report is refused");
  expect(registry.size() == 2U, "a refused report stores nothing");
  const auto after_refusal = registry.forCallsign("IU0LFQ", now);
  expect(after_refusal.has_value() && before.has_value() &&
             after_refusal->observations == before->observations &&
             after_refusal->newest_observation_ns ==
                 before->newest_observation_ns &&
             after_refusal->reverse_beacon == before->reverse_beacon &&
             after_refusal->cluster == before->cluster,
         "a refused report leaves the other stations exactly as they were");

  expect(registry.add(spotFor("DL1NKB", 14'031'000.0, now + kSecond,
                              CwSpotSource::ReverseBeacon),
                      now + kSecond),
         "a third station on the same frequency is stored");
  const auto neighbour = registry.forCallsign("IU0LFQ", now + kSecond);
  expect(neighbour.has_value() && !neighbour->reverse_beacon &&
             neighbour->observations == 1U,
         "a neighbouring station's sources are never borrowed");

  const std::size_t size_before_queries = registry.size();
  static_cast<void>(registry.nearFrequency(14'031'000.0, now + kSecond));
  static_cast<void>(registry.all(now + kSecond));
  static_cast<void>(registry.forCallsign("DL1NKB", now + kSecond));
  expect(registry.size() == size_before_queries,
         "reading the registry never changes it");

  // A spotter name is free text from the feed. However long it is, it cannot
  // grow the store or leak into what is presented.
  CwSpot verbose = spotFor("K1ABC", 14'032'000.0, now + kSecond,
                           CwSpotSource::Cluster);
  verbose.spotter = std::string(4'096U, 'X');
  expect(registry.add(verbose, now + kSecond),
         "an over-long spotter name does not cost a valid report");
  const auto bounded = registry.forCallsign("K1ABC", now + kSecond);
  expect(bounded.has_value() && bounded->callsign == "K1ABC" &&
             bounded->observations == 1U,
         "an over-long spotter name changes nothing that is presented");

  registry.clear();
  expect(registry.size() == 0U && registry.all(now + kSecond).empty() &&
             !registry.forCallsign("EA1EYL", now + kSecond).has_value(),
         "clearing empties the store completely");
}

}  // namespace

int main() {
  check_fixture_assumptions();
  if (fixture_failures != 0) {
    std::cerr << fixture_failures
              << " CW spot registry fixture assumption(s) no longer hold\n";
    return kFixtureFailureExit;
  }

  test_malformed_reports_are_ignored();
  test_expiry_drops_stale_reports();
  test_the_cap_evicts_the_oldest_report();
  test_both_sources_merge_into_one_match();
  test_one_source_refreshes_rather_than_duplicates();
  test_frequency_tolerance_and_ordering();
  test_lookup_is_case_and_spacing_insensitive();
  test_a_report_disturbs_nothing_it_was_not_given();

  if (failures != 0) {
    std::cerr << failures << " CW spot registry test(s) failed\n";
    return kExpectationFailureExit;
  }
  std::cout << "CW spot registry tests passed\n";
  return EXIT_SUCCESS;
}
