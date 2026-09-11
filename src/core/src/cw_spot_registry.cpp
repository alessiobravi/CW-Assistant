#include "cwassistant/core/cw_spot_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

// A spotter name is free text arriving from a network feed and is never part
// of a presented match, so it is retained only as a short diagnostic label.
// Bounding it means one malformed or hostile report costs a fixed number of
// bytes instead of whatever the feed chose to send.
constexpr std::size_t kMaximumSpotterLength = 32U;

[[nodiscard]] std::string boundedSpotter(const std::string& spotter) {
  if (spotter.size() <= kMaximumSpotterLength) return spotter;
  return spotter.substr(0, kMaximumSpotterLength);
}

// Timestamps are unsigned, so subtracting them in the wrong order wraps to an
// enormous value instead of going negative. Each direction is therefore tested
// separately rather than through a single difference.
[[nodiscard]] bool isCurrent(const CwSpot& spot, const std::uint64_t now_ns,
                             const std::uint64_t retention_ns) noexcept {
  if (spot.observed_ns >= now_ns) return true;
  return now_ns - spot.observed_ns <= retention_ns;
}

// Collapses the retained reports selected by the caller into one match per
// station. Aggregation is what makes a match meaningful: two sources naming
// the same station is the corroboration this registry exists to express, and
// it only appears once their separate reports are folded together.
template <typename Predicate>
[[nodiscard]] std::vector<CwSpotMatch> collectMatches(
    const std::vector<CwSpot>& spots, Predicate accepted) {
  std::vector<CwSpotMatch> matches;
  // The key borrows the stored callsign rather than the one inside the match
  // being built: the match vector reallocates as it grows, which would leave a
  // view of a short, in-place string pointing at a moved-from buffer. The
  // source vector is not touched here, so a view into it stays valid.
  std::unordered_map<std::string_view, std::size_t> index_by_callsign;
  for (const CwSpot& spot : spots) {
    if (!accepted(spot)) continue;
    const auto existing = index_by_callsign.find(spot.callsign);
    if (existing == index_by_callsign.end()) {
      CwSpotMatch match;
      match.callsign = spot.callsign;
      match.frequency_hz = spot.frequency_hz;
      match.reverse_beacon = spot.source == CwSpotSource::ReverseBeacon;
      match.cluster = spot.source == CwSpotSource::Cluster;
      match.newest_observation_ns = spot.observed_ns;
      match.observations = 1U;
      index_by_callsign.emplace(std::string_view(spot.callsign),
                                matches.size());
      matches.push_back(std::move(match));
      continue;
    }
    CwSpotMatch& match = matches[existing->second];
    match.reverse_beacon =
        match.reverse_beacon || spot.source == CwSpotSource::ReverseBeacon;
    match.cluster = match.cluster || spot.source == CwSpotSource::Cluster;
    ++match.observations;
    if (spot.observed_ns >= match.newest_observation_ns) {
      // A station that moves is reported again on its new frequency, so the
      // most recent report is the one that says where it is now.
      match.newest_observation_ns = spot.observed_ns;
      match.frequency_hz = spot.frequency_hz;
    }
  }

  std::sort(matches.begin(), matches.end(),
            [](const CwSpotMatch& left, const CwSpotMatch& right) {
              if (left.newest_observation_ns != right.newest_observation_ns) {
                return left.newest_observation_ns > right.newest_observation_ns;
              }
              // Equal timestamps are common when a feed delivers a batch. An
              // explicit tie-break keeps the presented order stable instead of
              // depending on insertion history.
              return left.callsign < right.callsign;
            });
  return matches;
}

}  // namespace

// Delegating rather than duplicating keeps the sanity check below on the one
// path every registry is built through.
CwSpotRegistry::CwSpotRegistry() : CwSpotRegistry(Limits{}) {}

CwSpotRegistry::CwSpotRegistry(Limits limits) : limits_(limits) {
  if (!std::isfinite(limits_.match_tolerance_hz) ||
      limits_.match_tolerance_hz < 0.0) {
    // A tolerance that is not a real, non-negative number makes every
    // comparison in nearFrequency() meaningless. Falling back to exact frequency
    // agreement fails closed; the alternative would quietly match a whole
    // band and present unrelated stations as if they were on frequency.
    limits_.match_tolerance_hz = 0.0;
  }
}

bool CwSpotRegistry::add(const CwSpot& spot, const std::uint64_t now_ns) {
  if (limits_.maximum_spots == 0U) return false;

  // The callsign syntax rules live in one place for the whole program. A
  // spotting feed does not get a second, looser definition of a valid call.
  const auto normalized = CallsignPolicy::normalize(spot.callsign);
  if (!normalized) return false;

  if (!std::isfinite(spot.frequency_hz) || spot.frequency_hz <= 0.0) {
    return false;
  }

  if (spot.observed_ns < now_ns &&
      now_ns - spot.observed_ns > limits_.retention_ns) {
    return false;
  }
  // Clock skew between a spotting network and this machine is ordinary, so a
  // report stamped slightly ahead of local time is kept. One stamped further
  // ahead than the whole retention window is refused: expiry could never reach
  // it, so it would sit in a bounded store as permanently unexpirable content.
  if (spot.observed_ns > now_ns &&
      spot.observed_ns - now_ns > limits_.retention_ns) {
    return false;
  }

  // Sweeping before inserting means a report that has already aged out never
  // costs a current one its slot, and keeps the store self-cleaning for a
  // caller that only ever adds.
  expire(now_ns);

  const auto existing =
      std::find_if(spots_.begin(), spots_.end(), [&](const CwSpot& stored) {
        return stored.source == spot.source && stored.callsign == *normalized;
      });
  if (existing != spots_.end()) {
    // One source naming the same station again is a refreshed report, not a
    // second station, so it replaces what that source said before. A report
    // that arrives out of order is still accepted, but it does not roll the
    // station's position and time back to an older observation.
    if (spot.observed_ns >= existing->observed_ns) {
      existing->frequency_hz = spot.frequency_hz;
      existing->observed_ns = spot.observed_ns;
      existing->spotter = boundedSpotter(spot.spotter);
    }
    return true;
  }

  if (spots_.size() >= limits_.maximum_spots) {
    const auto oldest = std::min_element(
        spots_.begin(), spots_.end(),
        [](const CwSpot& left, const CwSpot& right) {
          return left.observed_ns < right.observed_ns;
        });
    if (oldest == spots_.end() || spot.observed_ns <= oldest->observed_ns) {
      // At the cap the store keeps the newest reports it has seen. A late
      // arrival that is older than everything already held would otherwise
      // displace fresher evidence with staler evidence.
      return false;
    }
    spots_.erase(oldest);
  }

  CwSpot stored;
  // The normalized form is what gets stored, which is what makes every later
  // lookup case- and whitespace-insensitive without a second set of rules.
  stored.callsign = *normalized;
  stored.frequency_hz = spot.frequency_hz;
  stored.observed_ns = spot.observed_ns;
  stored.spotter = boundedSpotter(spot.spotter);
  stored.source = spot.source;
  spots_.push_back(std::move(stored));
  return true;
}

void CwSpotRegistry::expire(const std::uint64_t now_ns) {
  const auto stale = std::remove_if(
      spots_.begin(), spots_.end(), [&](const CwSpot& spot) {
        return !isCurrent(spot, now_ns, limits_.retention_ns);
      });
  spots_.erase(stale, spots_.end());
}

std::vector<CwSpotMatch> CwSpotRegistry::nearFrequency(
    const double frequency_hz, const std::uint64_t now_ns) const {
  if (!std::isfinite(frequency_hz)) return {};
  return collectMatches(spots_, [&](const CwSpot& spot) {
    // Only the reports actually near the asked-for frequency are folded in, so
    // a station heard on two bands does not claim both sources here on the
    // strength of a report from the other band.
    return isCurrent(spot, now_ns, limits_.retention_ns) &&
        std::fabs(spot.frequency_hz - frequency_hz) <=
            limits_.match_tolerance_hz;
  });
}

std::optional<CwSpotMatch> CwSpotRegistry::forCallsign(
    const std::string_view callsign, const std::uint64_t now_ns) const {
  const auto normalized = CallsignPolicy::normalize(callsign);
  if (!normalized) return std::nullopt;
  const auto matches = collectMatches(spots_, [&](const CwSpot& spot) {
    return spot.callsign == *normalized &&
        isCurrent(spot, now_ns, limits_.retention_ns);
  });
  if (matches.empty()) return std::nullopt;
  // Every retained report for one callsign collapses into exactly one match.
  return matches.front();
}

std::vector<CwSpotMatch> CwSpotRegistry::all(const std::uint64_t now_ns) const {
  return collectMatches(spots_, [&](const CwSpot& spot) {
    return isCurrent(spot, now_ns, limits_.retention_ns);
  });
}

std::size_t CwSpotRegistry::size() const noexcept {
  // The number of retained reports, which is at most two per station and never
  // more than the configured cap. It counts reports rather than matches, and
  // it cannot drop reports that have aged out because it is given no clock;
  // add() and expire() are what sweep them.
  return spots_.size();
}

void CwSpotRegistry::clear() noexcept { spots_.clear(); }

}  // namespace cwassistant::core
