#include "replay_controller.hpp"

#include <QFileInfo>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPermissions>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/wav_replay_source.hpp"
#include "live_audio_worker.hpp"

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

  void configure(const int averaging_frames, const int frame_rate_hz,
                 const bool dc_rejection,
                 const bool automatic_gain, const double gain_db,
                 const double automatic_gain_target_dbfs,
                 const bool automatic_bandwidth,
                 const double lower_frequency_hz,
                 const double upper_frequency_hz) {
    auto config = analyzer_.config();
    config.averaging_frames = static_cast<std::uint8_t>(
        std::clamp(averaging_frames, 1, 32));
    config.frame_rate_hz = static_cast<std::uint16_t>(
        std::clamp(frame_rate_hz, 1, 120));
    config.audio_dc_rejection = dc_rejection;
    config.audio_automatic_gain = automatic_gain;
    config.audio_gain_db =
        static_cast<float>(std::clamp(gain_db, -40.0, 40.0));
    config.audio_automatic_gain_target_dbfs = static_cast<float>(
        std::clamp(automatic_gain_target_dbfs, -40.0, -1.0));
    config.audio_automatic_bandwidth = automatic_bandwidth;
    config.audio_lower_frequency_hz =
        std::clamp(lower_frequency_hz, 0.0, 95'999.0);
    config.audio_upper_frequency_hz =
        std::clamp(upper_frequency_hz,
                   config.audio_lower_frequency_hz + 1.0, 96'000.0);
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
  connect(this, &ReplayController::frameReady, this,
          &ReplayController::processDecoderFrame);
  connect(this, &ReplayController::sourceReset, this,
          &ReplayController::resetDecoder);
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

  auto pipe = std::make_shared<LiveAudioPipe>();
  auto* capture_worker = new LiveAudioCaptureWorker(pipe);
  auto* dsp_worker = new LiveAudioDspWorker(std::move(pipe));
  audio_capture_worker_ = capture_worker;
  audio_dsp_worker_ = dsp_worker;
  capture_worker->moveToThread(&audio_capture_thread_);
  dsp_worker->moveToThread(&audio_dsp_thread_);
  connect(&audio_capture_thread_, &QThread::finished, capture_worker,
          &QObject::deleteLater);
  connect(&audio_dsp_thread_, &QThread::finished, dsp_worker,
          &QObject::deleteLater);
  connect(this, &ReplayController::liveStartRequested, capture_worker,
          &LiveAudioCaptureWorker::start);
  connect(this, &ReplayController::liveStopRequested, capture_worker,
          &LiveAudioCaptureWorker::stop);
  connect(this, &ReplayController::liveDspStartRequested, dsp_worker,
          &LiveAudioDspWorker::start);
  connect(this, &ReplayController::liveDspStopRequested, dsp_worker,
          &LiveAudioDspWorker::stop);
  connect(this, &ReplayController::liveDspConfigureRequested, dsp_worker,
          &LiveAudioDspWorker::configure);
  connect(capture_worker, &LiveAudioCaptureWorker::started, this,
          [this](const QString& name, const double sample_rate,
                 const int channel_count) {
            source_name_ = name;
            sample_rate_ = sample_rate;
            duration_seconds_ = 0.0;
            position_seconds_ = 0.0;
            input_overruns_ = 0;
            live_capturing_ = true;
            status_text_ =
                QStringLiteral("Live RX: %1 • %2 Hz • %3 channel(s)")
                    .arg(name)
                    .arg(sample_rate, 0, 'f', 0)
                    .arg(channel_count);
            emit stateChanged();
          });
  connect(capture_worker, &LiveAudioCaptureWorker::stopped, this, [this] {
    if (live_capturing_) {
      live_capturing_ = false;
      status_text_ = QStringLiteral("Live audio stopped");
      emit stateChanged();
    }
  });
  connect(capture_worker, &LiveAudioCaptureWorker::failed, this,
          [this](const QString& message) {
            live_capturing_ = false;
            emit liveDspStopRequested();
            setStatus(QStringLiteral("Live audio error: %1").arg(message));
          });
  connect(capture_worker, &LiveAudioCaptureWorker::overrunCountChanged, this,
          [this](const qulonglong count) {
            input_overruns_ = count;
            emit stateChanged();
          });
  connect(dsp_worker, &LiveAudioDspWorker::frameProduced, this,
          &ReplayController::frameReady);
  audio_capture_thread_.setObjectName(QStringLiteral("Live audio capture"));
  audio_dsp_thread_.setObjectName(QStringLiteral("Live audio DSP"));
  audio_capture_thread_.start();
  audio_dsp_thread_.start();
}

ReplayController::~ReplayController() {
  if (audio_capture_worker_ != nullptr && audio_capture_thread_.isRunning()) {
    QMetaObject::invokeMethod(audio_capture_worker_, "stop",
                              Qt::BlockingQueuedConnection);
  }
  if (audio_dsp_worker_ != nullptr && audio_dsp_thread_.isRunning()) {
    QMetaObject::invokeMethod(audio_dsp_worker_, "stop",
                              Qt::BlockingQueuedConnection);
  }
  audio_capture_thread_.quit();
  audio_dsp_thread_.quit();
  audio_capture_thread_.wait();
  audio_dsp_thread_.wait();
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
int ReplayController::sourceMode() const noexcept { return source_mode_; }
bool ReplayController::liveCapturing() const noexcept {
  return live_capturing_;
}
bool ReplayController::activeSource() const noexcept {
  return source_mode_ == 0 ? live_capturing_ : source_loaded_;
}
qulonglong ReplayController::inputOverruns() const noexcept {
  return input_overruns_;
}
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
const QString& ReplayController::decodedText() const noexcept {
  return decoded_text_;
}
double ReplayController::decoderWpm() const noexcept { return decoder_wpm_; }
double ReplayController::decoderSnrDb() const noexcept { return decoder_snr_db_; }
double ReplayController::decoderToneHz() const noexcept { return decoder_tone_hz_; }
double ReplayController::decoderConfidence() const noexcept {
  return decoder_confidence_;
}
bool ReplayController::decoderKeyDown() const noexcept {
  return decoder_key_down_;
}

void ReplayController::setCwDecoderSlice(const double center_hz,
                                         const double width_hz) {
  const double center = std::clamp(center_hz, 0.0, 96'000.0);
  const double width = std::clamp(width_hz, 10.0, 5'000.0);
  if (decoder_center_hz_ == center && decoder_width_hz_ == width) return;
  decoder_center_hz_ = center;
  decoder_width_hz_ = width;
  resetDecoder();
}

void ReplayController::setAveragingFrames(const int value) {
  const int clamped = std::clamp(value, 1, 32);
  if (averaging_frames_ == clamped) {
    return;
  }
  averaging_frames_ = clamped;
  emit averagingFramesChanged();
  publishSpectrumConfiguration();
}

void ReplayController::setSpectrumProcessing(
    const bool dc_rejection, const bool automatic_gain, const double gain_db,
    const double automatic_gain_target_dbfs, const bool automatic_bandwidth,
    const double lower_frequency_hz, const double upper_frequency_hz,
    const int frame_rate_hz) {
  const double clamped_gain_db = std::clamp(gain_db, -40.0, 40.0);
  const double clamped_target_dbfs =
      std::clamp(automatic_gain_target_dbfs, -40.0, -1.0);
  const double clamped_lower_hz =
      std::clamp(lower_frequency_hz, 0.0, 95'999.0);
  const double clamped_upper_hz =
      std::clamp(upper_frequency_hz, clamped_lower_hz + 1.0, 96'000.0);
  const int clamped_frame_rate_hz = std::clamp(frame_rate_hz, 1, 120);
  if (spectrum_processing_configured_ &&
      audio_dc_rejection_ == dc_rejection &&
      audio_automatic_gain_ == automatic_gain &&
      audio_gain_db_ == clamped_gain_db &&
      audio_automatic_gain_target_dbfs_ == clamped_target_dbfs &&
      audio_automatic_bandwidth_ == automatic_bandwidth &&
      audio_lower_frequency_hz_ == clamped_lower_hz &&
      audio_upper_frequency_hz_ == clamped_upper_hz &&
      spectrum_frame_rate_hz_ == clamped_frame_rate_hz) {
    return;
  }
  audio_dc_rejection_ = dc_rejection;
  audio_automatic_gain_ = automatic_gain;
  audio_gain_db_ = clamped_gain_db;
  audio_automatic_gain_target_dbfs_ = clamped_target_dbfs;
  audio_automatic_bandwidth_ = automatic_bandwidth;
  audio_lower_frequency_hz_ = clamped_lower_hz;
  audio_upper_frequency_hz_ = clamped_upper_hz;
  spectrum_frame_rate_hz_ = clamped_frame_rate_hz;
  spectrum_processing_configured_ = true;
  publishSpectrumConfiguration();
}

void ReplayController::publishSpectrumConfiguration() {
  emit configureRequested(
      averaging_frames_, spectrum_frame_rate_hz_, audio_dc_rejection_,
      audio_automatic_gain_,
      audio_gain_db_, audio_automatic_gain_target_dbfs_,
      audio_automatic_bandwidth_, audio_lower_frequency_hz_,
      audio_upper_frequency_hz_);
  emit liveDspConfigureRequested(
      averaging_frames_, spectrum_frame_rate_hz_, audio_dc_rejection_,
      audio_automatic_gain_,
      audio_gain_db_, audio_automatic_gain_target_dbfs_,
      audio_automatic_bandwidth_, audio_lower_frequency_hz_,
      audio_upper_frequency_hz_);
}

void ReplayController::processDecoderFrame(const SpectrumFrame& frame) {
  if (frame.bins_dbfs.isEmpty() ||
      frame.upper_frequency_hz <= frame.lower_frequency_hz) return;
  const double span = frame.upper_frequency_hz - frame.lower_frequency_hz;
  const double low = decoder_center_hz_ - decoder_width_hz_ * 0.5;
  const double high = decoder_center_hz_ + decoder_width_hz_ * 0.5;
  float peak = -200.0F;
  qsizetype peak_index = -1;
  QVector<float> noise;
  noise.reserve(frame.bins_dbfs.size());
  for (qsizetype i = 0; i < frame.bins_dbfs.size(); ++i) {
    const double frequency = frame.lower_frequency_hz +
        span * static_cast<double>(i) /
        static_cast<double>(std::max<qsizetype>(1, frame.bins_dbfs.size() - 1));
    const float level = frame.bins_dbfs[i];
    if (frequency >= low && frequency <= high && level > peak) {
      peak = level;
      peak_index = i;
    } else if (std::isfinite(level)) {
      noise.push_back(level);
    }
  }
  if (peak_index < 0 || noise.isEmpty()) return;
  const qsizetype noise_index = (noise.size() * 3) / 5;
  std::nth_element(noise.begin(), noise.begin() + noise_index, noise.end());
  decoder_snr_db_ = static_cast<double>(peak - noise[noise_index]);
  decoder_tone_hz_ = frame.lower_frequency_hz +
      span * static_cast<double>(peak_index) /
      static_cast<double>(std::max<qsizetype>(1, frame.bins_dbfs.size() - 1));
  const auto result = cw_decoder_.process(
      frame.timestamp_ns, static_cast<float>(decoder_snr_db_));
  decoded_text_ = QString::fromStdString(result.text);
  decoder_wpm_ = result.wpm;
  decoder_confidence_ = result.confidence;
  decoder_key_down_ = result.key_down;
  emit decoderChanged();
}

void ReplayController::resetDecoder() {
  cw_decoder_.reset();
  decoded_text_.clear();
  decoder_wpm_ = 20.0;
  decoder_snr_db_ = 0.0;
  decoder_tone_hz_ = decoder_center_hz_;
  decoder_confidence_ = 0.0;
  decoder_key_down_ = false;
  emit decoderChanged();
}

void ReplayController::setSourceMode(const int value) {
  const int clamped = std::clamp(value, 0, 1);
  if (source_mode_ == clamped) {
    return;
  }
  if (clamped == 0) {
    emit stopRequested();
    playing_ = false;
  } else {
    stopLiveAudio();
  }
  source_mode_ = clamped;
  emit sourceReset();
  emit stateChanged();
}

void ReplayController::setAudioInputSelection(QString encoded_id,
                                               QString display_name) {
  const bool changed = audio_input_id_ != encoded_id;
  audio_input_id_ = std::move(encoded_id);
  audio_input_name_ = std::move(display_name);
  if (changed && live_capturing_) {
    beginLiveAudioCapture();
  }
}

void ReplayController::openFile(const QUrl& url) {
  setSourceMode(1);
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

void ReplayController::play() {
  setSourceMode(1);
  emit playRequested();
}
void ReplayController::pause() { emit pauseRequested(); }
void ReplayController::stop() {
  emit stopRequested();
  emit sourceReset();
}

void ReplayController::startLiveAudio() {
  setSourceMode(0);
  QMicrophonePermission permission;
  auto* application = QCoreApplication::instance();
  switch (application->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
      setStatus(QStringLiteral("Waiting for microphone/audio-input permission…"));
      application->requestPermission(
          permission, this,
          [this](const QPermission&) { startLiveAudio(); });
      return;
    case Qt::PermissionStatus::Denied:
      setStatus(QStringLiteral(
          "Audio-input permission was denied. Enable microphone access in the operating-system privacy settings."));
      return;
    case Qt::PermissionStatus::Granted:
      beginLiveAudioCapture();
      return;
  }
}

void ReplayController::beginLiveAudioCapture() {
  emit stopRequested();
  playing_ = false;
  source_loaded_ = false;
  input_overruns_ = 0;
  emit sourceReset();
  setStatus(QStringLiteral("Starting live audio from %1…").arg(audio_input_name_));
  publishSpectrumConfiguration();
  emit liveDspStartRequested();
  emit liveStartRequested(audio_input_id_);
}

void ReplayController::stopLiveAudio() {
  emit liveStopRequested();
  emit liveDspStopRequested();
  if (live_capturing_) {
    live_capturing_ = false;
    setStatus(QStringLiteral("Live audio stopped"));
    emit sourceReset();
  }
}

void ReplayController::setStatus(QString status) {
  status_text_ = std::move(status);
  emit stateChanged();
}

}  // namespace cwassistant::desktop

#include "replay_controller.moc"
