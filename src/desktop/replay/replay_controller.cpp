#include "replay_controller.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/wav_replay_source.hpp"

namespace cwassistant::desktop {

class ReplayWorker final : public QObject {
  Q_OBJECT

 public:
  explicit ReplayWorker(QObject* parent = nullptr)
      : QObject(parent), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &ReplayWorker::readNextBlock);
  }

 public slots:
  void openPath(const QString& path) {
    timer_->stop();
    source_.stop();
    analyzer_.reset();
    started_ = false;
    opened_ = source_.open(path.toStdString(), {});
    if (!opened_) {
      emit failed(QString::fromStdString(source_.last_error()));
      return;
    }
    emit opened(QFileInfo(path).fileName(),
                source_.stream_descriptor().sample_rate_hz,
                source_.duration_seconds());
  }

  void play() {
    if (!opened_) {
      emit failed(QStringLiteral("Choose a valid WAV recording first."));
      return;
    }
    if (!started_) {
      if (!source_.start()) {
        emit failed(QString::fromStdString(source_.last_error()));
        return;
      }
      analyzer_.reset();
      started_ = true;
      emit progress(0.0);
    }
    if (!timer_->isActive()) {
      emit playbackChanged(true);
      timer_->start(0);
    }
  }

  void pause() {
    timer_->stop();
    emit playbackChanged(false);
  }

  void stop() {
    timer_->stop();
    source_.stop();
    analyzer_.reset();
    started_ = false;
    emit playbackChanged(false);
    emit progress(0.0);
  }

  void shutdown() {
    timer_->stop();
    source_.stop();
  }

  void configure(const int averaging_frames) {
    auto config = analyzer_.config();
    config.averaging_frames = static_cast<std::uint8_t>(
        std::clamp(averaging_frames, 1, 32));
    static_cast<void>(analyzer_.configure(config));
  }

 signals:
  void opened(const QString& name, double sample_rate, double duration);
  void failed(const QString& message);
  void playbackChanged(bool playing);
  void progress(double seconds);
  void frameProduced(const cwassistant::desktop::SpectrumFrame& frame);
  void ended();

 private slots:
  void readNextBlock() {
    using namespace std::chrono_literals;
    cwassistant::core::RealtimeSampleBlock block;
    if (!source_.read(block, 0ms)) {
      started_ = false;
      emit playbackChanged(false);
      emit progress(source_.duration_seconds());
      emit ended();
      return;
    }

    for (auto& snapshot : analyzer_.process(block)) {
      QVector<float> bins(static_cast<qsizetype>(snapshot.bins_dbfs.size()));
      std::copy(snapshot.bins_dbfs.cbegin(), snapshot.bins_dbfs.cend(),
                bins.begin());
      SpectrumFrame frame{
          .bins_dbfs = std::move(bins),
          .sequence = snapshot.sequence,
          .timestamp_ns = snapshot.timestamp_ns,
          .lower_frequency_hz = snapshot.lower_frequency_hz,
          .upper_frequency_hz = snapshot.upper_frequency_hz,
      };
      emit frameProduced(frame);
    }

    const double sample_rate = source_.stream_descriptor().sample_rate_hz;
    emit progress(static_cast<double>(source_.position_frames()) / sample_rate);
    const int block_duration_ms = std::max(
        1, static_cast<int>(std::lround(
               static_cast<double>(block.sample_count) * 1'000.0 / sample_rate)));
    timer_->start(block_duration_ms);
  }

 private:
  QTimer* timer_;
  cwassistant::core::WavReplaySource source_;
  cwassistant::core::SpectrumAnalyzer analyzer_;
  bool opened_{false};
  bool started_{false};
};

ReplayController::ReplayController(QObject* parent) : QObject(parent) {
  qRegisterMetaType<SpectrumFrame>();
  auto* worker = new ReplayWorker;
  worker_ = worker;
  worker->moveToThread(&worker_thread_);
  connect(&worker_thread_, &QThread::finished, worker, &QObject::deleteLater);
  connect(this, &ReplayController::openRequested, worker,
          &ReplayWorker::openPath);
  connect(this, &ReplayController::playRequested, worker, &ReplayWorker::play);
  connect(this, &ReplayController::pauseRequested, worker,
          &ReplayWorker::pause);
  connect(this, &ReplayController::stopRequested, worker, &ReplayWorker::stop);
  connect(this, &ReplayController::configureRequested, worker,
          &ReplayWorker::configure);
  connect(worker, &ReplayWorker::opened, this,
          [this](const QString& name, const double sample_rate,
                 const double duration) {
            source_name_ = name;
            sample_rate_ = sample_rate;
            duration_seconds_ = duration;
            position_seconds_ = 0.0;
            source_loaded_ = true;
            playing_ = false;
            status_text_ = QStringLiteral("Ready: %1 • %2 Hz • %3 s")
                               .arg(name)
                               .arg(sample_rate, 0, 'f', 0)
                               .arg(duration, 0, 'f', 2);
            emit sourceReset();
            emit stateChanged();
          });
  connect(worker, &ReplayWorker::failed, this, [this](const QString& message) {
    source_loaded_ = false;
    playing_ = false;
    setStatus(QStringLiteral("WAV replay error: %1").arg(message));
  });
  connect(worker, &ReplayWorker::playbackChanged, this,
          [this](const bool playing) {
            if (playing_ != playing) {
              playing_ = playing;
              emit stateChanged();
            }
          });
  connect(worker, &ReplayWorker::progress, this, [this](const double seconds) {
    position_seconds_ = seconds;
    emit stateChanged();
  });
  connect(worker, &ReplayWorker::frameProduced, this,
          &ReplayController::frameReady);
  connect(worker, &ReplayWorker::ended, this, [this] {
    playing_ = false;
    status_text_ = QStringLiteral("Replay complete");
    emit stateChanged();
  });
  worker_thread_.setObjectName(QStringLiteral("WAV replay DSP"));
  worker_thread_.start();
}

ReplayController::~ReplayController() {
  if (worker_ != nullptr && worker_thread_.isRunning()) {
    QMetaObject::invokeMethod(worker_, "shutdown", Qt::BlockingQueuedConnection);
  }
  worker_thread_.quit();
  worker_thread_.wait();
}

const QString& ReplayController::sourceName() const noexcept {
  return source_name_;
}
const QString& ReplayController::statusText() const noexcept {
  return status_text_;
}
bool ReplayController::sourceLoaded() const noexcept { return source_loaded_; }
bool ReplayController::playing() const noexcept { return playing_; }
double ReplayController::sampleRate() const noexcept { return sample_rate_; }
double ReplayController::durationSeconds() const noexcept {
  return duration_seconds_;
}
double ReplayController::positionSeconds() const noexcept {
  return position_seconds_;
}
int ReplayController::averagingFrames() const noexcept {
  return averaging_frames_;
}

void ReplayController::setAveragingFrames(const int value) {
  const int clamped = std::clamp(value, 1, 32);
  if (averaging_frames_ == clamped) {
    return;
  }
  averaging_frames_ = clamped;
  emit averagingFramesChanged();
  emit configureRequested(averaging_frames_);
}

void ReplayController::openFile(const QUrl& url) {
  const QString path = url.isLocalFile() ? url.toLocalFile() : QString{};
  if (path.isEmpty()) {
    setStatus(QStringLiteral("Select a local WAV file."));
    return;
  }
  source_loaded_ = false;
  playing_ = false;
  source_name_.clear();
  position_seconds_ = 0.0;
  setStatus(QStringLiteral("Opening WAV recording…"));
  emit sourceReset();
  emit openRequested(path);
}

void ReplayController::play() { emit playRequested(); }
void ReplayController::pause() { emit pauseRequested(); }
void ReplayController::stop() {
  emit stopRequested();
  emit sourceReset();
}

void ReplayController::setStatus(QString status) {
  status_text_ = std::move(status);
  emit stateChanged();
}

}  // namespace cwassistant::desktop

#include "replay_controller.moc"
