#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "cwassistant/core/cw_transmit_encoder.hpp"
#include "cwassistant/core/cw_transmit_scheduler.hpp"
#include "transmit/direct_keying_adapter.hpp"

class QThread;

namespace cwassistant::desktop {

struct DirectTransmitEngineConfig {
  DirectKeyingConfig keying;
  cwassistant::core::CwTransmitSchedulerConfig scheduler;
  std::uint64_t maximum_tune_duration_ns{15'000'000'000ULL};
};

class DirectTransmitWorker;

// Main-thread facade for direct serial transmission. The serial adapter,
// scheduler, monotonic clock, and precise timer live exclusively on a dedicated
// worker thread. Public state is a cached copy suitable for UI presentation;
// KEY is true only when the adapter reports the physical command active.
class DirectTransmitEngine final : public QObject {
  Q_OBJECT

 public:
  using BackendFactory =
      std::function<std::unique_ptr<DirectKeyingBackend>()>;

  explicit DirectTransmitEngine(QObject* parent = nullptr);
  DirectTransmitEngine(BackendFactory backend_factory,
                       QObject* parent = nullptr);
  ~DirectTransmitEngine() override;

  DirectTransmitEngine(const DirectTransmitEngine&) = delete;
  DirectTransmitEngine& operator=(const DirectTransmitEngine&) = delete;

  // Configuration is inert: it first releases and closes any current adapter.
  void configure(const DirectTransmitEngineConfig& config);
  // Blocking open returns true only after both lines are known inactive.
  [[nodiscard]] bool openSafe();
  // The explicit confirmation argument must come from the transmit guard's
  // exact-preview result. The worker always takes its own immutable plan copy.
  [[nodiscard]] bool start(const cwassistant::core::CwTransmitPlan& plan,
                           bool exact_preview_confirmed);
  // Cancellation and TUNE are completed synchronously. Both return only after
  // KEY then PTT have been commanded inactive (or a fault has been latched).
  [[nodiscard]] bool cancel();
  [[nodiscard]] bool startTune(bool operator_authorized);
  [[nodiscard]] bool stopTune();
  // Both operations block until KEY then PTT release has been attempted.
  void emergencyRelease();
  void close();

  [[nodiscard]] bool available() const noexcept { return available_; }
  [[nodiscard]] bool openSafeState() const noexcept { return open_safe_; }
  [[nodiscard]] bool busy() const noexcept { return busy_; }
  [[nodiscard]] bool ptt() const noexcept { return ptt_; }
  [[nodiscard]] bool key() const noexcept { return key_; }
  [[nodiscard]] bool fault() const noexcept { return fault_; }
  [[nodiscard]] const QString& status() const noexcept { return status_; }
  [[nodiscard]] std::uint64_t elapsedNs() const noexcept { return elapsed_ns_; }
  [[nodiscard]] std::uint64_t remainingNs() const noexcept {
    return remaining_ns_;
  }
  [[nodiscard]] double progress() const noexcept { return progress_; }

 signals:
  void changed();

 private:
  void initializeWorker(BackendFactory backend_factory);
  void updateCachedState(std::uint64_t revision, bool available,
                         bool open_safe, bool busy, bool ptt, bool key,
                         bool fault, QString status,
                         std::uint64_t elapsed_ns,
                         std::uint64_t remaining_ns, double progress);

  QThread* thread_{nullptr};
  DirectTransmitWorker* worker_{nullptr};
  bool available_{false};
  bool open_safe_{false};
  bool busy_{false};
  bool ptt_{false};
  bool key_{false};
  bool fault_{false};
  QString status_{QStringLiteral("Direct transmit adapter is not configured")};
  std::uint64_t elapsed_ns_{0U};
  std::uint64_t remaining_ns_{0U};
  double progress_{0.0};
  std::uint64_t cached_revision_{0U};
};

}  // namespace cwassistant::desktop
