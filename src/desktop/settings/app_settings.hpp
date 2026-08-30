#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace cwassistant::desktop {

class AppSettings final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList referenceRigNames READ referenceRigNames CONSTANT)
  Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
  Q_PROPERTY(QStringList availableProfiles READ availableProfiles NOTIFY profilesChanged)
  Q_PROPERTY(bool profileSelectionRequired READ profileSelectionRequired NOTIFY profileSelectionRequiredChanged)
  Q_PROPERTY(bool setupComplete READ setupComplete NOTIFY setupCompleteChanged)
  Q_PROPERTY(QStringList serialPorts READ serialPorts NOTIFY serialPortsChanged)
  Q_PROPERTY(bool omniRigAvailable READ omniRigAvailable CONSTANT)
  Q_PROPERTY(int referenceRigIndex READ referenceRigIndex NOTIFY settingsChanged)
  Q_PROPERTY(int frequencyBackendIndex READ frequencyBackendIndex WRITE setFrequencyBackendIndex NOTIFY settingsChanged)
  Q_PROPERTY(int omniRigSlot READ omniRigSlot WRITE setOmniRigSlot NOTIFY settingsChanged)
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
  Q_PROPERTY(QString keyingPort READ keyingPort WRITE setKeyingPort NOTIFY settingsChanged)
  Q_PROPERTY(int pttLineIndex READ pttLineIndex WRITE setPttLineIndex NOTIFY settingsChanged)
  Q_PROPERTY(int keyLineIndex READ keyLineIndex WRITE setKeyLineIndex NOTIFY settingsChanged)
  Q_PROPERTY(bool pttActiveHigh READ pttActiveHigh WRITE setPttActiveHigh NOTIFY settingsChanged)
  Q_PROPERTY(bool keyActiveHigh READ keyActiveHigh WRITE setKeyActiveHigh NOTIFY settingsChanged)
  Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps NOTIFY settingsChanged)
  Q_PROPERTY(int waterfallRate READ waterfallRate WRITE setWaterfallRate NOTIFY settingsChanged)
  Q_PROPERTY(bool automaticRange READ automaticRange WRITE setAutomaticRange NOTIFY settingsChanged)
  Q_PROPERTY(double lowerBoundDb READ lowerBoundDb WRITE setLowerBoundDb NOTIFY settingsChanged)
  Q_PROPERTY(double upperBoundDb READ upperBoundDb WRITE setUpperBoundDb NOTIFY settingsChanged)
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
  [[nodiscard]] bool omniRigAvailable() const noexcept;
  [[nodiscard]] int referenceRigIndex() const noexcept;
  [[nodiscard]] int frequencyBackendIndex() const noexcept;
  [[nodiscard]] int omniRigSlot() const noexcept;
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
  [[nodiscard]] const QString& keyingPort() const noexcept;
  [[nodiscard]] int pttLineIndex() const noexcept;
  [[nodiscard]] int keyLineIndex() const noexcept;
  [[nodiscard]] bool pttActiveHigh() const noexcept;
  [[nodiscard]] bool keyActiveHigh() const noexcept;
  [[nodiscard]] int targetFps() const noexcept;
  [[nodiscard]] int waterfallRate() const noexcept;
  [[nodiscard]] bool automaticRange() const noexcept;
  [[nodiscard]] double lowerBoundDb() const noexcept;
  [[nodiscard]] double upperBoundDb() const noexcept;
  [[nodiscard]] const QString& statusMessage() const noexcept;

  void setFrequencyBackendIndex(int value);
  void setOmniRigSlot(int value);
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
  void setKeyingPort(const QString& value);
  void setPttLineIndex(int value);
  void setKeyLineIndex(int value);
  void setPttActiveHigh(bool value);
  void setKeyActiveHigh(bool value);
  void setTargetFps(int value);
  void setWaterfallRate(int value);
  void setAutomaticRange(bool value);
  void setLowerBoundDb(double value);
  void setUpperBoundDb(double value);

  Q_INVOKABLE void selectReferenceRig(int index);
  Q_INVOKABLE void resetToReferenceDefaults();
  Q_INVOKABLE void refreshSerialPorts();
  Q_INVOKABLE bool apply();
  Q_INVOKABLE bool completeSetup();
  Q_INVOKABLE bool selectProfile(const QString& profile_name);
  Q_INVOKABLE bool createProfile(const QString& profile_name);
  Q_INVOKABLE void showOmniRigConfiguration();

 signals:
  void settingsChanged();
  void serialPortsChanged();
  void statusMessageChanged();
  void setupCompleteChanged();
  void profileChanged();
  void profilesChanged();
  void profileSelectionRequiredChanged();

 private:
  void load();
  void applyReferenceDefaults(int index);
  void setStatusMessage(QString message);
  [[nodiscard]] QString storageKey(const QString& relative) const;
  [[nodiscard]] static QString normalizeProfileKey(const QString& name);
  void refreshProfiles();
  void resetInMemorySettings();

  QString profile_name_;
  QString profile_storage_key_;
  QStringList available_profiles_;
  bool profile_selection_required_{false};
  bool setup_complete_{false};
  QStringList serial_ports_;
  int reference_rig_index_{0};
  int frequency_backend_index_{0};
  int omnirig_slot_{1};
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
  QString keying_port_;
  int ptt_line_index_{0};
  int key_line_index_{1};
  bool ptt_active_high_{true};
  bool key_active_high_{true};
  int target_fps_{60};
  int waterfall_rate_{30};
  bool automatic_range_{true};
  double lower_bound_db_{-120.0};
  double upper_bound_db_{-20.0};
  QString status_message_;
  void* omnirig_automation_{nullptr};
  bool com_initialized_{false};
};

}  // namespace cwassistant::desktop
