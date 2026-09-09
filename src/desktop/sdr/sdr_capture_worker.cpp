#include "sdr_capture_worker.hpp"

#include <utility>

namespace cwassistant::desktop {

SdrCaptureWorker::SdrCaptureWorker(std::shared_ptr<LiveAudioPipe> pipe,
                                   QObject* parent)
    : SdrCaptureWorker(std::move(pipe), makeSoapySdrReceiveBackend(), parent) {}

SdrCaptureWorker::SdrCaptureWorker(
    std::shared_ptr<LiveAudioPipe> pipe,
    std::unique_ptr<SdrReceiveBackend> backend, QObject* parent)
    : QObject(parent),
      pipe_(std::move(pipe)),
      receiver_(std::move(backend)),
      pump_timer_(this) {
  pump_timer_.setInterval(0);
  connect(&pump_timer_, &QTimer::timeout, this, &SdrCaptureWorker::pump);
}

SdrCaptureWorker::~SdrCaptureWorker() { stop(); }

void SdrCaptureWorker::start(const QString& device_id,
                             const double center_frequency_hz,
                             const double sample_rate_hz,
                             const double bandwidth_hz,
                             const QString& antenna,
                             const bool automatic_gain, const double gain_db) {
  stop();
  if (!pipe_) {
    emit failed(QStringLiteral("The SDR sample pipeline is unavailable."));
    return;
  }
  pipe_->overruns.store(0, std::memory_order_release);
  cwassistant::core::RealtimeSampleBlock stale;
  while (pipe_->blocks.try_pop(stale)) {
  }
  reported_device_overflows_ = 0;
  reported_source_overruns_ = 0;
  reported_read_timeouts_ = 0;
  reported_read_errors_ = 0;

  std::string error;
  if (!receiver_.start({.device_id = device_id.toStdString(),
                        .center_frequency_hz = center_frequency_hz,
                        .sample_rate_hz = sample_rate_hz,
                        .bandwidth_hz = bandwidth_hz,
                        .antenna = antenna.toStdString(),
                        .automatic_gain = automatic_gain,
                        .gain_db = gain_db},
                       error)) {
    emit failed(QString::fromStdString(error));
    return;
  }
  running_ = true;
  const auto& actual = receiver_.actualConfiguration();
  emit started(device_id, actual.center_frequency_hz, actual.sample_rate_hz,
               actual.bandwidth_hz, actual.automatic_gain, actual.gain_db);
  pump_timer_.start();
}

void SdrCaptureWorker::stop() {
  pump_timer_.stop();
  receiver_.stop();
  if (running_) {
    running_ = false;
    emit stopped();
  }
}

void SdrCaptureWorker::pump() {
  if (!running_ || !pipe_) return;
  cwassistant::core::RealtimeSampleBlock block;
  const bool have_block = receiver_.pump(block, 20'000);
  const auto& diagnostics = receiver_.diagnostics();
  if (have_block && !pipe_->blocks.try_push(block)) {
    pipe_->overruns.fetch_add(1, std::memory_order_acq_rel);
  }

  const auto source_overruns = pipe_->overruns.load(std::memory_order_acquire);
  if (diagnostics.overflows != reported_device_overflows_ ||
      diagnostics.timeouts != reported_read_timeouts_ ||
      diagnostics.read_errors != reported_read_errors_ ||
      source_overruns != reported_source_overruns_) {
    reported_source_overruns_ = source_overruns;
    reported_device_overflows_ = diagnostics.overflows;
    reported_read_timeouts_ = diagnostics.timeouts;
    reported_read_errors_ = diagnostics.read_errors;
    emit diagnosticsChanged(source_overruns, diagnostics.overflows,
                            diagnostics.timeouts, diagnostics.read_errors);
  }
  if (!diagnostics.last_error.empty()) {
    const QString error = QString::fromStdString(diagnostics.last_error);
    pump_timer_.stop();
    receiver_.stop();
    running_ = false;
    emit failed(error);
  }
}

}  // namespace cwassistant::desktop
