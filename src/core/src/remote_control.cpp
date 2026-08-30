#include "cwassistant/core/remote_control.hpp"

#include <algorithm>
#include <cctype>

namespace cwassistant::core {

bool ControlLeaseManager::valid_identifier(const std::string_view identifier) {
  if (identifier.empty() || identifier.size() > 128) {
    return false;
  }
  return std::ranges::all_of(identifier, [](const unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_' ||
           character == '.' || character == ':';
  });
}

std::chrono::milliseconds ControlLeaseManager::clamp_ttl(
    const std::chrono::milliseconds requested_ttl) {
  return std::clamp(requested_ttl,
                    std::chrono::duration_cast<std::chrono::milliseconds>(kMinimumTtl),
                    std::chrono::duration_cast<std::chrono::milliseconds>(kMaximumTtl));
}

bool ControlLeaseManager::acquire(std::string client_id,
                                  std::string rig_id,
                                  const TimePoint now,
                                  const std::chrono::milliseconds requested_ttl) {
  if (!valid_identifier(client_id) || !valid_identifier(rig_id)) {
    return false;
  }
  static_cast<void>(expire(now));
  const auto existing = leases_by_rig_.find(rig_id);
  if (existing != leases_by_rig_.end() && existing->second.client_id != client_id) {
    return false;
  }
  leases_by_rig_.insert_or_assign(
      std::move(rig_id), Lease{std::move(client_id), now + clamp_ttl(requested_ttl)});
  return true;
}

bool ControlLeaseManager::renew(const std::string_view client_id,
                                const std::string_view rig_id,
                                const TimePoint now,
                                const std::chrono::milliseconds requested_ttl) {
  static_cast<void>(expire(now));
  const auto existing = leases_by_rig_.find(std::string(rig_id));
  if (existing == leases_by_rig_.end() || existing->second.client_id != client_id) {
    return false;
  }
  existing->second.expires_at = now + clamp_ttl(requested_ttl);
  return true;
}

bool ControlLeaseManager::release(const std::string_view client_id,
                                  const std::string_view rig_id) {
  const auto existing = leases_by_rig_.find(std::string(rig_id));
  if (existing == leases_by_rig_.end() || existing->second.client_id != client_id) {
    return false;
  }
  leases_by_rig_.erase(existing);
  return true;
}

bool ControlLeaseManager::owns(const std::string_view client_id,
                               const std::string_view rig_id,
                               const TimePoint now) {
  static_cast<void>(expire(now));
  const auto existing = leases_by_rig_.find(std::string(rig_id));
  return existing != leases_by_rig_.end() && existing->second.client_id == client_id;
}

std::size_t ControlLeaseManager::expire(const TimePoint now) {
  std::size_t expired = 0;
  for (auto current = leases_by_rig_.begin(); current != leases_by_rig_.end();) {
    if (current->second.expires_at <= now) {
      current = leases_by_rig_.erase(current);
      ++expired;
    } else {
      ++current;
    }
  }
  return expired;
}

std::optional<ControlLeaseSnapshot> ControlLeaseManager::current(
    const std::string_view rig_id,
    const TimePoint now) {
  static_cast<void>(expire(now));
  const auto existing = leases_by_rig_.find(std::string(rig_id));
  if (existing == leases_by_rig_.end()) {
    return std::nullopt;
  }
  return ControlLeaseSnapshot{
      .client_id = existing->second.client_id,
      .rig_id = existing->first,
      .remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          existing->second.expires_at - now),
  };
}

}  // namespace cwassistant::core
