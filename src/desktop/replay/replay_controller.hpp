#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../visualization/spectrum_frame.hpp"
#include "cwassistant/core/cw_character_decoder.hpp"

namespace cwassistant::desktop {

// Keeps an operator-opened decoder card attached to the retained visual
// stream when the low-level tracker reacquires that same color/frequency with
// a new internal ID.
[[nodiscard]] QList<qulonglong> reconcileDecoderSessionOrder(
    const QList<qulonglong>& requested_order,
    const QVariantList& previous_sessions,
    const QVariantList& current_channels);

// Returns only a callsign-bearing span whose stable consensus grew in the
// current inference update. Older append-only text cannot be reused as fresh
// verification evidence when unrelated later characters arrive.
[[nodiscard]] std::optional<std::string> freshCharacterRefinementCallEvidence(
    std::string_view stable_text, std::size_t previous_stable_size);

class ReplayController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString sourceName READ sourceName NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(bool sourceLoaded READ sourceLoaded NOTIFY stateChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
  Q_PROPERTY(int sourceMode READ sourceMode WRITE setSourceMode NOTIFY stateChanged)
  Q_PROPERTY(bool liveCapturing READ liveCapturing NOTIFY stateChanged)
  Q_PROPERTY(bool activeSource READ activeSource NOTIFY stateChanged)
  Q_PROPERTY(qulonglong inputOverruns READ inputOverruns NOTIFY stateChanged)
  Q_PROPERTY(double sampleRate READ sampleRate NOTIFY stateChanged)
  Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY stateChanged)
  Q_PROPERTY(double positionSeconds READ positionSeconds NOTIFY stateChanged)
  Q_PROPERTY(int averagingFrames READ averagingFrames WRITE setAveragingFrames
                 NOTIFY averagingFramesChanged)
  Q_PROPERTY(QVariantList decoderChannels READ decoderChannels
                 NOTIFY decoderChanged)
  Q_PROPERTY(int decoderChannelCount READ decoderChannelCount
                 NOTIFY decoderChanged)
  Q_PROPERTY(QVariantList decoderSessions READ decoderSessions
                 NOTIFY decoderChanged)
  Q_PROPERTY(int decoderSessionCount READ decoderSessionCount
                 NOTIFY decoderChanged)
  Q_PROPERTY(QVariantMap verificationDiagnostics READ verificationDiagnostics
                 NOTIFY decoderChanged)
  Q_PROPERTY(QString localCharacterState READ localCharacterState
                 NOTIFY decoderChanged)
  Q_PROPERTY(QString localCharacterStatus READ localCharacterStatus
                 NOTIFY decoderChanged)
  Q_PROPERTY(bool debugCaptureActive READ debugCaptureActive
                 NOTIFY debugCaptureChanged)
  Q_PROPERTY(QString debugCapturePath READ debugCapturePath
                 NOTIFY debugCaptureChanged)
  Q_PROPERTY(double debugCaptureElapsedSeconds READ debugCaptureElapsedSeconds
                 NOTIFY debugCaptureChanged)
  Q_PROPERTY(QString debugCaptureNote READ debugCaptureNote
                 NOTIFY debugCaptureChanged)
  Q_PROPERTY(bool radioFrequencyAvailable READ radioFrequencyAvailable
                 NOTIFY radioFrequencyChanged)
  Q_PROPERTY(qulonglong radioRxFrequencyHz READ radioRxFrequencyHz
                 NOTIFY radioFrequencyChanged)
  Q_PROPERTY(qulonglong radioTxFrequencyHz READ radioTxFrequencyHz
                 NOTIFY radioFrequencyChanged)
  Q_PROPERTY(bool radioSplitActive READ radioSplitActive
                 NOTIFY radioFrequencyChanged)

 public:
  explicit ReplayController(QObject* parent = nullptr);
  ~ReplayController() override;

  [[nodiscard]] const QString& sourceName() const noexcept;
  [[nodiscard]] const QString& statusText() const noexcept;
  [[nodiscard]] bool sourceLoaded() const noexcept;
  [[nodiscard]] bool playing() const noexcept;
  [[nodiscard]] int sourceMode() const noexcept;
  [[nodiscard]] bool liveCapturing() const noexcept;
  [[nodiscard]] bool activeSource() const noexcept;
  [[nodiscard]] qulonglong inputOverruns() const noexcept;
  [[nodiscard]] double sampleRate() const noexcept;
  [[nodiscard]] double durationSeconds() const noexcept;
  [[nodiscard]] double positionSeconds() const noexcept;
  [[nodiscard]] int averagingFrames() const noexcept;
  [[nodiscard]] const QVariantList& decoderChannels() const noexcept;
  [[nodiscard]] int decoderChannelCount() const noexcept;
  [[nodiscard]] const QVariantList& decoderSessions() const noexcept;
  [[nodiscard]] int decoderSessionCount() const noexcept;
  [[nodiscard]] const QVariantMap& verificationDiagnostics() const noexcept;
  [[nodiscard]] const QString& localCharacterState() const noexcept;
  [[nodiscard]] const QString& localCharacterStatus() const noexcept;
  [[nodiscard]] bool debugCaptureActive() const noexcept;
  [[nodiscard]] const QString& debugCapturePath() const noexcept;
  [[nodiscard]] double debugCaptureElapsedSeconds() const noexcept;
  [[nodiscard]] const QString& debugCaptureNote() const noexcept;
  [[nodiscard]] bool radioFrequencyAvailable() const noexcept;
  [[nodiscard]] qulonglong radioRxFrequencyHz() const noexcept;
  [[nodiscard]] qulonglong radioTxFrequencyHz() const noexcept;
  [[nodiscard]] bool radioSplitActive() const noexcept;
  void setAveragingFrames(int value);
  void setSpectrumProcessing(bool dc_rejection, bool automatic_gain,
                             double gain_db,
                             double automatic_gain_target_dbfs,
                             bool automatic_bandwidth,
                             double lower_frequency_hz,
                             double upper_frequency_hz,
                             int frame_rate_hz);
  void setSourceMode(int value);
  void setDecodedSignalTimeoutSeconds(int seconds);
  void configureLocalCharacterDecoder(bool enabled, const QString& model_path,
                                      const QString& metadata_path);
  void setAudioInputSelection(QString encoded_id, QString display_name);
  void setRadioFrequencyContext(bool available, qulonglong rx_rf_hz,
                                qulonglong tx_rf_hz, bool split_active,
                                int sideband_index,
                                double reference_tone_hz);

  Q_INVOKABLE void openFile(const QUrl& url);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void startLiveAudio();
  Q_INVOKABLE void stopLiveAudio();
  Q_INVOKABLE void openDecoderSession(qulonglong channel_id);
  Q_INVOKABLE void openManualDecoderSession(double audio_frequency_hz);
  Q_INVOKABLE void closeDecoderSession(qulonglong channel_id);
  Q_INVOKABLE void moveDecoderSession(qulonglong channel_id, int new_index);
  // Operator-started, bounded diagnostic capture (OBS-003). Only available
  // while live audio is running; writes raw audio plus periodic per-track
  // private diagnostic snapshots to a timestamped folder under the app's
  // standard data location, capped at 5 minutes.
  Q_INVOKABLE void startDebugCapture();
  Q_INVOKABLE void stopDebugCapture();

 signals:
  void stateChanged();
  void sourceReset();
  void frameReady(const cwassistant::desktop::SpectrumFrame& frame);
  void averagingFramesChanged();
  void decoderChanged();

  void openRequested(const QString& path);
  void playRequested();
  void pauseRequested();
  void stopRequested();
  void configureRequested(int averaging_frames, int frame_rate_hz,
                          bool dc_rejection,
                          bool automatic_gain, double gain_db,
                          double automatic_gain_target_dbfs,
                          bool automatic_bandwidth,
                          double lower_frequency_hz,
                          double upper_frequency_hz);
  void liveStartRequested(const QString& encoded_device_id);
  void liveStopRequested();
  void liveDspStartRequested();
  void liveDspStopRequested();
  void liveDspConfigureRequested(int averaging_frames, int frame_rate_hz,
                                 bool dc_rejection,
                                 bool automatic_gain, double gain_db,
                                 double automatic_gain_target_dbfs,
                                 bool automatic_bandwidth,
                                 double lower_frequency_hz,
                                 double upper_frequency_hz);
  void decodedSignalTimeoutRequested(int seconds);
  void liveDecodedSignalTimeoutRequested(int seconds);
  void liveFrequencyShiftRequested(double audio_hz_delta);
  void manualDecoderFrequencyRequested(double audio_frequency_hz);
  void liveManualDecoderFrequencyRequested(double audio_frequency_hz);
  void liveRadioFrequencyContextRequested(bool available, qulonglong rx_rf_hz,
                                          qulonglong tx_rf_hz,
                                          bool split_active);
  void debugCaptureChanged();
  void radioFrequencyChanged();
  void liveDebugCaptureStartRequested(const QString& directory_path);
  void liveDebugCaptureStopRequested();
  void localCharacterDecoderConfigureRequested(bool enabled,
                                               const QString& model_path,
                                               const QString& metadata_path);
  void replayCharacterFrontendEnabledRequested(bool enabled);
  void liveCharacterFrontendEnabledRequested(bool enabled);
  void replayCharacterRefinementRequested(qulonglong channel_id,
                                          const QString& stable_text,
                                          qulonglong evidence_timestamp_ns);
  void liveCharacterRefinementRequested(qulonglong channel_id,
                                        const QString& stable_text,
                                        qulonglong evidence_timestamp_ns);
  void localCharacterResetRequested();

 private:
  void setStatus(QString status);
  void beginLiveAudioCapture();
  void publishSpectrumConfiguration();
  void acceptDecoderChannels(const QVariantList& channels);
  void rebuildDecoderModels();
  void resetDecoder();

  QThread worker_thread_;
  QObject* worker_{nullptr};
  QThread audio_capture_thread_;
  QObject* audio_capture_worker_{nullptr};
  QThread audio_dsp_thread_;
  QObject* audio_dsp_worker_{nullptr};
  QThread character_inference_thread_;
  QObject* character_inference_worker_{nullptr};
  QString source_name_;
  QString status_text_{QStringLiteral("Select Start live RX to begin receiving audio")};
  bool source_loaded_{false};
  bool playing_{false};
  int source_mode_{0};
  bool live_capturing_{false};
  qulonglong input_overruns_{0};
  QString audio_input_id_;
  QString audio_input_name_{QStringLiteral("System default input")};
  double sample_rate_{0.0};
  double duration_seconds_{0.0};
  double position_seconds_{0.0};
  int averaging_frames_{3};
  bool audio_dc_rejection_{true};
  bool audio_automatic_gain_{false};
  double audio_gain_db_{0.0};
  double audio_automatic_gain_target_dbfs_{-12.0};
  bool audio_automatic_bandwidth_{true};
  double audio_lower_frequency_hz_{100.0};
  double audio_upper_frequency_hz_{3'000.0};
  int spectrum_frame_rate_hz_{60};
  bool spectrum_processing_configured_{false};
  QVariantList decoder_channels_;
  QVariantList raw_decoder_channels_;
  QVariantList decoder_sessions_;
  QList<qulonglong> decoder_session_order_;
  QVariantMap verification_diagnostics_;
  std::unordered_map<std::uint64_t,
                     cwassistant::core::CwCharacterConsensusMerger>
      local_character_consensus_;
  QString local_character_state_{QStringLiteral("disabled")};
  QString local_character_status_{QStringLiteral("Local model disabled.")};
  bool debug_capture_active_{false};
  QString debug_capture_path_;
  double debug_capture_elapsed_seconds_{0.0};
  QString debug_capture_note_;
  bool radio_frequency_available_{false};
  qulonglong radio_rx_rf_hz_{0};
  qulonglong radio_tx_rf_hz_{0};
  bool radio_split_active_{false};
  int cw_sideband_index_{0};
  double cw_reference_tone_hz_{700.0};
};

}  // namespace cwassistant::desktop
