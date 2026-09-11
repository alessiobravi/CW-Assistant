#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwassistant::core {

// Where an external observation of a station came from. The two are kept
// apart deliberately: a reverse-beacon report is a receiver hearing a signal,
// while a cluster spot is a person saying they heard one, and they fail in
// different ways. Agreement between them is stronger evidence than either.
enum class CwSpotSource { ReverseBeacon, Cluster };

struct CwSpot {
  std::string callsign;
  double frequency_hz{0.0};
  std::uint64_t observed_ns{0};
  std::string spotter;
  CwSpotSource source{CwSpotSource::Cluster};
};

struct CwSpotMatch {
  std::string callsign;
  double frequency_hz{0.0};
  bool reverse_beacon{false};
  bool cluster{false};
  std::uint64_t newest_observation_ns{0};
  std::size_t observations{0};
};

// A bounded, expiring store of what other receivers report hearing.
//
// This is corroboration and never authority. A spot may lower how much of its
// own evidence a decoded callsign needs before it is offered, and it may say
// that an external source disagrees, but it can never replace a decoded
// callsign with a spotted one: reverse-beacon reports carry a measured error
// rate approaching two per cent per receiver, and a wrong callsign presented
// confidently is worse than none. Nothing here reaches the transmit path.
class CwSpotRegistry {
 public:
  struct Limits {
    std::size_t maximum_spots{4'096};
    std::uint64_t retention_ns{15ULL * 60ULL * 1'000'000'000ULL};
    double match_tolerance_hz{250.0};
  };

  // Two constructors rather than one with a defaulted argument: a default
  // argument of `{}` needs the nested type's own default member initializers
  // while this class is still incomplete, which Clang rejects even though GCC
  // and MSVC accept it. The nested name is kept because callers read better
  // for it.
  CwSpotRegistry();
  explicit CwSpotRegistry(Limits limits);

  // Ignores a malformed callsign, a non-finite or non-positive frequency, and
  // an observation older than the retention window.
  bool add(const CwSpot& spot, std::uint64_t now_ns);

  // Drops everything older than the retention window.
  void expire(std::uint64_t now_ns);

  // Every station reported within the tolerance of a frequency, newest first.
  [[nodiscard]] std::vector<CwSpotMatch> near(double frequency_hz,
                                              std::uint64_t now_ns) const;

  // What is reported for one callsign, if anything is current.
  [[nodiscard]] std::optional<CwSpotMatch> forCallsign(
      std::string_view callsign, std::uint64_t now_ns) const;

  [[nodiscard]] std::vector<CwSpotMatch> all(std::uint64_t now_ns) const;
  [[nodiscard]] std::size_t size() const noexcept;
  void clear() noexcept;

 private:
  Limits limits_;
  std::vector<CwSpot> spots_;
};

}  // namespace cwassistant::core
