#include "replay_controller.hpp"

#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QMetaObject>
#include <QPermissions>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <utility>

#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/frequency_plan.hpp"
#include "cwassistant/core/wav_replay_source.hpp"
#include "decoder_channel_model.hpp"
#include "../decoder/local_character_decoder.hpp"
#include "live_audio_worker.hpp"

namespace cwassistant::desktop {

QList<qulonglong> reconcileDecoderSessionOrder(
    const QList<qulonglong>& requested_order,
    const QVariantList& previous_sessions,
    const QVariantList& current_channels) {
  const auto identity_frequency = [](const QVariantMap& item) {
    const QVariant presentation =
        item.value(QStringLiteral("presentationFrequencyHz"));
    return presentation.isValid()
        ? presentation.toDouble()
        : item.value(QStringLiteral("audioFrequencyHz")).toDouble();
  };
  QHash<qulonglong, QVariantMap> current_by_id;
  QHash<qulonglong, QVariantMap> previous_by_id;
  for (const QVariant& value : current_channels) {
    const QVariantMap channel = value.toMap();
    current_by_id.insert(
        channel.value(QStringLiteral("id")).toULongLong(), channel);
  }
  for (const QVariant& value : previous_sessions) {
    const QVariantMap session = value.toMap();
    previous_by_id.insert(
        session.value(QStringLiteral("id")).toULongLong(), session);
  }

  QList<qulonglong> reconciled;
  reconciled.reserve(requested_order.size());
  for (const qulonglong requested_id : requested_order) {
    if (current_by_id.contains(requested_id)) {
      if (!reconciled.contains(requested_id)) reconciled.push_back(requested_id);
      continue;
    }
    const QVariantMap previous = previous_by_id.value(requested_id);
    if (previous.isEmpty()) continue;
    const QString previous_color = previous.value(QStringLiteral("color")).toString();
    const double previous_frequency = identity_frequency(previous);
    qulonglong replacement_id = 0;
    double nearest_distance = 35.0;
    for (auto current = current_by_id.cbegin();
         current != current_by_id.cend(); ++current) {
      const QVariantMap& channel = current.value();
      if (channel.value(QStringLiteral("color")).toString() != previous_color)
        continue;
      const double distance = std::abs(
          identity_frequency(channel) - previous_frequency);
      if (distance <= nearest_distance && !reconciled.contains(current.key())) {
        nearest_distance = distance;
        replacement_id = current.key();
      }
    }
    if (replacement_id != 0) reconciled.push_back(replacement_id);
  }
  return reconciled;
}

std::optional<std::string> freshCharacterRefinementCallEvidence(
    const std::string_view stable_text,
    const std::size_t previous_stable_size) {
  if (stable_text.size() <= previous_stable_size) return std::nullopt;

  std::optional<std::string> fresh_call;
  std::size_t token_start = 0U;
  while (token_start < stable_text.size()) {
    while (token_start < stable_text.size()) {
      const auto character =
          static_cast<unsigned char>(stable_text[token_start]);
      if (std::isalnum(character) != 0 || character == '/') break;
      ++token_start;
    }
    if (token_start == stable_text.size()) break;
    std::size_t token_end = token_start;
    while (token_end < stable_text.size()) {
      const auto character = static_cast<unsigned char>(stable_text[token_end]);
      if (std::isalnum(character) == 0 && character != '/') break;
      ++token_end;
    }

    const std::string_view token =
        stable_text.substr(token_start, token_end - token_start);
    const auto candidate =
        cwassistant::core::CallsignPolicy::latest_in_text(token);
    if (candidate && token_end > previous_stable_size) {
      const std::size_t previous_token_length = previous_stable_size > token_start
          ? std::min(previous_stable_size, token_end) - token_start
          : 0U;
      const bool was_already_a_complete_call = previous_token_length > 0U &&
          cwassistant::core::CallsignPolicy::latest_in_text(
              token.substr(0U, previous_token_length)).has_value();
      if (!was_already_a_complete_call) fresh_call = std::string(token);
    }
    token_start = token_end;
  }
  return fresh_call;
}

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
    decoder_.reset();
    character_frontends_.reset();
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
      decoder_.reset();
      character_frontends_.reset();
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
    decoder_.reset();
    character_frontends_.reset();
    started_ = false;
    emit playbackChanged(false);
    emit progress(0.0);
    emit diagnosticsProduced(
        verificationDiagnosticsModel(decoder_.verificationDiagnostics()));
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
    decoder_.reset();
    character_frontends_.reset();
  }

  void setDecodedSignalTimeoutSeconds(const int seconds) {
    decoder_.configure({.decoded_track_retention_seconds =
                            static_cast<double>(std::clamp(seconds, 5, 120))});
  }

  void setLocalCharacterFrontendEnabled(const bool enabled) {
    character_frontends_.setEnabled(enabled);
  }

  void acceptCharacterRefinement(const qulonglong channel_id,
                                 const QString& stable_text,
                                 const qulonglong evidence_timestamp_ns) {
    if (decoder_.acceptCharacterRefinement(
            static_cast<std::uint64_t>(channel_id), stable_text.toStdString(),
            static_cast<std::uint64_t>(evidence_timestamp_ns))) {
      emit decoderProduced(decoderChannelModel(decoder_.channels()));
    }
  }

  void selectDecoderFrequency(const double audio_frequency_hz) {
    const std::uint64_t channel_id =
        decoder_.selectFrequency(audio_frequency_hz);
    if (channel_id == 0) return;
    emit decoderProduced(decoderChannelModel(decoder_.channels()));
    emit manualDecoderSelected(static_cast<qulonglong>(channel_id));
  }

 signals:
  void opened(const QString& name, double sample_rate, double duration);
  void failed(const QString& message);
  void playbackChanged(bool playing);
  void progress(double seconds);
  void frameProduced(const cwassistant::desktop::SpectrumFrame& frame);
  void decoderProduced(const QVariantList& channels);
  void diagnosticsProduced(const QVariantMap& diagnostics);
  void manualDecoderSelected(qulonglong channel_id);
  void characterWindowProduced(
      int source_mode,
      cwassistant::desktop::CwCharacterFeatureWindowPtr window);
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

    auto snapshots = analyzer_.process(block);
    for (const auto& snapshot : snapshots) {
      static_cast<void>(decoder_.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.bins_dbfs));
    }
    const auto& decoder_channels = decoder_.processSamples(block);
    const auto character_tracks = decoder_.allTrackDiagnostics();
    for (auto& window : character_frontends_.process(block, character_tracks))
      emit characterWindowProduced(1, std::move(window));
    for (auto& snapshot : snapshots) {
      QVector<float> bins(static_cast<qsizetype>(snapshot.bins_dbfs.size()));
      std::copy(snapshot.bins_dbfs.cbegin(), snapshot.bins_dbfs.cend(),
                bins.begin());
      QVector<float> instantaneous_bins(
          static_cast<qsizetype>(snapshot.instantaneous_bins_dbfs.size()));
      std::copy(snapshot.instantaneous_bins_dbfs.cbegin(),
                snapshot.instantaneous_bins_dbfs.cend(),
                instantaneous_bins.begin());
      SpectrumFrame frame{
          .bins_dbfs = std::move(bins),
          .sequence = snapshot.sequence,
          .timestamp_ns = snapshot.timestamp_ns,
          .lower_frequency_hz = snapshot.lower_frequency_hz,
          .upper_frequency_hz = snapshot.upper_frequency_hz,
          .instantaneous_bins_dbfs = std::move(instantaneous_bins),
      };
      emit frameProduced(frame);
    }
    emit decoderProduced(decoderChannelModel(decoder_channels));
    emit diagnosticsProduced(
        verificationDiagnosticsModel(decoder_.verificationDiagnostics()));

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
  cwassistant::core::CwChannelBank decoder_;
  LocalCharacterFrontendBank character_frontends_;
  bool opened_{false};
  bool started_{false};
};

ReplayController::ReplayController(QObject* parent) : QObject(parent) {
  qRegisterMetaType<SpectrumFrame>();
  qRegisterMetaType<CwCharacterFeatureWindowPtr>();
  qRegisterMetaType<CwCharacterHypothesisPtr>();
  auto* character_worker = new LocalCharacterInferenceWorker;
  character_inference_worker_ = character_worker;
  character_worker->moveToThread(&character_inference_thread_);
  connect(&character_inference_thread_, &QThread::finished, character_worker,
          &QObject::deleteLater);
  connect(this, &ReplayController::localCharacterDecoderConfigureRequested,
          character_worker, &LocalCharacterInferenceWorker::configure);
  connect(this, &ReplayController::localCharacterResetRequested,
          character_worker, &LocalCharacterInferenceWorker::discardPending,
          Qt::DirectConnection);
  connect(character_worker, &LocalCharacterInferenceWorker::statusChanged,
          this, [this](const QString& state, const QString& detail) {
            local_character_state_ = state;
            local_character_status_ = detail;
            const bool ready = state == QStringLiteral("ready");
            if (!ready) local_character_consensus_.clear();
            emit replayCharacterFrontendEnabledRequested(ready);
            emit liveCharacterFrontendEnabledRequested(ready);
            rebuildDecoderModels();
          });
  connect(character_worker, &LocalCharacterInferenceWorker::resultReady,
          this, [this](const int source_mode,
                       const CwCharacterHypothesisPtr& hypothesis) {
            if (!hypothesis || source_mode != source_mode_ ||
                local_character_state_ != QStringLiteral("ready")) return;
            const auto id = hypothesis->track.track_id;
            auto [entry, inserted] = local_character_consensus_.try_emplace(id);
            static_cast<void>(inserted);
            const std::size_t previous_stable_size =
                entry->second.stableText().size();
            const auto update = entry->second.process(*hypothesis);
            const auto fresh_evidence = freshCharacterRefinementCallEvidence(
                entry->second.stableText(), previous_stable_size);
            if (fresh_evidence) {
              const QString stable_text =
                  QString::fromStdString(*fresh_evidence);
              if (source_mode == 0) {
                emit liveCharacterRefinementRequested(
                    static_cast<qulonglong>(id), stable_text,
                    static_cast<qulonglong>(hypothesis->window_ended_ns));
              } else {
                emit replayCharacterRefinementRequested(
                    static_cast<qulonglong>(id), stable_text,
                    static_cast<qulonglong>(hypothesis->window_ended_ns));
              }
            }
            if (update.changed) rebuildDecoderModels();
          });
  character_inference_thread_.setObjectName(
      QStringLiteral("Local character inference"));
  character_inference_thread_.start();
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
  connect(this, &ReplayController::decodedSignalTimeoutRequested, worker,
          &ReplayWorker::setDecodedSignalTimeoutSeconds);
  connect(this, &ReplayController::replayCharacterFrontendEnabledRequested,
          worker, &ReplayWorker::setLocalCharacterFrontendEnabled);
  connect(this, &ReplayController::replayCharacterRefinementRequested,
          worker, &ReplayWorker::acceptCharacterRefinement);
  connect(worker, &ReplayWorker::characterWindowProduced, character_worker,
          &LocalCharacterInferenceWorker::submit, Qt::DirectConnection);
  connect(this, &ReplayController::manualDecoderFrequencyRequested, worker,
          &ReplayWorker::selectDecoderFrequency);
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
  connect(worker, &ReplayWorker::decoderProduced, this,
          &ReplayController::acceptDecoderChannels);
  connect(worker, &ReplayWorker::manualDecoderSelected, this,
          &ReplayController::openDecoderSession);
  connect(worker, &ReplayWorker::diagnosticsProduced, this,
          [this](const QVariantMap& diagnostics) {
            verification_diagnostics_ = diagnostics;
            emit decoderChanged();
          });
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
  connect(this, &ReplayController::liveDecodedSignalTimeoutRequested,
          dsp_worker, &LiveAudioDspWorker::setDecodedSignalTimeoutSeconds);
  connect(this, &ReplayController::liveCharacterFrontendEnabledRequested,
          dsp_worker, &LiveAudioDspWorker::setLocalCharacterFrontendEnabled);
  connect(this, &ReplayController::liveCharacterRefinementRequested,
          dsp_worker, &LiveAudioDspWorker::acceptCharacterRefinement);
  connect(dsp_worker, &LiveAudioDspWorker::characterWindowProduced,
          character_worker, &LocalCharacterInferenceWorker::submit,
          Qt::DirectConnection);
  connect(this, &ReplayController::liveFrequencyShiftRequested, dsp_worker,
          &LiveAudioDspWorker::shiftTrackedFrequencies);
  connect(this, &ReplayController::liveManualDecoderFrequencyRequested,
          dsp_worker, &LiveAudioDspWorker::selectDecoderFrequency);
  connect(this, &ReplayController::liveRadioFrequencyContextRequested,
          dsp_worker, &LiveAudioDspWorker::setRadioFrequencyContext);
  connect(this, &ReplayController::liveDebugCaptureStartRequested, dsp_worker,
          &LiveAudioDspWorker::startDebugCapture);
  connect(this, &ReplayController::liveDebugCaptureStopRequested, dsp_worker,
          &LiveAudioDspWorker::stopDebugCapture);
  connect(dsp_worker, &LiveAudioDspWorker::debugCaptureStateChanged, this,
          [this](const bool active, const QString& path,
                 const double elapsed_seconds, const QString& note) {
            debug_capture_active_ = active;
            debug_capture_path_ = path;
            debug_capture_elapsed_seconds_ = elapsed_seconds;
            debug_capture_note_ = note;
            emit debugCaptureChanged();
            if (!active) {
              setStatus(QStringLiteral("Debug capture: %1 (%2)")
                            .arg(note, path));
            }
          });
  connect(capture_worker, &LiveAudioCaptureWorker::started, this,
          [this](const QString& name, const double sample_rate,
                 const int channel_count) {
            source_name_ = name;
            sample_rate_ = sample_rate;
            duration_seconds_ = 0.0;
            position_seconds_ = 0.0;
            input_overruns_ = 0;
            live_capturing_ = true;
            rebuildDecoderModels();
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
      rebuildDecoderModels();
      status_text_ = QStringLiteral("Live audio stopped");
      emit stateChanged();
    }
  });
  connect(capture_worker, &LiveAudioCaptureWorker::failed, this,
          [this](const QString& message) {
            live_capturing_ = false;
            rebuildDecoderModels();
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
  connect(dsp_worker, &LiveAudioDspWorker::decoderProduced, this,
          &ReplayController::acceptDecoderChannels);
  connect(dsp_worker, &LiveAudioDspWorker::manualDecoderSelected, this,
          &ReplayController::openDecoderSession);
  connect(dsp_worker, &LiveAudioDspWorker::diagnosticsProduced, this,
          [this](const QVariantMap& diagnostics) {
            verification_diagnostics_ = diagnostics;
            emit decoderChanged();
          });
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
  emit localCharacterResetRequested();
  character_inference_thread_.quit();
  character_inference_thread_.wait();
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
const QVariantList& ReplayController::decoderChannels() const noexcept {
  return decoder_channels_;
}
int ReplayController::decoderChannelCount() const noexcept {
  return static_cast<int>(std::count_if(
      decoder_channels_.cbegin(), decoder_channels_.cend(),
      [](const QVariant& value) {
        return value.toMap().value(QStringLiteral("verifiedCw")).toBool();
      }));
}
const QVariantList& ReplayController::decoderSessions() const noexcept {
  return decoder_sessions_;
}
int ReplayController::decoderSessionCount() const noexcept {
  return static_cast<int>(decoder_sessions_.size());
}
const QVariantMap& ReplayController::verificationDiagnostics() const noexcept {
  return verification_diagnostics_;
}
const QString& ReplayController::localCharacterState() const noexcept {
  return local_character_state_;
}
const QString& ReplayController::localCharacterStatus() const noexcept {
  return local_character_status_;
}
bool ReplayController::debugCaptureActive() const noexcept {
  return debug_capture_active_;
}
const QString& ReplayController::debugCapturePath() const noexcept {
  return debug_capture_path_;
}
double ReplayController::debugCaptureElapsedSeconds() const noexcept {
  return debug_capture_elapsed_seconds_;
}
const QString& ReplayController::debugCaptureNote() const noexcept {
  return debug_capture_note_;
}
bool ReplayController::radioFrequencyAvailable() const noexcept {
  return radio_frequency_available_;
}
qulonglong ReplayController::radioRxFrequencyHz() const noexcept {
  return radio_rx_rf_hz_;
}
qulonglong ReplayController::radioTxFrequencyHz() const noexcept {
  return radio_tx_rf_hz_;
}
bool ReplayController::radioSplitActive() const noexcept {
  return radio_split_active_;
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

void ReplayController::configureLocalCharacterDecoder(
    const bool enabled, const QString& model_path,
    const QString& metadata_path) {
  emit replayCharacterFrontendEnabledRequested(false);
  emit liveCharacterFrontendEnabledRequested(false);
  emit localCharacterResetRequested();
  local_character_consensus_.clear();
  local_character_state_ = enabled ? QStringLiteral("loading")
                                   : QStringLiteral("disabled");
  local_character_status_ = enabled
      ? QStringLiteral("Loading and validating the local model…")
      : QStringLiteral("Local model disabled.");
  rebuildDecoderModels();
  emit localCharacterDecoderConfigureRequested(enabled, model_path,
                                                metadata_path);
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
  const bool decoder_passband_changed =
      spectrum_processing_configured_ &&
      (audio_automatic_bandwidth_ != automatic_bandwidth ||
       audio_lower_frequency_hz_ != clamped_lower_hz ||
       audio_upper_frequency_hz_ != clamped_upper_hz);
  audio_dc_rejection_ = dc_rejection;
  audio_automatic_gain_ = automatic_gain;
  audio_gain_db_ = clamped_gain_db;
  audio_automatic_gain_target_dbfs_ = clamped_target_dbfs;
  audio_automatic_bandwidth_ = automatic_bandwidth;
  audio_lower_frequency_hz_ = clamped_lower_hz;
  audio_upper_frequency_hz_ = clamped_upper_hz;
  spectrum_frame_rate_hz_ = clamped_frame_rate_hz;
  spectrum_processing_configured_ = true;
  if (decoder_passband_changed) resetDecoder();
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

void ReplayController::acceptDecoderChannels(const QVariantList& channels) {
  raw_decoder_channels_ = channels;
  rebuildDecoderModels();
}

void ReplayController::rebuildDecoderModels() {
  const QVariantList previous_sessions = decoder_sessions_;
  decoder_channels_.clear();
  QHash<qulonglong, QVariantMap> by_id;
  const bool show_rf = radio_frequency_available_ && source_mode_ == 0 &&
                       live_capturing_;
  for (const QVariant& value : raw_decoder_channels_) {
    QVariantMap item = value.toMap();
    const auto id = item.value(QStringLiteral("id")).toULongLong();
    const double tracked_audio_hz =
        item.value(QStringLiteral("frequencyHz")).toDouble();
    const QVariant presentation =
        item.value(QStringLiteral("presentationFrequencyHz"));
    const double audio_hz = presentation.isValid()
        ? presentation.toDouble() : tracked_audio_hz;
    item.insert(QStringLiteral("audioFrequencyHz"), audio_hz);
    item.insert(QStringLiteral("trackedAudioFrequencyHz"), tracked_audio_hz);
    if (show_rf) {
      const auto resolved_rf = cwassistant::core::resolve_audio_tone_rf(
          radio_rx_rf_hz_, audio_hz, cw_reference_tone_hz_,
          cw_sideband_index_ == 0);
      if (resolved_rf) {
        const auto rf_hz = static_cast<qulonglong>(*resolved_rf);
        item.insert(QStringLiteral("displayFrequencyHz"),
                    QVariant::fromValue<qulonglong>(rf_hz));
        item.insert(QStringLiteral("frequencyKind"), QStringLiteral("RF"));
        item.insert(QStringLiteral("frequencyLabel"),
                    QStringLiteral("%1 Hz RF").arg(rf_hz));
      }
    }
    if (!item.contains(QStringLiteral("frequencyLabel"))) {
      item.insert(QStringLiteral("displayFrequencyHz"), audio_hz);
      item.insert(QStringLiteral("frequencyKind"), QStringLiteral("AF"));
      item.insert(QStringLiteral("frequencyLabel"),
                  QStringLiteral("%1 Hz AF").arg(audio_hz, 0, 'f', 0));
    }
    item.insert(QStringLiteral("sessionOpen"),
                decoder_session_order_.contains(id));
    item.insert(QStringLiteral("localModelState"), local_character_state_);
    item.insert(QStringLiteral("localModelStatus"), local_character_status_);
    const auto local = local_character_consensus_.find(id);
    if (item.value(QStringLiteral("verifiedCw")).toBool() &&
        local != local_character_consensus_.end()) {
      const auto& stable_text = local->second.stableText();
      item.insert(QStringLiteral("localModelText"),
                  QString::fromStdString(stable_text));
      const auto suggested_callsign =
          cwassistant::core::CallsignPolicy::latest_in_text(stable_text);
      item.insert(QStringLiteral("localModelCallsign"),
                  suggested_callsign
                      ? QString::fromStdString(*suggested_callsign)
                      : QString{});
    } else {
      item.insert(QStringLiteral("localModelText"), QString{});
      item.insert(QStringLiteral("localModelCallsign"), QString{});
    }
    decoder_channels_.push_back(item);
    by_id.insert(id, item);
  }
  decoder_session_order_ = reconcileDecoderSessionOrder(
      decoder_session_order_, previous_sessions, decoder_channels_);
  for (QVariant& value : decoder_channels_) {
    QVariantMap item = value.toMap();
    item.insert(QStringLiteral("sessionOpen"),
                decoder_session_order_.contains(
                    item.value(QStringLiteral("id")).toULongLong()));
    value = item;
    by_id.insert(item.value(QStringLiteral("id")).toULongLong(), item);
  }
  decoder_sessions_.clear();
  for (const auto id : decoder_session_order_) {
    auto item = by_id.value(id);
    item.insert(QStringLiteral("sessionOpen"), true);
    decoder_sessions_.push_back(item);
  }
  emit decoderChanged();
}

void ReplayController::setRadioFrequencyContext(
    const bool available, const qulonglong rx_rf_hz,
    const qulonglong tx_rf_hz, const bool split_active,
    const int sideband_index, const double reference_tone_hz) {
  const int sideband = std::clamp(sideband_index, 0, 1);
  const double reference = std::clamp(reference_tone_hz, 0.0, 96'000.0);
  if (radio_frequency_available_ == available &&
      radio_rx_rf_hz_ == rx_rf_hz && radio_tx_rf_hz_ == tx_rf_hz &&
      radio_split_active_ == split_active &&
      cw_sideband_index_ == sideband && cw_reference_tone_hz_ == reference) {
    return;
  }
  // A retune while already linked (not the initial link-up, and not a live
  // audio source) shifts every currently tracked signal's audio frequency
  // by the same amount the RX dial moved, so identified signals keep their
  // identity across the retune instead of being lost and re-acquired.
  if (source_mode_ == 0 && radio_frequency_available_ && available &&
      radio_rx_rf_hz_ != rx_rf_hz) {
    const double delta_rf_hz =
        static_cast<double>(rx_rf_hz) - static_cast<double>(radio_rx_rf_hz_);
    const double delta_audio_hz = sideband == 0 ? -delta_rf_hz : delta_rf_hz;
    emit liveFrequencyShiftRequested(delta_audio_hz);
  }
  radio_frequency_available_ = available;
  radio_rx_rf_hz_ = rx_rf_hz;
  radio_tx_rf_hz_ = tx_rf_hz;
  radio_split_active_ = split_active;
  cw_sideband_index_ = sideband;
  cw_reference_tone_hz_ = reference;
  emit liveRadioFrequencyContextRequested(radio_frequency_available_,
                                          radio_rx_rf_hz_, radio_tx_rf_hz_,
                                          radio_split_active_);
  emit radioFrequencyChanged();
  rebuildDecoderModels();
}

void ReplayController::openDecoderSession(const qulonglong channel_id) {
  if (decoder_session_order_.contains(channel_id)) return;
  const bool exists = std::any_of(
      decoder_channels_.cbegin(), decoder_channels_.cend(),
      [channel_id](const QVariant& value) {
        return value.toMap().value(QStringLiteral("id")).toULongLong() ==
               channel_id;
      });
  if (!exists) return;
  decoder_session_order_.push_back(channel_id);
  rebuildDecoderModels();
}

void ReplayController::openManualDecoderSession(
    const double audio_frequency_hz) {
  if (!activeSource() || !std::isfinite(audio_frequency_hz)) return;
  if (source_mode_ == 0) {
    emit liveManualDecoderFrequencyRequested(audio_frequency_hz);
  } else {
    emit manualDecoderFrequencyRequested(audio_frequency_hz);
  }
}

void ReplayController::closeDecoderSession(const qulonglong channel_id) {
  if (decoder_session_order_.removeAll(channel_id) > 0) rebuildDecoderModels();
}

void ReplayController::moveDecoderSession(const qulonglong channel_id,
                                          const int new_index) {
  const int old_index = decoder_session_order_.indexOf(channel_id);
  if (old_index < 0 || decoder_session_order_.size() < 2) return;
  const int target = std::clamp(
      new_index, 0, static_cast<int>(decoder_session_order_.size()) - 1);
  if (old_index == target) return;
  decoder_session_order_.move(old_index, target);
  rebuildDecoderModels();
}

void ReplayController::resetDecoder() {
  raw_decoder_channels_.clear();
  decoder_channels_.clear();
  decoder_sessions_.clear();
  decoder_session_order_.clear();
  verification_diagnostics_.clear();
  local_character_consensus_.clear();
  emit localCharacterResetRequested();
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
  rebuildDecoderModels();
  emit sourceReset();
  emit stateChanged();
}

void ReplayController::setDecodedSignalTimeoutSeconds(const int seconds) {
  emit decodedSignalTimeoutRequested(seconds);
  emit liveDecodedSignalTimeoutRequested(seconds);
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

void ReplayController::startDebugCapture() {
  if (!live_capturing_) {
    setStatus(QStringLiteral(
        "Debug capture requires live RX to be running."));
    return;
  }
  const QString base = QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation);
  const QString directory = QDir(base).filePath(QStringLiteral("diagnostics"));
  emit liveDebugCaptureStartRequested(directory);
}

void ReplayController::stopDebugCapture() {
  emit liveDebugCaptureStopRequested();
}

void ReplayController::stopLiveAudio() {
  emit liveStopRequested();
  emit liveDspStopRequested();
  if (live_capturing_) {
    live_capturing_ = false;
    rebuildDecoderModels();
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
