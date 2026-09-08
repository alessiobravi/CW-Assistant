#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cwassistant/core/cw_transmit_encoder.hpp"

namespace cwassistant::core {

enum class CwTransmitSchedulerState {
  Idle,
  Running,
  Completed,
  Cancelled,
  Emergency,
  Fault,
};

enum class CwTransmitAdvanceResult {
  NoChange,
  Transition,
  Faulted,
};

struct CwTransmitSchedulerConfig {
  std::uint64_t ptt_lead_ns{100'000'000ULL};
  std::uint64_t ptt_hang_ns{100'000'000ULL};
  std::uint64_t maximum_message_duration_ns{600'000'000'000ULL};
  std::uint64_t maximum_transition_lateness_ns{250'000'000ULL};
};

struct CwTransmitLineState {
  bool ptt{false};
  bool key{false};
};

struct CwTransmitSchedulerSnapshot {
  CwTransmitSchedulerState state{CwTransmitSchedulerState::Idle};
  CwTransmitLineState lines{};
  std::uint64_t next_transition_ns{0};
  std::size_t active_span{0};
  bool release_pending{false};
};

// Deterministically sequences a validated, immutable copy of a CW plan. It
// owns no clock and no hardware: the caller supplies monotonic timestamps and
// applies each desired line-state transition before advancing again. At most
// one transition is exposed per call, so every terminal path makes KEY
// inactive before a following call makes PTT inactive.
class CwTransmitScheduler final {
 public:
  explicit CwTransmitScheduler(CwTransmitSchedulerConfig config = {}) noexcept;

  [[nodiscard]] bool start(const CwTransmitPlan& plan,
                           std::uint64_t now_ns);
  [[nodiscard]] CwTransmitAdvanceResult advance(std::uint64_t now_ns) noexcept;
  [[nodiscard]] bool cancel() noexcept;
  void emergencyRelease() noexcept;
  [[nodiscard]] bool reset() noexcept;

  [[nodiscard]] CwTransmitSchedulerSnapshot snapshot() const noexcept;
  [[nodiscard]] const CwTransmitPlan* plan() const noexcept;

  static constexpr std::uint64_t kMaximumContinuousKeyDownNs =
      3'000'000'000ULL;
  static constexpr std::uint64_t kMaximumLeadOrHangNs =
      5'000'000'000ULL;
  static constexpr std::uint64_t kMaximumAllowedMessageDurationNs =
      1'800'000'000'000ULL;
  static constexpr std::uint64_t kMaximumAllowedLatenessNs =
      3'000'000'000ULL;

 private:
  enum class Phase { None, Lead, Spans, Hang };

  [[nodiscard]] bool validConfig() const noexcept;
  [[nodiscard]] bool validatePlan(const CwTransmitPlan& plan) const noexcept;
  [[nodiscard]] bool setNextTransition(std::uint64_t base_ns,
                                       std::uint64_t delay_ns) noexcept;
  void enterTerminal(CwTransmitSchedulerState state) noexcept;
  [[nodiscard]] CwTransmitAdvanceResult tripFault() noexcept;

  CwTransmitSchedulerConfig config_{};
  std::optional<CwTransmitPlan> plan_{};
  CwTransmitSchedulerState state_{CwTransmitSchedulerState::Idle};
  Phase phase_{Phase::None};
  CwTransmitLineState lines_{};
  std::uint64_t last_advance_ns_{0};
  std::uint64_t next_transition_ns_{0};
  std::size_t span_index_{0};
  bool clock_initialized_{false};
  bool release_pending_{false};
};

}  // namespace cwassistant::core
