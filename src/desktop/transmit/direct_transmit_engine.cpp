#include "transmit/direct_transmit_engine.hpp"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace cwassistant::desktop {
namespace {

struct WorkerState {
  bool available{false};
  bool open_safe{false};
  bool busy{false};
  bool ptt{false};
  bool key{false};
  bool fault{false};
  QString status;
};

constexpr qint64 kAdapterHealthPollMs = 10;

bool validKeyingConfig(const DirectKeyingConfig& config) {
  return !config.port_name.trimmed().isEmpty() &&
      config.ptt_line != config.key_line && config.ptt_active_high &&
      config.key_active_high;
}

bool lineActive(const DirectKeyingLineState state) noexcept {
  return state == DirectKeyingLineState::Active;
}

}  // namespace

class DirectTransmitWorker final : public QObject {
  Q_OBJECT

 public:
  explicit DirectTransmitWorker(DirectTransmitEngine::BackendFactory factory)
      : backend_factory_(std::move(factory)) {}

  void initialize() {
    if (!clock_.isValid()) clock_.start();
    if (!timer_) {
      timer_ = std::make_unique<QTimer>();
      timer_->setSingleShot(true);
      timer_->setTimerType(Qt::PreciseTimer);
      connect(timer_.get(), &QTimer::timeout, this,
              &DirectTransmitWorker::handleTimer);
    }
    if (!adapter_) {
      adapter_ = backend_factory_
          ? std::make_unique<DirectKeyingAdapter>(backend_factory_())
          : std::make_unique<DirectKeyingAdapter>();
    }
    publish();
  }

  WorkerState configure(const DirectTransmitEngineConfig& config) {
    closeInternal(false);
    config_ = config;
    scheduler_ = cwassistant::core::CwTransmitScheduler(config.scheduler);
    configured_ = validKeyingConfig(config.keying);
    fault_ = !configured_;
    status_ = configured_
        ? QStringLiteral("Direct transmit adapter configured and closed")
        : QStringLiteral("Direct transmit configuration is invalid");
    publish();
    return state();
  }

  std::pair<bool, WorkerState> openSafe() {
    if (!adapter_) initialize();
    if (!configured_) {
      fault_ = true;
      status_ = QStringLiteral("Configure an explicit safe keying port first");
      publish();
      return {false, state()};
    }
    timer_->stop();
    drainSchedulerRelease();
    adapter_->close();
    const bool opened = adapter_->open(config_.keying);
    const auto& hardware = adapter_->snapshot();
    const bool known_inactive = opened && hardware.open_safe &&
        hardware.key == DirectKeyingLineState::Inactive &&
        hardware.ptt == DirectKeyingLineState::Inactive &&
        !hardware.fault_or_unknown;
    fault_ = !known_inactive;
    status_ = known_inactive
        ? QStringLiteral("Direct transmit adapter is open with KEY and PTT inactive")
        : hardware.fault.isEmpty()
            ? QStringLiteral("Direct transmit adapter did not reach known-inactive state")
            : hardware.fault;
    publish();
    return {known_inactive, state()};
  }

  std::pair<bool, WorkerState> start(
      const cwassistant::core::CwTransmitPlan& plan,
      const bool exact_preview_confirmed) {
    if (!exact_preview_confirmed) {
      status_ = QStringLiteral("Exact transmit preview confirmation is required");
      publish();
      return {false, state()};
    }
    if (!adapterKnownInactive()) {
      fault_ = true;
      status_ = QStringLiteral(
          "Direct transmit adapter must be open with KEY and PTT inactive");
      publish();
      return {false, state()};
    }
    if (scheduler_.snapshot().state !=
        cwassistant::core::CwTransmitSchedulerState::Idle) {
      if (!scheduler_.reset()) {
        faultEngine(QStringLiteral("Transmit scheduler is not safely reusable"));
        return {false, state()};
      }
    }
    const std::uint64_t now_ns = monotonicNow();
    if (fault_ || !scheduler_.start(plan, now_ns) ||
        !applyDesiredLines()) {
      if (!fault_) faultEngine(QStringLiteral("Cannot start the confirmed transmit plan"));
      return {false, state()};
    }
    status_ = QStringLiteral("Confirmed CW message is transmitting");
    scheduleTimer();
    publish();
    return {true, state()};
  }

  WorkerState emergencyRelease() {
    timer_->stop();
    scheduler_.emergencyRelease();
    // The scheduler deliberately exposes KEY-off and PTT-off as two distinct
    // desired states. Apply both synchronously in that order.
    static_cast<void>(applyDesiredLines());
    static_cast<void>(scheduler_.advance(monotonicNow()));
    static_cast<void>(applyDesiredLines());
    if (adapter_) static_cast<void>(adapter_->releaseAll());
    fault_ = true;
    status_ = QStringLiteral("Emergency release completed; transmit is faulted");
    publish();
    return state();
  }

  WorkerState closeBlocking() {
    closeInternal(false);
    publish();
    return state();
  }

 signals:
  void stateChanged(bool available, bool open_safe, bool busy, bool ptt,
                    bool key, bool fault, QString status);

 private:
  std::uint64_t monotonicNow() {
    if (!clock_.isValid()) clock_.start();
    const qint64 elapsed = clock_.nsecsElapsed();
    if (elapsed < 0) {
      faultEngine(QStringLiteral("Monotonic transmit clock failed"));
      return 0U;
    }
    return static_cast<std::uint64_t>(elapsed);
  }

  bool adapterKnownInactive() const noexcept {
    if (!adapter_) return false;
    const auto& hardware = adapter_->snapshot();
    return hardware.open_safe && !hardware.fault_or_unknown &&
        hardware.ptt == DirectKeyingLineState::Inactive &&
        hardware.key == DirectKeyingLineState::Inactive;
  }

  bool applyDesiredLines() {
    if (!adapter_) return false;
    const auto desired = scheduler_.snapshot().lines;
    auto hardware = adapter_->snapshot();
    if (hardware.fault_or_unknown || !hardware.open_safe) {
      faultEngine(hardware.fault.isEmpty()
                      ? QStringLiteral("Direct keying adapter state is unknown")
                      : hardware.fault);
      return false;
    }

    // Assertions always establish PTT before KEY. Releases always remove KEY
    // before PTT, even when one scheduler advance crosses a zero-length delay.
    if (desired.ptt && !lineActive(hardware.ptt)) {
      if (!adapter_->setPtt(true)) return adapterFailure();
      hardware = adapter_->snapshot();
    }
    if (!desired.key && lineActive(hardware.key)) {
      if (!adapter_->setKey(false)) return adapterFailure();
      hardware = adapter_->snapshot();
    }
    if (desired.key && !lineActive(hardware.key)) {
      if (!desired.ptt || !lineActive(hardware.ptt)) {
        faultEngine(QStringLiteral("Scheduler attempted KEY without active PTT"));
        return false;
      }
      if (!adapter_->setKey(true)) return adapterFailure();
      hardware = adapter_->snapshot();
    }
    if (!desired.ptt && lineActive(hardware.ptt)) {
      if (lineActive(hardware.key)) {
        faultEngine(QStringLiteral("Scheduler attempted PTT release before KEY"));
        return false;
      }
      if (!adapter_->setPtt(false)) return adapterFailure();
    }
    return true;
  }

  bool adapterFailure() {
    const QString error = adapter_ ? adapter_->snapshot().fault : QString{};
    faultEngine(error.isEmpty() ? QStringLiteral("Direct keying adapter failed")
                                : error);
    return false;
  }

  void faultEngine(QString error) {
    if (handling_fault_) return;
    handling_fault_ = true;
    timer_->stop();
    scheduler_.emergencyRelease();
    if (adapter_) {
      static_cast<void>(adapter_->releaseAll());
    }
    // Keep the scheduler's desired state aligned with the already released
    // hardware so a later close/configure can never reassert PTT transiently.
    if (scheduler_.snapshot().release_pending) {
      static_cast<void>(scheduler_.advance(monotonicNow()));
    }
    fault_ = true;
    status_ = error.isEmpty() ? QStringLiteral("Direct transmit engine fault")
                              : std::move(error);
    handling_fault_ = false;
    publish();
  }

  void drainSchedulerRelease() {
    const auto scheduler_state = scheduler_.snapshot().state;
    if (scheduler_state == cwassistant::core::CwTransmitSchedulerState::Running) {
      scheduler_.emergencyRelease();
      static_cast<void>(applyDesiredLines());
    }
    if (scheduler_.snapshot().release_pending) {
      static_cast<void>(scheduler_.advance(monotonicNow()));
      static_cast<void>(applyDesiredLines());
    }
  }

  void closeInternal(const bool preserve_fault) {
    if (timer_) timer_->stop();
    if (adapter_ && adapter_->snapshot().open_safe) drainSchedulerRelease();
    if (adapter_) adapter_->close();
    if (!preserve_fault) fault_ = false;
    status_ = QStringLiteral("Direct transmit adapter is closed");
  }

  void scheduleTimer() {
    if (scheduler_.snapshot().state !=
        cwassistant::core::CwTransmitSchedulerState::Running) {
      timer_->stop();
      return;
    }
    const std::uint64_t now_ns = monotonicNow();
    const std::uint64_t deadline_ns = scheduler_.snapshot().next_transition_ns;
    const std::uint64_t remaining_ns = deadline_ns > now_ns
        ? deadline_ns - now_ns : 0U;
    const std::uint64_t rounded_ms = (remaining_ns + 999'999U) / 1'000'000U;
    const auto delay_ms = static_cast<int>(std::min<std::uint64_t>(
        rounded_ms, static_cast<std::uint64_t>(kAdapterHealthPollMs)));
    timer_->start(delay_ms);
  }

  void handleTimer() {
    if (!adapter_ || adapter_->snapshot().fault_or_unknown) {
      adapterFailure();
      return;
    }
    const auto result = scheduler_.advance(monotonicNow());
    if (result == cwassistant::core::CwTransmitAdvanceResult::Faulted) {
      faultEngine(QStringLiteral("Transmit timer missed its safe deadline"));
      return;
    }
    if (result == cwassistant::core::CwTransmitAdvanceResult::Transition &&
        !applyDesiredLines()) {
      return;
    }
    if (scheduler_.snapshot().state ==
        cwassistant::core::CwTransmitSchedulerState::Completed) {
      status_ = QStringLiteral("Confirmed CW message completed");
    }
    publish();
    scheduleTimer();
  }

  WorkerState state() const {
    const auto hardware = adapter_ ? adapter_->snapshot()
                                   : DirectKeyingSnapshot{};
    return {.available = configured_ && adapter_ != nullptr,
            .open_safe = hardware.open_safe && !hardware.fault_or_unknown,
            .busy = scheduler_.snapshot().state ==
                    cwassistant::core::CwTransmitSchedulerState::Running,
            .ptt = lineActive(hardware.ptt),
            .key = lineActive(hardware.key),
            .fault = fault_ || !hardware.fault.isEmpty(),
            .status = status_};
  }

  void publish() {
    const WorkerState current = state();
    emit stateChanged(current.available, current.open_safe, current.busy,
                      current.ptt, current.key, current.fault, current.status);
  }

  DirectTransmitEngine::BackendFactory backend_factory_;
  std::unique_ptr<DirectKeyingAdapter> adapter_;
  DirectTransmitEngineConfig config_{};
  cwassistant::core::CwTransmitScheduler scheduler_{};
  QElapsedTimer clock_;
  std::unique_ptr<QTimer> timer_;
  QString status_{QStringLiteral("Direct transmit worker is initializing")};
  bool configured_{false};
  bool fault_{false};
  bool handling_fault_{false};
};

DirectTransmitEngine::DirectTransmitEngine(QObject* parent)
    : DirectTransmitEngine(BackendFactory{}, parent) {}

DirectTransmitEngine::DirectTransmitEngine(BackendFactory backend_factory,
                                           QObject* parent)
    : QObject(parent) {
  initializeWorker(std::move(backend_factory));
}

void DirectTransmitEngine::initializeWorker(BackendFactory backend_factory) {
  thread_ = new QThread(this);
  worker_ = new DirectTransmitWorker(std::move(backend_factory));
  worker_->moveToThread(thread_);
  connect(worker_, &DirectTransmitWorker::stateChanged, this,
          &DirectTransmitEngine::updateCachedState, Qt::QueuedConnection);
  connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
  thread_->start();
  QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->initialize(); },
                            Qt::BlockingQueuedConnection);
}

DirectTransmitEngine::~DirectTransmitEngine() {
  close();
  if (thread_) {
    thread_->quit();
    thread_->wait();
  }
  worker_ = nullptr;
}

void DirectTransmitEngine::configure(const DirectTransmitEngineConfig& config) {
  if (!worker_ || !thread_ || !thread_->isRunning()) return;
  WorkerState result;
  QMetaObject::invokeMethod(
      worker_, [this, config, &result] { result = worker_->configure(config); },
      Qt::BlockingQueuedConnection);
  updateCachedState(result.available, result.open_safe, result.busy, result.ptt,
                    result.key, result.fault, std::move(result.status));
}

bool DirectTransmitEngine::openSafe() {
  if (!worker_ || !thread_ || !thread_->isRunning()) return false;
  std::pair<bool, WorkerState> result;
  QMetaObject::invokeMethod(
      worker_, [this, &result] { result = worker_->openSafe(); },
      Qt::BlockingQueuedConnection);
  updateCachedState(result.second.available, result.second.open_safe,
                    result.second.busy, result.second.ptt, result.second.key,
                    result.second.fault, std::move(result.second.status));
  return result.first;
}

bool DirectTransmitEngine::start(
    const cwassistant::core::CwTransmitPlan& plan,
    const bool exact_preview_confirmed) {
  if (!worker_ || !thread_ || !thread_->isRunning()) return false;
  std::pair<bool, WorkerState> result;
  QMetaObject::invokeMethod(
      worker_,
      [this, plan, exact_preview_confirmed, &result] {
        result = worker_->start(plan, exact_preview_confirmed);
      },
      Qt::BlockingQueuedConnection);
  updateCachedState(result.second.available, result.second.open_safe,
                    result.second.busy, result.second.ptt, result.second.key,
                    result.second.fault, std::move(result.second.status));
  return result.first;
}

void DirectTransmitEngine::emergencyRelease() {
  if (!worker_ || !thread_ || !thread_->isRunning()) return;
  WorkerState result;
  QMetaObject::invokeMethod(
      worker_, [this, &result] { result = worker_->emergencyRelease(); },
      Qt::BlockingQueuedConnection);
  updateCachedState(result.available, result.open_safe, result.busy, result.ptt,
                    result.key, result.fault, std::move(result.status));
}

void DirectTransmitEngine::close() {
  if (!worker_ || !thread_ || !thread_->isRunning()) return;
  WorkerState result;
  QMetaObject::invokeMethod(
      worker_, [this, &result] { result = worker_->closeBlocking(); },
      Qt::BlockingQueuedConnection);
  updateCachedState(result.available, result.open_safe, result.busy, result.ptt,
                    result.key, result.fault, std::move(result.status));
}

void DirectTransmitEngine::updateCachedState(
    const bool available, const bool open_safe, const bool busy, const bool ptt,
    const bool key, const bool fault, QString status) {
  const bool changed_state = available_ != available || open_safe_ != open_safe ||
      busy_ != busy || ptt_ != ptt || key_ != key || fault_ != fault ||
      status_ != status;
  available_ = available;
  open_safe_ = open_safe;
  busy_ = busy;
  ptt_ = ptt;
  key_ = key;
  fault_ = fault;
  status_ = std::move(status);
  if (changed_state) emit changed();
}

}  // namespace cwassistant::desktop

#include "direct_transmit_engine.moc"
