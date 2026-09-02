#include "live_audio_worker.hpp"

#include "decoder_channel_model.hpp"

#include <QAudioDevice>
#include <QAudioSource>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace cwassistant::desktop {
namespace {

QString encoded_device_id(const QAudioDevice& device) {
  return QString::fromLatin1(
      device.id().toBase64(QByteArray::Base64UrlEncoding |
                           QByteArray::OmitTrailingEquals));
}

QAudioDevice resolve_input(const QString& requested_id) {
  if (requested_id.isEmpty()) {
    return QMediaDevices::defaultAudioInput();
  }
  for (const auto& device : QMediaDevices::audioInputs()) {
    if (encoded_device_id(device) == requested_id) {
      return device;
    }
  }
  return {};
}

QAudioFormat capture_format(const QAudioDevice& device) {
  QAudioFormat requested;
  requested.setSampleRate(48'000);
  requested.setChannelCount(1);
  requested.setSampleFormat(QAudioFormat::Float);
  if (device.isFormatSupported(requested)) {
    return requested;
  }
  return device.preferredFormat();
}

}  // namespace

LiveAudioCaptureWorker::LiveAudioCaptureWorker(
    std::shared_ptr<LiveAudioPipe> pipe, QObject* parent)
    : QObject(parent), pipe_(std::move(pipe)) {}

LiveAudioCaptureWorker::~LiveAudioCaptureWorker() { stop(); }

void LiveAudioCaptureWorker::start(const QString& encoded_device_id_value) {
  stop();
  stopping_ = false;
  const QAudioDevice device = resolve_input(encoded_device_id_value);
  if (device.isNull()) {
    emit failed(QStringLiteral(
        "The selected audio input is unavailable. Reconnect it or select another input."));
    return;
  }

  format_ = capture_format(device);
  if (!format_.isValid() || format_.channelCount() < 1 ||
      format_.bytesPerFrame() < 1 ||
      format_.sampleFormat() == QAudioFormat::Unknown) {
    emit failed(QStringLiteral("The selected audio input has no supported PCM format."));
    return;
  }

  pipe_->overruns.store(0, std::memory_order_release);
  block_ = {};
  block_.stream = {
      .kind = cwassistant::core::StreamKind::Audio,
      .sample_rate_hz = static_cast<double>(format_.sampleRate()),
      .center_frequency_hz = 0.0,
      .channel_count = 1,
  };
  sequence_ = 0;
  captured_samples_ = 0;
  pending_bytes_ = 0;

  source_ = new QAudioSource(device, format_, this);
  source_->setBufferSize(
      std::max(format_.bytesPerFrame() * format_.sampleRate() / 10, 4'096));
  connect(source_, &QAudioSource::stateChanged, this,
          &LiveAudioCaptureWorker::handleStateChanged);
  input_ = source_->start();
  if (input_ == nullptr) {
    const QString message = QStringLiteral("The operating system could not start the audio input.");
    delete source_;
    source_ = nullptr;
    emit failed(message);
    return;
  }
  connect(input_, &QIODevice::readyRead, this,
          &LiveAudioCaptureWorker::consumeAvailableBytes);
  emit started(device.description(), static_cast<double>(format_.sampleRate()),
               format_.channelCount());
}

void LiveAudioCaptureWorker::stop() {
  stopping_ = true;
  input_ = nullptr;
  if (source_ != nullptr) {
    source_->stop();
    delete source_;
    source_ = nullptr;
    emit stopped();
  }
  pending_bytes_ = 0;
  block_ = {};
}

float LiveAudioCaptureWorker::readSample(const char* data) const noexcept {
  switch (format_.sampleFormat()) {
    case QAudioFormat::UInt8:
      return (static_cast<float>(*reinterpret_cast<const unsigned char*>(data)) -
              128.0F) /
             128.0F;
    case QAudioFormat::Int16: {
      std::int16_t value{};
      std::memcpy(&value, data, sizeof(value));
      return static_cast<float>(value) / 32'768.0F;
    }
    case QAudioFormat::Int32: {
      std::int32_t value{};
      std::memcpy(&value, data, sizeof(value));
      return static_cast<float>(static_cast<double>(value) / 2'147'483'648.0);
    }
    case QAudioFormat::Float: {
      float value{};
      std::memcpy(&value, data, sizeof(value));
      return std::clamp(value, -1.0F, 1.0F);
    }
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
      break;
  }
  return 0.0F;
}

void LiveAudioCaptureWorker::appendFrame(const char* frame) {
  const int bytes_per_sample = format_.bytesPerSample();
  float mono = 0.0F;
  for (int channel = 0; channel < format_.channelCount(); ++channel) {
    mono += readSample(frame + channel * bytes_per_sample);
  }
  mono /= static_cast<float>(format_.channelCount());

  if (block_.sample_count == 0) {
    block_.timestamp_ns = static_cast<std::uint64_t>(
        static_cast<long double>(captured_samples_) * 1'000'000'000.0L /
        static_cast<long double>(format_.sampleRate()));
  }
  block_.samples[block_.sample_count++] = {mono, 0.0F};
  ++captured_samples_;
  if (block_.sample_count == kPublishedBlockSamples) {
    publishBlock();
  }
}

void LiveAudioCaptureWorker::publishBlock() {
  block_.sequence = sequence_++;
  if (!pipe_->blocks.try_push(block_)) {
    const auto count = pipe_->overruns.fetch_add(1, std::memory_order_acq_rel) + 1;
    emit overrunCountChanged(count);
  }
  block_.sample_count = 0;
}

void LiveAudioCaptureWorker::consumeAvailableBytes() {
  if (input_ == nullptr) {
    return;
  }
  const qsizetype capacity = kRawBufferBytes - pending_bytes_;
  const qsizetype bytes_read =
      input_->read(raw_buffer_.data() + pending_bytes_, capacity);
  if (bytes_read <= 0) {
    return;
  }

  const qsizetype total = pending_bytes_ + bytes_read;
  const qsizetype frame_bytes = format_.bytesPerFrame();
  const qsizetype complete_bytes = total - total % frame_bytes;
  for (qsizetype offset = 0; offset < complete_bytes; offset += frame_bytes) {
    appendFrame(raw_buffer_.data() + offset);
  }
  pending_bytes_ = total - complete_bytes;
  if (pending_bytes_ > 0) {
    std::memmove(raw_buffer_.data(), raw_buffer_.data() + complete_bytes,
                 static_cast<std::size_t>(pending_bytes_));
  }
}

void LiveAudioCaptureWorker::handleStateChanged() {
  if (!stopping_ && source_ != nullptr &&
      source_->state() == QtAudio::StoppedState &&
      source_->error() != QtAudio::NoError) {
    emit failed(QStringLiteral("Live audio stopped because the input device reported an error."));
  }
}

LiveAudioDspWorker::LiveAudioDspWorker(std::shared_ptr<LiveAudioPipe> pipe,
                                       QObject* parent)
    : QObject(parent), pipe_(std::move(pipe)), timer_(this) {
  timer_.setInterval(5);
  connect(&timer_, &QTimer::timeout, this, &LiveAudioDspWorker::drain);
}

void LiveAudioDspWorker::start() {
  analyzer_.reset();
  decoder_.reset();
  cwassistant::core::RealtimeSampleBlock stale;
  while (pipe_->blocks.try_pop(stale)) {
  }
  timer_.start();
}

void LiveAudioDspWorker::stop() {
  timer_.stop();
  analyzer_.reset();
  decoder_.reset();
  emit diagnosticsProduced(
      verificationDiagnosticsModel(decoder_.verificationDiagnostics()));
  if (capture_active_) {
    finishDebugCapture(QStringLiteral("Live RX stopped"));
  }
}

void LiveAudioDspWorker::startDebugCapture(const QString& directory_path) {
  if (capture_active_) {
    finishDebugCapture(QStringLiteral("Restarted"));
  }
  const QString folder_name =
      QStringLiteral("cwa-debug-capture-%1")
          .arg(QDateTime::currentDateTimeUtc().toString(
              QStringLiteral("yyyyMMdd-HHmmss")));
  QDir base_dir(directory_path);
  if (!base_dir.exists()) {
    base_dir.mkpath(QStringLiteral("."));
  }
  const QString capture_dir = base_dir.filePath(folder_name);
  if (!QDir().mkpath(capture_dir)) {
    emit debugCaptureStateChanged(false, QString(), 0.0,
                                  QStringLiteral("Could not create capture folder"));
    return;
  }
  const QString log_path =
      QDir(capture_dir).filePath(QStringLiteral("diagnostics.jsonl"));
  capture_diagnostics_log_.open(log_path.toStdString(), std::ios::out | std::ios::trunc);
  if (!capture_diagnostics_log_) {
    emit debugCaptureStateChanged(
        false, QString(), 0.0,
        QStringLiteral("Could not open capture diagnostics file"));
    return;
  }
  // The WAV file is opened lazily on the first block in drain(), once the
  // input's actual sample rate is known; opening it here with a guessed
  // rate could write a file that plays back at the wrong pitch/speed.
  capture_base_path_ = capture_dir;
  capture_wav_path_ = QDir(capture_dir).filePath(QStringLiteral("audio.wav"));
  capture_writer_pending_ = true;
  capture_start_ns_ = 0;
  capture_last_snapshot_ns_ = 0;
  capture_have_start_ = false;
  capture_active_ = true;
  emit debugCaptureStateChanged(true, capture_base_path_, 0.0,
                                QStringLiteral("Recording"));
}

void LiveAudioDspWorker::stopDebugCapture() {
  if (!capture_active_) return;
  finishDebugCapture(QStringLiteral("Stopped by operator"));
}

void LiveAudioDspWorker::finishDebugCapture(const QString& note) {
  capture_writer_.close();
  if (capture_diagnostics_log_.is_open()) {
    capture_diagnostics_log_.flush();
    capture_diagnostics_log_.close();
  }
  const double elapsed_seconds = capture_have_start_
      ? static_cast<double>(capture_last_snapshot_ns_ - capture_start_ns_) /
            1'000'000'000.0
      : 0.0;
  capture_active_ = false;
  capture_writer_pending_ = false;
  capture_have_start_ = false;
  emit debugCaptureStateChanged(false, capture_base_path_, elapsed_seconds, note);
}

void LiveAudioDspWorker::writeDebugCaptureSnapshot() {
  QJsonObject root;
  root.insert(QStringLiteral("elapsedSeconds"),
              static_cast<double>(capture_last_snapshot_ns_ - capture_start_ns_) /
                  1'000'000'000.0);
  const auto diagnostics = decoder_.verificationDiagnostics();
  QJsonObject summary;
  summary.insert(QStringLiteral("candidateTracks"),
                 static_cast<qint64>(diagnostics.candidate_tracks));
  summary.insert(QStringLiteral("morseLikelyTracks"),
                 static_cast<qint64>(diagnostics.morse_likely_tracks));
  summary.insert(QStringLiteral("verifiedTracks"),
                 static_cast<qint64>(diagnostics.verified_tracks));
  root.insert(QStringLiteral("summary"), summary);

  // Recorded every snapshot (not just once) specifically so a reviewer can
  // tell, after the fact, whether/when the operator's RX VFO moved during
  // the capture window -- a VFO move is a common, easily overlooked
  // explanation for a signal that stops decoding partway through a capture.
  QJsonObject radio;
  radio.insert(QStringLiteral("available"), radio_frequency_available_);
  radio.insert(QStringLiteral("rxFrequencyHz"),
              static_cast<double>(radio_rx_rf_hz_));
  radio.insert(QStringLiteral("txFrequencyHz"),
              static_cast<double>(radio_tx_rf_hz_));
  radio.insert(QStringLiteral("splitActive"), radio_split_active_);
  root.insert(QStringLiteral("radio"), radio);

  QJsonArray tracks;
  for (const auto& track : decoder_.allTrackDiagnostics()) {
    QJsonObject item;
    item.insert(QStringLiteral("id"), static_cast<qint64>(track.id));
    item.insert(QStringLiteral("frequencyHz"), track.frequency_hz);
    item.insert(QStringLiteral("identityOriginFrequencyHz"),
                track.identity_origin_frequency_hz);
    item.insert(QStringLiteral("presentationFrequencyHz"),
                track.presentation_frequency_hz);
    item.insert(QStringLiteral("driftHzPerSecond"), track.drift_hz_per_second);
    item.insert(QStringLiteral("snrDb"), track.snr_db);
    item.insert(QStringLiteral("narrowbandCoherence"), track.narrowband_coherence);
    item.insert(QStringLiteral("filterWidthHz"), track.filter_width_hz);
    item.insert(QStringLiteral("state"), QString::fromLatin1(
        cwassistant::core::cwTrackStateName(track.verification_state)));
    item.insert(QStringLiteral("reason"), QString::fromLatin1(
        cwassistant::core::cwVerificationReasonName(track.verification_reason)));
    item.insert(QStringLiteral("spectralObservations"), track.spectral_observations);
    item.insert(QStringLiteral("keyTransitions"),
                static_cast<qint64>(track.key_transitions));
    item.insert(QStringLiteral("decodedSymbols"),
                static_cast<qint64>(track.decoded_symbols));
    item.insert(QStringLiteral("unknownSymbols"),
                static_cast<qint64>(track.unknown_symbols));
    item.insert(QStringLiteral("timingQuality"), track.timing_quality);
    item.insert(QStringLiteral("cadenceQuality"), track.cadence_quality);
    item.insert(QStringLiteral("meanCharacterConfidence"),
                track.mean_character_confidence);
    item.insert(QStringLiteral("wpm"), track.wpm);
    item.insert(QStringLiteral("acousticWpm"), track.acoustic_wpm);
    item.insert(QStringLiteral("acousticCadenceConfidence"),
                track.acoustic_cadence_confidence);
    item.insert(QStringLiteral("text"), QString::fromStdString(track.text));
    item.insert(QStringLiteral("provisionalText"),
                QString::fromStdString(track.provisional_text));
    item.insert(QStringLiteral("matchAgeSeconds"), track.match_age_seconds);
    item.insert(QStringLiteral("colorIndex"),
                static_cast<int>(track.color_index));
    item.insert(QStringLiteral("matched"), track.matched);
    item.insert(QStringLiteral("active"), track.active);
    item.insert(QStringLiteral("keyDown"), track.key_down);
    tracks.push_back(item);
  }
  root.insert(QStringLiteral("tracks"), tracks);

  capture_diagnostics_log_
      << QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString()
      << '\n';
}

void LiveAudioDspWorker::configure(
    const int averaging_frames, const int frame_rate_hz,
    const bool dc_rejection,
    const bool automatic_gain, const double gain_db,
    const double automatic_gain_target_dbfs, const bool automatic_bandwidth,
    const double lower_frequency_hz, const double upper_frequency_hz) {
  auto config = analyzer_.config();
  config.averaging_frames = static_cast<std::uint8_t>(
      std::clamp(averaging_frames, 1, 32));
  config.frame_rate_hz = static_cast<std::uint16_t>(
      std::clamp(frame_rate_hz, 1, 120));
  config.audio_dc_rejection = dc_rejection;
  config.audio_automatic_gain = automatic_gain;
  config.audio_gain_db = static_cast<float>(std::clamp(gain_db, -40.0, 40.0));
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
}

void LiveAudioDspWorker::setDecodedSignalTimeoutSeconds(const int seconds) {
  decoder_.configure({.decoded_track_retention_seconds =
                          static_cast<double>(std::clamp(seconds, 5, 120))});
}

void LiveAudioDspWorker::shiftTrackedFrequencies(const double audio_hz_delta) {
  decoder_.shiftTrackedFrequencies(audio_hz_delta);
}

void LiveAudioDspWorker::setRadioFrequencyContext(
    const bool available, const qulonglong rx_rf_hz,
    const qulonglong tx_rf_hz, const bool split_active) {
  radio_frequency_available_ = available;
  radio_rx_rf_hz_ = rx_rf_hz;
  radio_tx_rf_hz_ = tx_rf_hz;
  radio_split_active_ = split_active;
}

void LiveAudioDspWorker::drain() {
  cwassistant::core::RealtimeSampleBlock block;
  int drained = 0;
  while (drained < 8 && pipe_->blocks.try_pop(block)) {
    ++drained;
    auto snapshots = analyzer_.process(block);
    for (const auto& snapshot : snapshots) {
      static_cast<void>(decoder_.updateSpectrum(
          snapshot.timestamp_ns, snapshot.lower_frequency_hz,
          snapshot.upper_frequency_hz, snapshot.bins_dbfs));
    }
    const auto& decoder_channels = decoder_.processSamples(block);
    for (auto& snapshot : snapshots) {
      QVector<float> bins(static_cast<qsizetype>(snapshot.bins_dbfs.size()));
      std::copy(snapshot.bins_dbfs.cbegin(), snapshot.bins_dbfs.cend(),
                bins.begin());
      QVector<float> instantaneous_bins(
          static_cast<qsizetype>(snapshot.instantaneous_bins_dbfs.size()));
      std::copy(snapshot.instantaneous_bins_dbfs.cbegin(),
                snapshot.instantaneous_bins_dbfs.cend(),
                instantaneous_bins.begin());
      emit frameProduced(SpectrumFrame{
          .bins_dbfs = std::move(bins),
          .sequence = snapshot.sequence,
          .timestamp_ns = snapshot.timestamp_ns,
          .lower_frequency_hz = snapshot.lower_frequency_hz,
          .upper_frequency_hz = snapshot.upper_frequency_hz,
          .instantaneous_bins_dbfs = std::move(instantaneous_bins),
      });
    }
    emit decoderProduced(decoderChannelModel(decoder_channels));

    if (capture_active_ && capture_writer_pending_) {
      if (capture_writer_.open(capture_wav_path_.toStdString(),
                               block.stream.sample_rate_hz)) {
        capture_writer_pending_ = false;
      } else {
        finishDebugCapture(QStringLiteral("Could not open capture audio file"));
      }
    }
    if (capture_active_ && !capture_writer_pending_) {
      if (!capture_have_start_) {
        capture_start_ns_ = block.timestamp_ns;
        capture_last_snapshot_ns_ = block.timestamp_ns;
        capture_have_start_ = true;
      }
      if (!capture_writer_.writeBlock(block)) {
        finishDebugCapture(QStringLiteral("Capture reached its maximum size"));
      } else {
        const double elapsed_seconds =
            static_cast<double>(block.timestamp_ns - capture_start_ns_) /
            1'000'000'000.0;
        if (elapsed_seconds >= kMaximumCaptureSeconds) {
          finishDebugCapture(
              QStringLiteral("Reached the 5-minute capture limit"));
        } else if (static_cast<double>(block.timestamp_ns -
                                       capture_last_snapshot_ns_) /
                       1'000'000'000.0 >=
                   kSnapshotIntervalSeconds) {
          capture_last_snapshot_ns_ = block.timestamp_ns;
          writeDebugCaptureSnapshot();
          emit debugCaptureStateChanged(true, capture_base_path_,
                                        elapsed_seconds,
                                        QStringLiteral("Recording"));
        }
      }
    }
  }
  if (drained > 0) {
    emit diagnosticsProduced(
        verificationDiagnosticsModel(decoder_.verificationDiagnostics()));
  }
}

}  // namespace cwassistant::desktop
