#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cwassistant::core {

enum class ApplicationRole {
  Standalone,
  StationServer,
  RemoteClient,
};

enum class RemoteBandwidthProfile {
  EventsOnly,
  SpectrumAndEvents,
  AudioSpectrumAndEvents,
  IqSpectrumAndEvents,
};

struct RemoteProtocolVersion {
  std::uint16_t major{1};
  std::uint16_t minor{0};
};

// The client submits a complete message. Dot/dash or PTT edge timing is never
// transported; the station server validates and schedules all keying locally.
struct RemoteCwTransmitRequest {
  std::string request_id;
  std::string client_id;
  std::string rig_id;
  std::string callsign;
  std::string message;
  std::uint16_t words_per_minute{20};
  std::uint8_t weight_percent{50};
  bool operator_confirmed{false};
};

struct ControlLeaseSnapshot {
  std::string client_id;
  std::string rig_id;
  std::chrono::milliseconds remaining{0};
};

// Single-threaded control-plane component. The server must call expire() from
// its heartbeat timer and release all hardware lines independently on session
// loss; a lease grants permission but never drives hardware itself.
class ControlLeaseManager {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  [[nodiscard]] bool acquire(std::string client_id,
                             std::string rig_id,
                             TimePoint now,
                             std::chrono::milliseconds requested_ttl);
  [[nodiscard]] bool renew(std::string_view client_id,
                           std::string_view rig_id,
                           TimePoint now,
                           std::chrono::milliseconds requested_ttl);
  [[nodiscard]] bool release(std::string_view client_id,
                             std::string_view rig_id);
  [[nodiscard]] bool owns(std::string_view client_id,
                          std::string_view rig_id,
                          TimePoint now);
  [[nodiscard]] std::size_t expire(TimePoint now);
  [[nodiscard]] std::optional<ControlLeaseSnapshot> current(
      std::string_view rig_id,
      TimePoint now);

  static constexpr auto kMinimumTtl = std::chrono::seconds(2);
  static constexpr auto kMaximumTtl = std::chrono::seconds(30);

 private:
  struct Lease {
    std::string client_id;
    TimePoint expires_at;
  };

  [[nodiscard]] static bool valid_identifier(std::string_view identifier);
  [[nodiscard]] static std::chrono::milliseconds clamp_ttl(
      std::chrono::milliseconds requested_ttl);

  std::unordered_map<std::string, Lease> leases_by_rig_;
};

}  // namespace cwassistant::core
