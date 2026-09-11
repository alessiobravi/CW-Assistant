#pragma once

#include <QAudioFormat>
#include <QByteArray>
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
#include <optional>

#include "../decoder/local_character_decoder.hpp"
#include "../visualization/spectrum_frame.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/iq_receive.hpp"
#include "cwassistant/core/iq_writer.hpp"
#include "cwassistant/core/sample_block.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/wav_writer.hpp"
#include "live_audio_pipe.hpp"

#include <fstream>

class QAudioSource;
class QIODevice;

namespace cwassistant::desktop {

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
                 double automatic_gain_target_dbfs, bool automatic_bandwidth,
                 double lower_frequency_hz, double upper_frequency_hz);
  void setOwnCallsign(const QString& callsign);
  void setKeyingModel(const QString& model);
  void setDebugCaptureMaximumSeconds(double seconds);
  void setOperatorRole(const QString& role);
  void setDecodedSignalTimeoutSeconds(int seconds);
  // Chooses which tracked signals are decoded, and from what level. It never
  // touches the audio the detector receives, so it deliberately does not reset
  // the decoder: doing so would discard every track, transcript and confirmed
  // callsign the operator currently has open in exchange for a preference the
  // channel bank honours from its very next update.
  void setWeakSignalDecoding(bool enabled, double minimum_decode_snr_db);
  void setLocalCharacterFrontendEnabled(bool enabled);
  void setMonitor(int mode, const QVariantList& channel_ids,
                  double reference_tone_hz);
  void setSdrDecoderWindow(double center_frequency_hz, double bandwidth_hz);
  void acceptCharacterRefinement(qulonglong channel_id,
                                 const QString& stable_text,
                                 qulonglong evidence_timestamp_ns);
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
  // Receive-only description of the SDR front end, mirrored here purely so a
  // debug capture can record the gain state alongside the samples. A recording
  // without it documents the symptom and not the cause: an overloaded front
  // end and a starved one look very different in the same spectrum, and the
  // application has no other overload indicator.
  void setSdrCaptureContext(const QString& receiver_label,
                            const QString& antenna, bool automatic_gain,
                            double gain_db);
  void setPresentationDiagnostics(const QVariantMap& diagnostics);
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
  void monitorAudioProduced(const QByteArray& float_mono_audio,
                            double sample_rate_hz);

 private slots:
  void drain();

 private:
  void captureBlock(const cwassistant::core::RealtimeSampleBlock& block);
  [[nodiscard]] bool openIqCapture(
      const cwassistant::core::RealtimeSampleBlock& block);
  void writeDebugCaptureSnapshot();
  void finishDebugCapture(const QString& note);

  std::shared_ptr<LiveAudioPipe> pipe_;
  QTimer timer_;
  cwassistant::core::SpectrumAnalyzer analyzer_;
  // Recorded in the diagnostics so a report of "decodes nothing" can be told
  // apart from a detector that was never given a spectrum frame.
  std::uint64_t detector_frames_{0};
  std::uint64_t decoder_resets_{0};
  cwassistant::core::SpectrumAnalyzer decoder_analyzer_{
      {.fft_size = 8'192, .averaging_frames = 3, .frame_rate_hz = 60}};
  cwassistant::core::IqSubbandDecimator sdr_decoder_channelizer_;
  cwassistant::core::RealtimeSampleBlock sdr_decoder_pending_;
  std::uint64_t sdr_decoder_pending_sequence_{0};
  double sdr_decoder_center_frequency_hz_{14'050'000.0};
  double sdr_decoder_bandwidth_hz_{24'000.0};
  std::optional<double> pending_manual_frequency_hz_;
  cwassistant::core::CwChannelBank decoder_;
  LocalCharacterFrontendBank character_frontends_;

  cwassistant::core::WavWriter capture_writer_;
  // Complex IQ needs its own recorder: WavWriter is hard-wired to mono PCM16
  // and keeps only the real component, which throws away the sideband
  // distinction that is the entire point of recording a complex receiver.
  cwassistant::core::IqWriter capture_iq_writer_;
  std::ofstream capture_diagnostics_log_;
  QString capture_base_path_;
  QString capture_wav_path_;
  QString capture_iq_path_;
  // ci16_le is lossless for both supported receivers (the RSPduo is 14-bit,
  // the RTL-SDR 8-bit) and halves the file against cf32_le, which at
  // megasample rates decides whether a capture is usable at all.
  cwassistant::core::IqSampleFormat capture_iq_format_{
      cwassistant::core::IqSampleFormat::Ci16Le};
  double capture_iq_sample_rate_hz_{0.0};
  double capture_iq_center_frequency_hz_{0.0};
  QString sdr_receiver_label_;
  QString sdr_antenna_;
  bool sdr_gain_state_known_{false};
  bool sdr_automatic_gain_{false};
  double sdr_gain_db_{0.0};
  std::uint64_t capture_start_ns_{0};
  std::uint64_t capture_last_snapshot_ns_{0};
  std::size_t capture_existing_track_count_{0};
  std::size_t capture_existing_published_count_{0};
  bool capture_active_{false};
  bool capture_writer_pending_{false};
  bool capture_have_start_{false};
  bool radio_frequency_available_{false};
  qulonglong radio_rx_rf_hz_{0};
  qulonglong radio_tx_rf_hz_{0};
  bool radio_split_active_{false};
  int monitor_mode_{0};
  double monitor_resample_phase_{0.0};
  double monitor_resample_input_rate_hz_{0.0};
  float monitor_resample_sum_{0.0F};
  std::size_t monitor_resample_count_{0};
  QVariantMap presentation_diagnostics_;
  // How long a debug capture runs before stopping itself. Configurable because
  // a signal that only misbehaves occasionally cannot be caught inside a fixed
  // five minutes, while a quick reproduction should not leave the operator with
  // a needlessly large audio file to review before sharing it.
  static constexpr double kDefaultMaximumCaptureSeconds = 300.0;
  double maximum_capture_seconds_{kDefaultMaximumCaptureSeconds};
  // A wide IQ recording dwarfs an audio one: at 8 MS/s the ci16 stream is
  // 32 MB/s, so the operator's duration setting on its own is not a usable
  // bound. 4 GiB is roughly 134 seconds at that rate -- a generous forensic
  // window -- and is also the largest single file a FAT32 removable drive
  // accepts, which is where these recordings usually end up.
  static constexpr std::uint64_t kMaximumIqCaptureBytes =
      4ULL * 1024ULL * 1024ULL * 1024ULL;
  static constexpr double kSnapshotIntervalSeconds = 1.0;
};

}  // namespace cwassistant::desktop
