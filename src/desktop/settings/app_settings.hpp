#pragma once

#include <QObject>
#include <QList>
#include <QMediaDevices>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <cstdint>
#include <optional>

#include "cwassistant/core/frequency_plan.hpp"

namespace cwassistant::desktop {

class Cat4OmClient;
class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList referenceRigNames READ referenceRigNames CONSTANT)
  Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
  Q_PROPERTY(QStringList availableProfiles READ availableProfiles NOTIFY profilesChanged)
  Q_PROPERTY(bool profileSelectionRequired READ profileSelectionRequired NOTIFY profileSelectionRequiredChanged)
  Q_PROPERTY(bool setupComplete READ setupComplete NOTIFY setupCompleteChanged)
  Q_PROPERTY(QStringList serialPorts READ serialPorts NOTIFY serialPortsChanged)
  Q_PROPERTY(QStringList audioInputNames READ audioInputNames NOTIFY audioInputsChanged)
  Q_PROPERTY(int audioInputIndex READ audioInputIndex NOTIFY audioInputsChanged)
  Q_PROPERTY(QString audioInputDisplayName READ audioInputDisplayName NOTIFY audioInputsChanged)
  Q_PROPERTY(bool audioDcRejection READ audioDcRejection WRITE setAudioDcRejection NOTIFY settingsChanged)
  Q_PROPERTY(bool audioAutomaticGain READ audioAutomaticGain WRITE setAudioAutomaticGain NOTIFY settingsChanged)
  Q_PROPERTY(double audioGainDb READ audioGainDb WRITE setAudioGainDb NOTIFY settingsChanged)
  Q_PROPERTY(double audioAutomaticGainTargetDbfs READ audioAutomaticGainTargetDbfs WRITE setAudioAutomaticGainTargetDbfs NOTIFY settingsChanged)
  Q_PROPERTY(bool audioAutomaticBandwidth READ audioAutomaticBandwidth WRITE setAudioAutomaticBandwidth NOTIFY settingsChanged)
  Q_PROPERTY(double audioLowerFrequencyHz READ audioLowerFrequencyHz WRITE setAudioLowerFrequencyHz NOTIFY settingsChanged)
  Q_PROPERTY(double audioUpperFrequencyHz READ audioUpperFrequencyHz WRITE setAudioUpperFrequencyHz NOTIFY settingsChanged)
  Q_PROPERTY(bool audioInputRadioLinked READ audioInputRadioLinked WRITE setAudioInputRadioLinked NOTIFY settingsChanged)
  Q_PROPERTY(QString ownCallsign READ ownCallsign WRITE setOwnCallsign NOTIFY settingsChanged)
  Q_PROPERTY(bool omniRigAvailable READ omniRigAvailable CONSTANT)
  Q_PROPERTY(bool radioEnabled READ radioEnabled WRITE setRadioEnabled NOTIFY settingsChanged)
  Q_PROPERTY(QString radioDisplayName READ radioDisplayName NOTIFY settingsChanged)
  Q_PROPERTY(QStringList detectedRadioNames READ detectedRadioNames NOTIFY detectedRadiosChanged)
  Q_PROPERTY(int detectedRadioIndex READ detectedRadioIndex NOTIFY settingsChanged)
  Q_PROPERTY(int referenceRigIndex READ referenceRigIndex NOTIFY settingsChanged)
  Q_PROPERTY(int frequencyBackendIndex READ frequencyBackendIndex WRITE setFrequencyBackendIndex NOTIFY settingsChanged)
  Q_PROPERTY(int omniRigSlot READ omniRigSlot WRITE setOmniRigSlot NOTIFY settingsChanged)
  Q_PROPERTY(QString cat4omUrl READ cat4omUrl WRITE setCat4omUrl NOTIFY settingsChanged)
  Q_PROPERTY(QString cat4omRadioId READ cat4omRadioId WRITE setCat4omRadioId NOTIFY settingsChanged)
  Q_PROPERTY(QString cat4omPassword READ cat4omPassword WRITE setCat4omPassword NOTIFY settingsChanged)
  Q_PROPERTY(QString cat4omState READ cat4omState NOTIFY cat4omChanged)
  Q_PROPERTY(QString cat4omFrequencySummary READ cat4omFrequencySummary NOTIFY cat4omChanged)
  Q_PROPERTY(bool cat4omCanWrite READ cat4omCanWrite NOTIFY cat4omChanged)
  Q_PROPERTY(QString catPort READ catPort WRITE setCatPort NOTIFY settingsChanged)
  Q_PROPERTY(int catBaudRate READ catBaudRate WRITE setCatBaudRate NOTIFY settingsChanged)
  Q_PROPERTY(int catDataBits READ catDataBits WRITE setCatDataBits NOTIFY settingsChanged)
  Q_PROPERTY(int catParityIndex READ catParityIndex WRITE setCatParityIndex NOTIFY settingsChanged)
  Q_PROPERTY(int catStopBits READ catStopBits WRITE setCatStopBits NOTIFY settingsChanged)
  Q_PROPERTY(int catFlowControlIndex READ catFlowControlIndex WRITE setCatFlowControlIndex NOTIFY settingsChanged)
  Q_PROPERTY(int pollIntervalMs READ pollIntervalMs WRITE setPollIntervalMs NOTIFY settingsChanged)
  Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY settingsChanged)
  Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY settingsChanged)
  Q_PROPERTY(qint64 rxTransverterOffsetHz READ rxTransverterOffsetHz WRITE setRxTransverterOffsetHz NOTIFY settingsChanged)
  Q_PROPERTY(qint64 txTransverterOffsetHz READ txTransverterOffsetHz WRITE setTxTransverterOffsetHz NOTIFY settingsChanged)
  Q_PROPERTY(int cwToneSidebandIndex READ cwToneSidebandIndex WRITE setCwToneSidebandIndex NOTIFY settingsChanged)
  Q_PROPERTY(QString keyingPort READ keyingPort WRITE setKeyingPort NOTIFY settingsChanged)
  Q_PROPERTY(int pttLineIndex READ pttLineIndex WRITE setPttLineIndex NOTIFY settingsChanged)
  Q_PROPERTY(int keyLineIndex READ keyLineIndex WRITE setKeyLineIndex NOTIFY settingsChanged)
  Q_PROPERTY(bool pttActiveHigh READ pttActiveHigh WRITE setPttActiveHigh NOTIFY settingsChanged)
  Q_PROPERTY(bool keyActiveHigh READ keyActiveHigh WRITE setKeyActiveHigh NOTIFY settingsChanged)
  Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps NOTIFY settingsChanged)
  Q_PROPERTY(int waterfallRate READ waterfallRate WRITE setWaterfallRate NOTIFY settingsChanged)
  Q_PROPERTY(int waterfallTimeSpanSeconds READ waterfallTimeSpanSeconds WRITE setWaterfallTimeSpanSeconds NOTIFY settingsChanged)
  Q_PROPERTY(int spectrumDisplayMode READ spectrumDisplayMode WRITE setSpectrumDisplayMode NOTIFY settingsChanged)
  Q_PROPERTY(bool automaticRange READ automaticRange WRITE setAutomaticRange NOTIFY settingsChanged)
  Q_PROPERTY(double lowerBoundDb READ lowerBoundDb WRITE setLowerBoundDb NOTIFY settingsChanged)
  Q_PROPERTY(double upperBoundDb READ upperBoundDb WRITE setUpperBoundDb NOTIFY settingsChanged)
  Q_PROPERTY(double automaticRangeSpanDb READ automaticRangeSpanDb WRITE setAutomaticRangeSpanDb NOTIFY settingsChanged)
  Q_PROPERTY(bool waterfallNoiseSuppression READ waterfallNoiseSuppression WRITE setWaterfallNoiseSuppression NOTIFY settingsChanged)
  Q_PROPERTY(double waterfallNoiseMarginDb READ waterfallNoiseMarginDb WRITE setWaterfallNoiseMarginDb NOTIFY settingsChanged)
  Q_PROPERTY(bool showCwGuide READ showCwGuide WRITE setShowCwGuide NOTIFY settingsChanged)
  Q_PROPERTY(double cwGuideCenterHz READ cwGuideCenterHz WRITE setCwGuideCenterHz NOTIFY settingsChanged)
  Q_PROPERTY(double cwGuideWidthHz READ cwGuideWidthHz WRITE setCwGuideWidthHz NOTIFY settingsChanged)
  Q_PROPERTY(int averagingFrames READ averagingFrames WRITE setAveragingFrames NOTIFY settingsChanged)
  Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY settingsChanged)
  Q_PROPERTY(int decodedSignalTimeoutSeconds READ decodedSignalTimeoutSeconds WRITE setDecodedSignalTimeoutSeconds NOTIFY settingsChanged)
  Q_PROPERTY(bool localDecoderEnabled READ localDecoderEnabled WRITE setLocalDecoderEnabled NOTIFY settingsChanged)
  Q_PROPERTY(QString localDecoderModelPath READ localDecoderModelPath NOTIFY settingsChanged)
  Q_PROPERTY(QString localDecoderMetadataPath READ localDecoderMetadataPath NOTIFY settingsChanged)
  Q_PROPERTY(bool localDecoderBackendAvailable READ localDecoderBackendAvailable CONSTANT)
  Q_PROPERTY(QString localDecoderStatus READ localDecoderStatus NOTIFY settingsChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

 public:
  explicit AppSettings(QString profile_name, bool profile_was_explicit,
                       QObject* parent = nullptr);
  ~AppSettings() override;

  [[nodiscard]] QStringList referenceRigNames() const;
  [[nodiscard]] const QString& profileName() const noexcept;
  [[nodiscard]] const QStringList& availableProfiles() const noexcept;
  [[nodiscard]] bool profileSelectionRequired() const noexcept;
  [[nodiscard]] bool setupComplete() const noexcept;
  [[nodiscard]] const QStringList& serialPorts() const noexcept;
  [[nodiscard]] const QStringList& audioInputNames() const noexcept;
  [[nodiscard]] int audioInputIndex() const noexcept;
  [[nodiscard]] QString audioInputDisplayName() const;
  [[nodiscard]] const QString& audioInputId() const noexcept;
  [[nodiscard]] bool audioDcRejection() const noexcept;
  [[nodiscard]] bool audioAutomaticGain() const noexcept;
  [[nodiscard]] double audioGainDb() const noexcept;
  [[nodiscard]] double audioAutomaticGainTargetDbfs() const noexcept;
  [[nodiscard]] bool audioAutomaticBandwidth() const noexcept;
  [[nodiscard]] double audioLowerFrequencyHz() const noexcept;
  [[nodiscard]] double audioUpperFrequencyHz() const noexcept;
  [[nodiscard]] bool audioInputRadioLinked() const noexcept;
  [[nodiscard]] const QString& ownCallsign() const noexcept;
  [[nodiscard]] bool omniRigAvailable() const noexcept;
  [[nodiscard]] bool radioEnabled() const noexcept;
  [[nodiscard]] QString radioDisplayName() const;
  [[nodiscard]] const QStringList& detectedRadioNames() const noexcept;
  [[nodiscard]] int detectedRadioIndex() const noexcept;
  [[nodiscard]] int referenceRigIndex() const noexcept;
  [[nodiscard]] int frequencyBackendIndex() const noexcept;
  [[nodiscard]] int omniRigSlot() const noexcept;
  [[nodiscard]] const QString& cat4omUrl() const noexcept;
  [[nodiscard]] const QString& cat4omRadioId() const noexcept;
  [[nodiscard]] const QString& cat4omPassword() const noexcept;
  [[nodiscard]] QString cat4omState() const;
  [[nodiscard]] QString cat4omFrequencySummary() const;
  [[nodiscard]] bool cat4omCanWrite() const noexcept;
  [[nodiscard]] const QString& catPort() const noexcept;
  [[nodiscard]] int catBaudRate() const noexcept;
  [[nodiscard]] int catDataBits() const noexcept;
  [[nodiscard]] int catParityIndex() const noexcept;
  [[nodiscard]] int catStopBits() const noexcept;
  [[nodiscard]] int catFlowControlIndex() const noexcept;
  [[nodiscard]] int pollIntervalMs() const noexcept;
  [[nodiscard]] int timeoutMs() const noexcept;
  [[nodiscard]] bool splitEnabled() const noexcept;
  [[nodiscard]] qint64 rxTransverterOffsetHz() const noexcept;
  [[nodiscard]] qint64 txTransverterOffsetHz() const noexcept;
  [[nodiscard]] int cwToneSidebandIndex() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> controlledRxRfHz() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> controlledTxRfHz() const noexcept;
  [[nodiscard]] bool controlledSplitActive() const noexcept;
  [[nodiscard]] const QString& keyingPort() const noexcept;
  [[nodiscard]] int pttLineIndex() const noexcept;
  [[nodiscard]] int keyLineIndex() const noexcept;
  [[nodiscard]] bool pttActiveHigh() const noexcept;
  [[nodiscard]] bool keyActiveHigh() const noexcept;
  [[nodiscard]] int targetFps() const noexcept;
  [[nodiscard]] int waterfallRate() const noexcept;
  [[nodiscard]] int waterfallTimeSpanSeconds() const noexcept;
  [[nodiscard]] int spectrumDisplayMode() const noexcept;
  [[nodiscard]] bool automaticRange() const noexcept;
  [[nodiscard]] double lowerBoundDb() const noexcept;
  [[nodiscard]] double upperBoundDb() const noexcept;
  [[nodiscard]] double automaticRangeSpanDb() const noexcept;
  [[nodiscard]] bool waterfallNoiseSuppression() const noexcept;
  [[nodiscard]] double waterfallNoiseMarginDb() const noexcept;
  [[nodiscard]] bool showCwGuide() const noexcept;
  [[nodiscard]] double cwGuideCenterHz() const noexcept;
  [[nodiscard]] double cwGuideWidthHz() const noexcept;
  [[nodiscard]] int averagingFrames() const noexcept;
  [[nodiscard]] bool showGrid() const noexcept;
  [[nodiscard]] int decodedSignalTimeoutSeconds() const noexcept;
  [[nodiscard]] bool localDecoderEnabled() const noexcept;
  [[nodiscard]] const QString& localDecoderModelPath() const noexcept;
  [[nodiscard]] const QString& localDecoderMetadataPath() const noexcept;
  [[nodiscard]] bool localDecoderBackendAvailable() const noexcept;
  [[nodiscard]] QString localDecoderStatus() const;
  [[nodiscard]] const QString& statusMessage() const noexcept;

  void setFrequencyBackendIndex(int value);
  void setAudioDcRejection(bool value);
  void setAudioAutomaticGain(bool value);
  void setAudioGainDb(double value);
  void setAudioAutomaticGainTargetDbfs(double value);
  void setAudioAutomaticBandwidth(bool value);
  void setAudioLowerFrequencyHz(double value);
  void setAudioUpperFrequencyHz(double value);
  void setAudioInputRadioLinked(bool value);
  void setOwnCallsign(const QString& value);
  void setRadioEnabled(bool value);
  void setOmniRigSlot(int value);
  void setCat4omUrl(const QString& value);
  void setCat4omRadioId(const QString& value);
  void setCat4omPassword(const QString& value);
  void setCatPort(const QString& value);
  void setCatBaudRate(int value);
  void setCatDataBits(int value);
  void setCatParityIndex(int value);
  void setCatStopBits(int value);
  void setCatFlowControlIndex(int value);
  void setPollIntervalMs(int value);
  void setTimeoutMs(int value);
  void setSplitEnabled(bool value);
  void setRxTransverterOffsetHz(qint64 value);
  void setTxTransverterOffsetHz(qint64 value);
  void setCwToneSidebandIndex(int value);
  void setKeyingPort(const QString& value);
  void setPttLineIndex(int value);
  void setKeyLineIndex(int value);
  void setPttActiveHigh(bool value);
  void setKeyActiveHigh(bool value);
  void setTargetFps(int value);
  void setWaterfallRate(int value);
  void setWaterfallTimeSpanSeconds(int value);
  void setSpectrumDisplayMode(int value);
  void setAutomaticRange(bool value);
  void setLowerBoundDb(double value);
  void setUpperBoundDb(double value);
  void setAutomaticRangeSpanDb(double value);
  void setWaterfallNoiseSuppression(bool value);
  void setWaterfallNoiseMarginDb(double value);
  void setShowCwGuide(bool value);
  void setCwGuideCenterHz(double value);
  void setCwGuideWidthHz(double value);
  void setAveragingFrames(int value);
  void setShowGrid(bool value);
  void setDecodedSignalTimeoutSeconds(int value);
  void setLocalDecoderEnabled(bool value);

  Q_INVOKABLE void selectReferenceRig(int index);
  Q_INVOKABLE void resetToReferenceDefaults();
  Q_INVOKABLE void refreshSerialPorts();
  Q_INVOKABLE void refreshAudioInputs();
  Q_INVOKABLE void selectAudioInput(int index);
  Q_INVOKABLE void refreshDetectedRadios();
  Q_INVOKABLE void selectDetectedRadio(int index);
  Q_INVOKABLE bool apply();
  Q_INVOKABLE bool selectLocalDecoderModel(const QUrl& url);
  Q_INVOKABLE bool selectLocalDecoderMetadata(const QUrl& url);
  Q_INVOKABLE void clearLocalDecoderModel();
  Q_INVOKABLE void clearLocalDecoderMetadata();
  Q_INVOKABLE bool completeSetup();
  Q_INVOKABLE bool selectProfile(const QString& profile_name);
  Q_INVOKABLE bool createProfile(const QString& profile_name);
  Q_INVOKABLE void showOmniRigConfiguration();
  Q_INVOKABLE void testCat4omConnection();
  Q_INVOKABLE void connectCat4omControl();
  Q_INVOKABLE void disconnectCat4om();
  Q_INVOKABLE void requestCat4omOwnership();

 signals:
  void settingsChanged();
  void serialPortsChanged();
  void audioInputsChanged();
  void statusMessageChanged();
  void setupCompleteChanged();
  void profileChanged();
  void profilesChanged();
  void profileSelectionRequiredChanged();
  void cat4omChanged();
  void radioFrequencyChanged();
  void detectedRadiosChanged();
  void localDecoderConfigurationCommitted(bool enabled,
                                          const QString& model_path,
                                          const QString& metadata_path);

 private:
  void load();
  void applyReferenceDefaults(int index);
  void setStatusMessage(QString message);
  [[nodiscard]] QString storageKey(const QString& relative) const;
  [[nodiscard]] static QString normalizeProfileKey(const QString& name);
  void refreshProfiles();
  void resetInMemorySettings();
  void refreshControlledFrequency();
  [[nodiscard]] std::optional<cwassistant::core::ResolvedFrequencies>
  resolvedControlledFrequencies() const noexcept;
#ifdef Q_OS_WIN
  [[nodiscard]] bool ensureOmniRigAutomation();
#endif

  QString profile_name_;
  QString profile_storage_key_;
  QStringList available_profiles_;
  bool profile_selection_required_{false};
  bool setup_complete_{false};
  QStringList serial_ports_;
  QStringList audio_input_names_;
  QStringList audio_input_ids_;
  QString audio_input_id_;
  QString audio_input_name_;
  bool audio_dc_rejection_{true};
  bool audio_automatic_gain_{false};
  double audio_gain_db_{0.0};
  double audio_automatic_gain_target_dbfs_{-12.0};
  bool audio_automatic_bandwidth_{true};
  double audio_lower_frequency_hz_{100.0};
  double audio_upper_frequency_hz_{3'000.0};
  bool audio_input_radio_linked_{false};
  QString own_callsign_;
  bool radio_enabled_{false};
  QStringList detected_radio_names_;
  QList<int> detected_radio_slots_;
  int reference_rig_index_{0};
  int frequency_backend_index_{0};
  int omnirig_slot_{1};
  QString cat4om_url_{QStringLiteral("ws://127.0.0.1:5001/")};
  QString cat4om_radio_id_;
  QString cat4om_password_;
  QString cat_port_;
  int cat_baud_rate_{4'800};
  int cat_data_bits_{8};
  int cat_parity_index_{0};
  int cat_stop_bits_{1};
  int cat_flow_control_index_{0};
  int poll_interval_ms_{500};
  int timeout_ms_{4'000};
  bool split_enabled_{false};
  qint64 rx_transverter_offset_hz_{0};
  qint64 tx_transverter_offset_hz_{0};
  int cw_tone_sideband_index_{0};
  QString keying_port_;
  int ptt_line_index_{0};
  int key_line_index_{1};
  bool ptt_active_high_{true};
  bool key_active_high_{true};
  int target_fps_{60};
  int waterfall_rate_{60};
  int waterfall_time_span_seconds_{10};
  int spectrum_display_mode_{0};
  bool automatic_range_{true};
  double lower_bound_db_{-120.0};
  double upper_bound_db_{-20.0};
  double automatic_range_span_db_{60.0};
  bool waterfall_noise_suppression_{true};
  double waterfall_noise_margin_db_{6.0};
  bool show_cw_guide_{true};
  double cw_guide_center_hz_{700.0};
  double cw_guide_width_hz_{200.0};
  int averaging_frames_{3};
  bool show_grid_{true};
  int decoded_signal_timeout_seconds_{30};
  bool local_decoder_enabled_{false};
  QString local_decoder_model_path_;
  QString local_decoder_metadata_path_;
  QString status_message_;
  void* omnirig_automation_{nullptr};
  bool com_initialized_{false};
  bool com_initialization_attempted_{false};
  std::unique_ptr<Cat4OmClient> cat4om_client_;
  std::unique_ptr<QMediaDevices> media_devices_;
  QTimer radio_frequency_timer_;
  std::optional<std::uint64_t> omnirig_rx_dial_hz_;
};

}  // namespace cwassistant::desktop
