#pragma once

#include <QAudioFormat>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "cwassistant/core/sample_block.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/spsc_ring_buffer.hpp"
#include "cwassistant/core/wav_writer.hpp"
#include "../visualization/spectrum_frame.hpp"
#include "../decoder/local_character_decoder.hpp"

#include <fstream>

class QAudioSource;
class QIODevice;

namespace cwassistant::desktop {

struct LiveAudioPipe {
  cwassistant::core::SpscRingBuffer<cwassistant::core::RealtimeSampleBlock, 32>
      blocks;
  std::atomic<qulonglong> overruns{0};
};

class LiveAudioCaptureWorker final : public QObject {
  Q_OBJECT

 public:
  explicit LiveAudioCaptureWorker(std::shared_ptr<LiveAudioPipe> pipe,
                                  QObject* parent = nullptr);
  ~LiveAudioCaptureWorker() override;

 public slots:
  void start(const QString& encoded_device_id);
  void stop();

 signals:
  void started(const QString& device_name, double sample_rate,
               int channel_count);
  void stopped();
  void failed(const QString& message);
  void overrunCountChanged(qulonglong count);

 private slots:
  void consumeAvailableBytes();
  void handleStateChanged();

 private:
  [[nodiscard]] float readSample(const char* data) const noexcept;
  void appendFrame(const char* frame);
  void publishBlock();

  static constexpr std::size_t kPublishedBlockSamples = 2'048;
  static constexpr qsizetype kRawBufferBytes = 65'536;

  std::shared_ptr<LiveAudioPipe> pipe_;
  QAudioSource* source_{nullptr};
  QIODevice* input_{nullptr};
  QAudioFormat format_;
  std::array<char, static_cast<std::size_t>(kRawBufferBytes)> raw_buffer_{};
  qsizetype pending_bytes_{0};
  cwassistant::core::RealtimeSampleBlock block_{};
  std::uint64_t sequence_{0};
  std::uint64_t captured_samples_{0};
  bool stopping_{false};
};

class LiveAudioDspWorker final : public QObject {
  Q_OBJECT

 public:
  explicit LiveAudioDspWorker(std::shared_ptr<LiveAudioPipe> pipe,
                              QObject* parent = nullptr);

 public slots:
  void start();
  void stop();
  void configure(int averaging_frames, int frame_rate_hz, bool dc_rejection,
                 bool automatic_gain, double gain_db,
                 double automatic_gain_target_dbfs,
                 bool automatic_bandwidth, double lower_frequency_hz,
                 double upper_frequency_hz);
  void setDecodedSignalTimeoutSeconds(int seconds);
  void setLocalCharacterFrontendEnabled(bool enabled);
  // Re-centers every currently tracked signal by a known audio-domain shift
  // (the shift implied by an operator retuning the linked radio's RX VFO),
  // so an already-identified signal's tracking follows the retune instead
  // of being lost and re-acquired from scratch.
  void shiftTrackedFrequencies(double audio_hz_delta);
  void selectDecoderFrequency(double audio_frequency_hz);
  // Mirrors the radio frequency context ReplayController tracks, purely so
  // a debug capture snapshot can record whether/how the RX (and TX, if
  // split) dial frequency moved during the capture window.
  void setRadioFrequencyContext(bool available, qulonglong rx_rf_hz,
                                qulonglong tx_rf_hz, bool split_active);
  // Operator-started, bounded diagnostic capture (OBS-003): records the raw
  // audio feeding the decoder plus periodic per-track private diagnostic
  // snapshots to help debug why a visible signal is not decoding. Never
  // starts implicitly; always bounded in duration.
  void startDebugCapture(const QString& directory_path);
  void stopDebugCapture();

signals:
  void frameProduced(const cwassistant::desktop::SpectrumFrame& frame);
  void decoderProduced(const QVariantList& channels);
  void diagnosticsProduced(const QVariantMap& diagnostics);
  void manualDecoderSelected(qulonglong channel_id);
  void debugCaptureStateChanged(bool active, const QString& base_path,
                                double elapsed_seconds, const QString& note);
  void characterWindowProduced(
      int source_mode,
      cwassistant::desktop::CwCharacterFeatureWindowPtr window);

 private slots:
  void drain();

 private:
  void writeDebugCaptureSnapshot();
  void finishDebugCapture(const QString& note);

  std::shared_ptr<LiveAudioPipe> pipe_;
  QTimer timer_;
  cwassistant::core::SpectrumAnalyzer analyzer_;
  cwassistant::core::CwChannelBank decoder_;
  LocalCharacterFrontendBank character_frontends_;

  cwassistant::core::WavWriter capture_writer_;
  std::ofstream capture_diagnostics_log_;
  QString capture_base_path_;
  QString capture_wav_path_;
  std::uint64_t capture_start_ns_{0};
  std::uint64_t capture_last_snapshot_ns_{0};
  bool capture_active_{false};
  bool capture_writer_pending_{false};
  bool capture_have_start_{false};
  bool radio_frequency_available_{false};
  qulonglong radio_rx_rf_hz_{0};
  qulonglong radio_tx_rf_hz_{0};
  bool radio_split_active_{false};
  static constexpr double kMaximumCaptureSeconds = 300.0;
  static constexpr double kSnapshotIntervalSeconds = 1.0;
};

}  // namespace cwassistant::desktop
