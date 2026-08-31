#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>

#include "cwassistant/core/cw_decoder.hpp"

#include "../visualization/spectrum_frame.hpp"

namespace cwassistant::desktop {

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
  Q_PROPERTY(QString decodedText READ decodedText NOTIFY decoderChanged)
  Q_PROPERTY(double decoderWpm READ decoderWpm NOTIFY decoderChanged)
  Q_PROPERTY(double decoderSnrDb READ decoderSnrDb NOTIFY decoderChanged)
  Q_PROPERTY(double decoderToneHz READ decoderToneHz NOTIFY decoderChanged)
  Q_PROPERTY(double decoderConfidence READ decoderConfidence NOTIFY decoderChanged)
  Q_PROPERTY(bool decoderKeyDown READ decoderKeyDown NOTIFY decoderChanged)

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
  [[nodiscard]] const QString& decodedText() const noexcept;
  [[nodiscard]] double decoderWpm() const noexcept;
  [[nodiscard]] double decoderSnrDb() const noexcept;
  [[nodiscard]] double decoderToneHz() const noexcept;
  [[nodiscard]] double decoderConfidence() const noexcept;
  [[nodiscard]] bool decoderKeyDown() const noexcept;
  void setAveragingFrames(int value);
  void setSpectrumProcessing(bool dc_rejection, bool automatic_gain,
                             double gain_db,
                             double automatic_gain_target_dbfs,
                             bool automatic_bandwidth,
                             double lower_frequency_hz,
                             double upper_frequency_hz,
                             int frame_rate_hz);
  void setSourceMode(int value);
  void setAudioInputSelection(QString encoded_id, QString display_name);
  void setCwDecoderSlice(double center_hz, double width_hz);

  Q_INVOKABLE void openFile(const QUrl& url);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void startLiveAudio();
  Q_INVOKABLE void stopLiveAudio();

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

 private:
  void setStatus(QString status);
  void beginLiveAudioCapture();
  void publishSpectrumConfiguration();
  void processDecoderFrame(const cwassistant::desktop::SpectrumFrame& frame);
  void resetDecoder();

  QThread worker_thread_;
  QObject* worker_{nullptr};
  QThread audio_capture_thread_;
  QObject* audio_capture_worker_{nullptr};
  QThread audio_dsp_thread_;
  QObject* audio_dsp_worker_{nullptr};
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
  cwassistant::core::CwTimingDecoder cw_decoder_;
  QString decoded_text_;
  double decoder_center_hz_{700.0};
  double decoder_width_hz_{200.0};
  double decoder_wpm_{20.0};
  double decoder_snr_db_{0.0};
  double decoder_tone_hz_{700.0};
  double decoder_confidence_{0.0};
  bool decoder_key_down_{false};
};

}  // namespace cwassistant::desktop
