#include "app_settings.hpp"

#include <QSettings>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <OleAuto.h>
#endif

#include "cwassistant/core/reference_rig_profiles.hpp"
#include "../radio/cat4om_client.hpp"

namespace cwassistant::desktop {
namespace {

constexpr auto kSchemaVersion = 1;

#ifdef Q_OS_WIN
constexpr long kOmniRigOnlineStatus = 4;

bool automation_property(IDispatch* object, const wchar_t* name,
                         VARIANT* value) {
  OLECHAR* property_name = const_cast<OLECHAR*>(name);
  DISPID property_id{};
  if (FAILED(object->GetIDsOfNames(IID_NULL, &property_name, 1,
                                   LOCALE_USER_DEFAULT, &property_id))) {
    return false;
  }
  DISPPARAMS parameters{nullptr, nullptr, 0, 0};
  VariantInit(value);
  return SUCCEEDED(object->Invoke(property_id, IID_NULL, LOCALE_USER_DEFAULT,
                                  DISPATCH_PROPERTYGET, &parameters, value,
                                  nullptr, nullptr));
}
#endif

template <typename T>
bool assign_if_changed(T& destination, const T& value) {
  if (destination == value) {
    return false;
  }
  destination = value;
  return true;
}

}  // namespace

AppSettings::AppSettings(QString profile_name, const bool profile_was_explicit,
                         QObject* parent)
    : QObject(parent), profile_name_(profile_name.trimmed()) {
  if (profile_name_.isEmpty()) {
    profile_name_ = QStringLiteral("default");
  }
  profile_storage_key_ = normalizeProfileKey(profile_name_);
  if (profile_storage_key_.isEmpty()) {
    profile_storage_key_ = QStringLiteral("default");
  }
  applyReferenceDefaults(0);
  load();
  refreshProfiles();
  profile_selection_required_ = !profile_was_explicit && available_profiles_.size() > 1;
  refreshSerialPorts();
  cat4om_client_ = std::make_unique<Cat4OmClient>(this);
  connect(cat4om_client_.get(), &Cat4OmClient::statusChanged, this, [this] {
    setStatusMessage(cat4om_client_->statusText());
    emit cat4omChanged();
  });
  connect(cat4om_client_.get(), &Cat4OmClient::radioStateChanged, this,
          &AppSettings::cat4omChanged);
}

AppSettings::~AppSettings() {
#ifdef Q_OS_WIN
  if (omnirig_automation_ != nullptr) {
    static_cast<IDispatch*>(omnirig_automation_)->Release();
  }
  if (com_initialized_) {
    CoUninitialize();
  }
#endif
}

QStringList AppSettings::referenceRigNames() const {
  QStringList names;
  for (const auto& profile : cwassistant::core::reference_rig_profiles()) {
    names.push_back(QString::fromStdString(profile.display_name));
  }
  return names;
}

const QString& AppSettings::profileName() const noexcept { return profile_name_; }
const QStringList& AppSettings::availableProfiles() const noexcept { return available_profiles_; }
bool AppSettings::profileSelectionRequired() const noexcept { return profile_selection_required_; }
bool AppSettings::setupComplete() const noexcept { return setup_complete_; }

const QStringList& AppSettings::serialPorts() const noexcept { return serial_ports_; }

bool AppSettings::omniRigAvailable() const noexcept {
#ifdef Q_OS_WIN
  CLSID class_id{};
  return SUCCEEDED(CLSIDFromProgID(L"OmniRig.OmniRigX", &class_id));
#else
  return false;
#endif
}

bool AppSettings::radioEnabled() const noexcept { return radio_enabled_; }

QString AppSettings::radioDisplayName() const {
  if (!radio_enabled_) {
    return QStringLiteral("No radio — receive-only (SWL)");
  }
  const auto detected_index = detected_radio_slots_.indexOf(omnirig_slot_);
  if (frequency_backend_index_ == 0 && detected_index >= 0) {
    return detected_radio_names_.at(detected_index);
  }
  const auto names = referenceRigNames();
  return names.value(reference_rig_index_, QStringLiteral("Manually configured radio"));
}

const QStringList& AppSettings::detectedRadioNames() const noexcept {
  return detected_radio_names_;
}

int AppSettings::detectedRadioIndex() const noexcept {
  return detected_radio_slots_.indexOf(omnirig_slot_);
}

int AppSettings::referenceRigIndex() const noexcept { return reference_rig_index_; }
int AppSettings::frequencyBackendIndex() const noexcept { return frequency_backend_index_; }
int AppSettings::omniRigSlot() const noexcept { return omnirig_slot_; }
const QString& AppSettings::cat4omUrl() const noexcept { return cat4om_url_; }
const QString& AppSettings::cat4omRadioId() const noexcept { return cat4om_radio_id_; }
const QString& AppSettings::cat4omPassword() const noexcept { return cat4om_password_; }
QString AppSettings::cat4omState() const {
  return cat4om_client_ ? cat4om_client_->statusText()
                        : QStringLiteral("Disconnected");
}
QString AppSettings::cat4omFrequencySummary() const {
  if (!cat4om_client_) {
    return QStringLiteral("No radio state");
  }
  const auto plan = cat4om_client_->frequencyPlan();
  if (!plan) {
    return QStringLiteral("No radio state");
  }
  return plan->split_enabled
             ? QStringLiteral("RX %1 Hz • TX %2 Hz • split")
                   .arg(static_cast<qulonglong>(plan->rx_dial_hz))
                   .arg(static_cast<qulonglong>(plan->tx_dial_hz))
             : QStringLiteral("%1 Hz • simplex")
                   .arg(static_cast<qulonglong>(plan->rx_dial_hz));
}
bool AppSettings::cat4omCanWrite() const noexcept {
  return cat4om_client_ && cat4om_client_->canWrite();
}
const QString& AppSettings::catPort() const noexcept { return cat_port_; }
int AppSettings::catBaudRate() const noexcept { return cat_baud_rate_; }
int AppSettings::catDataBits() const noexcept { return cat_data_bits_; }
int AppSettings::catParityIndex() const noexcept { return cat_parity_index_; }
int AppSettings::catStopBits() const noexcept { return cat_stop_bits_; }
int AppSettings::catFlowControlIndex() const noexcept { return cat_flow_control_index_; }
int AppSettings::pollIntervalMs() const noexcept { return poll_interval_ms_; }
int AppSettings::timeoutMs() const noexcept { return timeout_ms_; }
bool AppSettings::splitEnabled() const noexcept { return split_enabled_; }
qint64 AppSettings::rxTransverterOffsetHz() const noexcept { return rx_transverter_offset_hz_; }
qint64 AppSettings::txTransverterOffsetHz() const noexcept { return tx_transverter_offset_hz_; }
const QString& AppSettings::keyingPort() const noexcept { return keying_port_; }
int AppSettings::pttLineIndex() const noexcept { return ptt_line_index_; }
int AppSettings::keyLineIndex() const noexcept { return key_line_index_; }
bool AppSettings::pttActiveHigh() const noexcept { return ptt_active_high_; }
bool AppSettings::keyActiveHigh() const noexcept { return key_active_high_; }
int AppSettings::targetFps() const noexcept { return target_fps_; }
int AppSettings::waterfallRate() const noexcept { return waterfall_rate_; }
bool AppSettings::automaticRange() const noexcept { return automatic_range_; }
double AppSettings::lowerBoundDb() const noexcept { return lower_bound_db_; }
double AppSettings::upperBoundDb() const noexcept { return upper_bound_db_; }
int AppSettings::averagingFrames() const noexcept { return averaging_frames_; }
bool AppSettings::showGrid() const noexcept { return show_grid_; }
const QString& AppSettings::statusMessage() const noexcept { return status_message_; }

#define CWA_SETTER(Method, Member, Type)            \
  void AppSettings::Method(Type value) {            \
    if (assign_if_changed(Member, value)) {         \
      emit settingsChanged();                       \
    }                                               \
  }

CWA_SETTER(setFrequencyBackendIndex, frequency_backend_index_, int)
CWA_SETTER(setRadioEnabled, radio_enabled_, bool)
CWA_SETTER(setOmniRigSlot, omnirig_slot_, int)
CWA_SETTER(setCat4omUrl, cat4om_url_, const QString&)
CWA_SETTER(setCat4omRadioId, cat4om_radio_id_, const QString&)
CWA_SETTER(setCat4omPassword, cat4om_password_, const QString&)
CWA_SETTER(setCatPort, cat_port_, const QString&)
CWA_SETTER(setCatBaudRate, cat_baud_rate_, int)
CWA_SETTER(setCatDataBits, cat_data_bits_, int)
CWA_SETTER(setCatParityIndex, cat_parity_index_, int)
CWA_SETTER(setCatStopBits, cat_stop_bits_, int)
CWA_SETTER(setCatFlowControlIndex, cat_flow_control_index_, int)
CWA_SETTER(setPollIntervalMs, poll_interval_ms_, int)
CWA_SETTER(setTimeoutMs, timeout_ms_, int)
CWA_SETTER(setSplitEnabled, split_enabled_, bool)
CWA_SETTER(setRxTransverterOffsetHz, rx_transverter_offset_hz_, qint64)
CWA_SETTER(setTxTransverterOffsetHz, tx_transverter_offset_hz_, qint64)
CWA_SETTER(setKeyingPort, keying_port_, const QString&)
CWA_SETTER(setPttLineIndex, ptt_line_index_, int)
CWA_SETTER(setKeyLineIndex, key_line_index_, int)
CWA_SETTER(setPttActiveHigh, ptt_active_high_, bool)
CWA_SETTER(setKeyActiveHigh, key_active_high_, bool)
CWA_SETTER(setTargetFps, target_fps_, int)
CWA_SETTER(setWaterfallRate, waterfall_rate_, int)
CWA_SETTER(setAutomaticRange, automatic_range_, bool)
CWA_SETTER(setLowerBoundDb, lower_bound_db_, double)
CWA_SETTER(setUpperBoundDb, upper_bound_db_, double)
CWA_SETTER(setAveragingFrames, averaging_frames_, int)
CWA_SETTER(setShowGrid, show_grid_, bool)

#undef CWA_SETTER

void AppSettings::selectReferenceRig(const int index) {
  const auto profiles = cwassistant::core::reference_rig_profiles();
  if (index < 0 || static_cast<std::size_t>(index) >= profiles.size()) {
    return;
  }
  reference_rig_index_ = index;
  applyReferenceDefaults(index);
  setStatusMessage(QStringLiteral("Reference defaults loaded; all fields remain editable."));
  emit settingsChanged();
}

void AppSettings::resetToReferenceDefaults() {
  applyReferenceDefaults(reference_rig_index_);
  setStatusMessage(QStringLiteral("Radio defaults restored. Select Apply to persist them."));
  emit settingsChanged();
}

void AppSettings::applyReferenceDefaults(const int index) {
  const auto profiles = cwassistant::core::reference_rig_profiles();
  if (index < 0 || static_cast<std::size_t>(index) >= profiles.size()) {
    return;
  }
  const auto& profile = profiles[static_cast<std::size_t>(index)];
  cat_baud_rate_ = static_cast<int>(profile.cat.baud_rate);
  cat_data_bits_ = static_cast<int>(profile.cat.data_bits);
  cat_parity_index_ = static_cast<int>(profile.cat.parity);
  cat_stop_bits_ = static_cast<int>(profile.cat.stop_bits);
  cat_flow_control_index_ = static_cast<int>(profile.cat.flow_control);
  poll_interval_ms_ = static_cast<int>(profile.poll_interval_ms);
  timeout_ms_ = static_cast<int>(profile.timeout_ms);
  ptt_line_index_ = static_cast<int>(profile.ptt_line);
  key_line_index_ = static_cast<int>(profile.key_line);
  ptt_active_high_ = profile.keying.rts_active_high;
  key_active_high_ = profile.keying.dtr_active_high;
}

void AppSettings::refreshSerialPorts() {
  QStringList ports;
  for (const auto& port : QSerialPortInfo::availablePorts()) {
    ports.push_back(port.portName());
  }
  ports.removeDuplicates();
  ports.sort(Qt::CaseInsensitive);
  if (ports != serial_ports_) {
    serial_ports_ = ports;
    emit serialPortsChanged();
  }
  setStatusMessage(QStringLiteral("Serial ports refreshed without opening or toggling them."));
}

void AppSettings::refreshDetectedRadios() {
  QStringList names;
  QList<int> slots;
#ifdef Q_OS_WIN
  if (ensureOmniRigAutomation()) {
    auto* automation = static_cast<IDispatch*>(omnirig_automation_);
    for (int slot = 1; slot <= 2; ++slot) {
      VARIANT rig_value;
      const auto rig_property = slot == 1 ? L"Rig1" : L"Rig2";
      if (!automation_property(automation, rig_property, &rig_value)) {
        continue;
      }
      IDispatch* rig = rig_value.vt == VT_DISPATCH ? rig_value.pdispVal : nullptr;
      if (rig == nullptr) {
        VariantClear(&rig_value);
        continue;
      }

      VARIANT type_value;
      VARIANT status_value;
      const bool has_type = automation_property(rig, L"RigType", &type_value);
      const bool has_status = automation_property(rig, L"Status", &status_value);
      const QString rig_type =
          has_type && type_value.vt == VT_BSTR
              ? QString::fromWCharArray(type_value.bstrVal).trimmed()
              : QString{};
      const long status =
          has_status && (status_value.vt == VT_I4 || status_value.vt == VT_INT)
              ? status_value.lVal
              : -1;
      if (!rig_type.isEmpty() && status == kOmniRigOnlineStatus) {
        names.push_back(QStringLiteral("OmniRig %1 — %2").arg(slot).arg(rig_type));
        slots.push_back(slot);
      }
      if (has_type) {
        VariantClear(&type_value);
      }
      if (has_status) {
        VariantClear(&status_value);
      }
      VariantClear(&rig_value);
    }
  }
#endif
  const bool changed = names != detected_radio_names_ || slots != detected_radio_slots_;
  detected_radio_names_ = std::move(names);
  detected_radio_slots_ = std::move(slots);
  if (changed) {
    emit detectedRadiosChanged();
    emit settingsChanged();
  }
  setStatusMessage(detected_radio_names_.isEmpty()
                       ? QStringLiteral("No positively identified online radio was found. SWL and manual setup remain available.")
                       : QStringLiteral("Online radios refreshed without probing arbitrary serial ports."));
}

void AppSettings::selectDetectedRadio(const int index) {
  if (index < 0 || index >= detected_radio_slots_.size()) {
    return;
  }
  radio_enabled_ = true;
  frequency_backend_index_ = 0;
  omnirig_slot_ = detected_radio_slots_.at(index);
  setStatusMessage(QStringLiteral("Detected radio selected. Transmit remains disarmed."));
  emit settingsChanged();
}

bool AppSettings::apply() {
  frequency_backend_index_ = std::clamp(frequency_backend_index_, 0, 2);
  omnirig_slot_ = std::clamp(omnirig_slot_, 1, 2);
  cat_baud_rate_ = std::clamp(cat_baud_rate_, 300, 1'000'000);
  cat_data_bits_ = std::clamp(cat_data_bits_, 5, 8);
  cat_parity_index_ = std::clamp(cat_parity_index_, 0, 2);
  cat_stop_bits_ = std::clamp(cat_stop_bits_, 1, 2);
  cat_flow_control_index_ = std::clamp(cat_flow_control_index_, 0, 1);
  poll_interval_ms_ = std::clamp(poll_interval_ms_, 50, 10'000);
  timeout_ms_ = std::clamp(timeout_ms_, 100, 60'000);
  target_fps_ = std::clamp(target_fps_, 10, 120);
  waterfall_rate_ = std::clamp(waterfall_rate_, 1, 120);
  averaging_frames_ = std::clamp(averaging_frames_, 1, 32);
  if (upper_bound_db_ - lower_bound_db_ < 10.0) {
    upper_bound_db_ = lower_bound_db_ + 10.0;
  }

  if (radio_enabled_ && ptt_line_index_ == key_line_index_) {
    setStatusMessage(QStringLiteral("PTT and KEY must use different COM control lines."));
    emit settingsChanged();
    return false;
  }

  QSettings settings;
  settings.setValue(storageKey(QStringLiteral("configuration/schemaVersion")), kSchemaVersion);
  settings.setValue(storageKey(QStringLiteral("configuration/displayName")), profile_name_);
  settings.setValue(storageKey(QStringLiteral("radio/referenceRigIndex")), reference_rig_index_);
  settings.setValue(storageKey(QStringLiteral("radio/enabled")), radio_enabled_);
  settings.setValue(storageKey(QStringLiteral("radio/frequencyBackendIndex")), frequency_backend_index_);
  settings.setValue(storageKey(QStringLiteral("radio/omniRigSlot")), omnirig_slot_);
  settings.setValue(storageKey(QStringLiteral("radio/cat4omUrl")), cat4om_url_.trimmed());
  settings.setValue(storageKey(QStringLiteral("radio/cat4omRadioId")), cat4om_radio_id_.trimmed());
  settings.setValue(storageKey(QStringLiteral("radio/catPort")), cat_port_.trimmed());
  settings.setValue(storageKey(QStringLiteral("radio/catBaudRate")), cat_baud_rate_);
  settings.setValue(storageKey(QStringLiteral("radio/catDataBits")), cat_data_bits_);
  settings.setValue(storageKey(QStringLiteral("radio/catParityIndex")), cat_parity_index_);
  settings.setValue(storageKey(QStringLiteral("radio/catStopBits")), cat_stop_bits_);
  settings.setValue(storageKey(QStringLiteral("radio/catFlowControlIndex")), cat_flow_control_index_);
  settings.setValue(storageKey(QStringLiteral("radio/pollIntervalMs")), poll_interval_ms_);
  settings.setValue(storageKey(QStringLiteral("radio/timeoutMs")), timeout_ms_);
  settings.setValue(storageKey(QStringLiteral("radio/splitEnabled")), split_enabled_);
  settings.setValue(storageKey(QStringLiteral("radio/rxTransverterOffsetHz")), rx_transverter_offset_hz_);
  settings.setValue(storageKey(QStringLiteral("radio/txTransverterOffsetHz")), tx_transverter_offset_hz_);
  settings.setValue(storageKey(QStringLiteral("keying/port")), keying_port_.trimmed());
  settings.setValue(storageKey(QStringLiteral("keying/pttLineIndex")), ptt_line_index_);
  settings.setValue(storageKey(QStringLiteral("keying/keyLineIndex")), key_line_index_);
  settings.setValue(storageKey(QStringLiteral("keying/pttActiveHigh")), ptt_active_high_);
  settings.setValue(storageKey(QStringLiteral("keying/keyActiveHigh")), key_active_high_);
  settings.setValue(storageKey(QStringLiteral("display/targetFps")), target_fps_);
  settings.setValue(storageKey(QStringLiteral("display/waterfallRate")), waterfall_rate_);
  settings.setValue(storageKey(QStringLiteral("display/automaticRange")), automatic_range_);
  settings.setValue(storageKey(QStringLiteral("display/lowerBoundDb")), lower_bound_db_);
  settings.setValue(storageKey(QStringLiteral("display/upperBoundDb")), upper_bound_db_);
  settings.setValue(storageKey(QStringLiteral("display/averagingFrames")), averaging_frames_);
  settings.setValue(storageKey(QStringLiteral("display/showGrid")), show_grid_);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    setStatusMessage(QStringLiteral("Settings could not be written."));
    return false;
  }
  emit settingsChanged();
  setStatusMessage(QStringLiteral("Settings saved. Transmit remains disarmed."));
  refreshProfiles();
  return true;
}

void AppSettings::load() {
  QSettings settings;
  setup_complete_ = settings.value(storageKey(QStringLiteral("configuration/setupComplete")), false).toBool();
  radio_enabled_ = settings
                       .value(storageKey(QStringLiteral("radio/enabled")),
                              setup_complete_)
                       .toBool();
  const int saved_index = settings.value(storageKey(QStringLiteral("radio/referenceRigIndex")), 0).toInt();
  const auto profile_count = static_cast<int>(cwassistant::core::reference_rig_profiles().size());
  reference_rig_index_ = std::clamp(saved_index, 0, std::max(0, profile_count - 1));
  applyReferenceDefaults(reference_rig_index_);
  frequency_backend_index_ = settings.value(storageKey(QStringLiteral("radio/frequencyBackendIndex")), 0).toInt();
  omnirig_slot_ = settings.value(storageKey(QStringLiteral("radio/omniRigSlot")), 1).toInt();
  cat4om_url_ = settings
                    .value(storageKey(QStringLiteral("radio/cat4omUrl")),
                           QStringLiteral("ws://127.0.0.1:5001/"))
                    .toString();
  cat4om_radio_id_ =
      settings.value(storageKey(QStringLiteral("radio/cat4omRadioId"))).toString();
  cat4om_password_.clear();
  cat_port_ = settings.value(storageKey(QStringLiteral("radio/catPort"))).toString();
  cat_baud_rate_ = settings.value(storageKey(QStringLiteral("radio/catBaudRate")), cat_baud_rate_).toInt();
  cat_data_bits_ = settings.value(storageKey(QStringLiteral("radio/catDataBits")), cat_data_bits_).toInt();
  cat_parity_index_ = settings.value(storageKey(QStringLiteral("radio/catParityIndex")), cat_parity_index_).toInt();
  cat_stop_bits_ = settings.value(storageKey(QStringLiteral("radio/catStopBits")), cat_stop_bits_).toInt();
  cat_flow_control_index_ = settings.value(storageKey(QStringLiteral("radio/catFlowControlIndex")), cat_flow_control_index_).toInt();
  poll_interval_ms_ = settings.value(storageKey(QStringLiteral("radio/pollIntervalMs")), poll_interval_ms_).toInt();
  timeout_ms_ = settings.value(storageKey(QStringLiteral("radio/timeoutMs")), timeout_ms_).toInt();
  split_enabled_ = settings.value(storageKey(QStringLiteral("radio/splitEnabled")), false).toBool();
  rx_transverter_offset_hz_ = settings.value(storageKey(QStringLiteral("radio/rxTransverterOffsetHz")), 0).toLongLong();
  tx_transverter_offset_hz_ = settings.value(storageKey(QStringLiteral("radio/txTransverterOffsetHz")), 0).toLongLong();
  keying_port_ = settings.value(storageKey(QStringLiteral("keying/port"))).toString();
  ptt_line_index_ = settings.value(storageKey(QStringLiteral("keying/pttLineIndex")), ptt_line_index_).toInt();
  key_line_index_ = settings.value(storageKey(QStringLiteral("keying/keyLineIndex")), key_line_index_).toInt();
  ptt_active_high_ = settings.value(storageKey(QStringLiteral("keying/pttActiveHigh")), true).toBool();
  key_active_high_ = settings.value(storageKey(QStringLiteral("keying/keyActiveHigh")), true).toBool();
  target_fps_ = settings.value(storageKey(QStringLiteral("display/targetFps")), 60).toInt();
  waterfall_rate_ = settings.value(storageKey(QStringLiteral("display/waterfallRate")), 30).toInt();
  automatic_range_ = settings.value(storageKey(QStringLiteral("display/automaticRange")), true).toBool();
  lower_bound_db_ = settings.value(storageKey(QStringLiteral("display/lowerBoundDb")), -120.0).toDouble();
  upper_bound_db_ = settings.value(storageKey(QStringLiteral("display/upperBoundDb")), -20.0).toDouble();
  averaging_frames_ = settings.value(storageKey(QStringLiteral("display/averagingFrames")), 3).toInt();
  show_grid_ = settings.value(storageKey(QStringLiteral("display/showGrid")), true).toBool();
}

bool AppSettings::completeSetup() {
  if (!apply()) {
    return false;
  }
  QSettings settings;
  settings.setValue(storageKey(QStringLiteral("configuration/setupComplete")), true);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    setStatusMessage(QStringLiteral("Setup could not be completed because settings were not writable."));
    return false;
  }
  setup_complete_ = true;
  emit setupCompleteChanged();
  setStatusMessage(QStringLiteral("Station profile is ready. Transmit remains disarmed."));
  return true;
}

bool AppSettings::selectProfile(const QString& profile_name) {
  const QString key = normalizeProfileKey(profile_name);
  if (key.isEmpty()) {
    setStatusMessage(QStringLiteral("Select a valid station profile."));
    return false;
  }
  QSettings settings;
  if (!settings.contains(QStringLiteral("profiles/%1/configuration/schemaVersion").arg(key))) {
    setStatusMessage(QStringLiteral("The selected station profile does not exist."));
    return false;
  }
  profile_storage_key_ = key;
  profile_name_ = settings
                      .value(QStringLiteral("profiles/%1/configuration/displayName").arg(key),
                             profile_name)
                      .toString();
  resetInMemorySettings();
  load();
  profile_selection_required_ = false;
  emit profileChanged();
  emit setupCompleteChanged();
  emit profileSelectionRequiredChanged();
  emit settingsChanged();
  setStatusMessage(QStringLiteral("Station profile selected. Transmit remains disarmed."));
  return true;
}

bool AppSettings::createProfile(const QString& profile_name) {
  const QString trimmed = profile_name.trimmed();
  const QString key = normalizeProfileKey(trimmed);
  if (trimmed.isEmpty() || key.isEmpty()) {
    setStatusMessage(QStringLiteral("Enter a valid profile name."));
    return false;
  }
  QSettings settings;
  const QString schema_key =
      QStringLiteral("profiles/%1/configuration/schemaVersion").arg(key);
  if (settings.contains(schema_key)) {
    setStatusMessage(QStringLiteral("A profile with that name already exists."));
    return false;
  }
  profile_storage_key_ = key;
  profile_name_ = trimmed;
  resetInMemorySettings();
  settings.setValue(schema_key, kSchemaVersion);
  settings.setValue(storageKey(QStringLiteral("configuration/displayName")), profile_name_);
  settings.setValue(storageKey(QStringLiteral("configuration/setupComplete")), false);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    setStatusMessage(QStringLiteral("The new profile could not be created."));
    return false;
  }
  refreshProfiles();
  profile_selection_required_ = false;
  emit profileChanged();
  emit setupCompleteChanged();
  emit profileSelectionRequiredChanged();
  emit settingsChanged();
  setStatusMessage(QStringLiteral("New station profile created. Complete its setup."));
  return true;
}

QString AppSettings::storageKey(const QString& relative) const {
  return QStringLiteral("profiles/%1/%2").arg(profile_storage_key_, relative);
}

QString AppSettings::normalizeProfileKey(const QString& name) {
  QString result = name.trimmed().toLower();
  result.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")),
                 QStringLiteral("-"));
  while (result.contains(QStringLiteral("--"))) {
    result.replace(QStringLiteral("--"), QStringLiteral("-"));
  }
  return result.left(64);
}

void AppSettings::refreshProfiles() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("profiles"));
  const QStringList groups = settings.childGroups();
  settings.endGroup();
  QStringList profiles;
  for (const auto& group : groups) {
    profiles.push_back(
        settings.value(QStringLiteral("profiles/%1/configuration/displayName").arg(group), group)
            .toString());
  }
  if (!profiles.contains(profile_name_, Qt::CaseInsensitive)) {
    profiles.push_back(profile_name_);
  }
  profiles.sort(Qt::CaseInsensitive);
  if (profiles != available_profiles_) {
    available_profiles_ = profiles;
    emit profilesChanged();
  }
}

void AppSettings::resetInMemorySettings() {
  setup_complete_ = false;
  radio_enabled_ = false;
  reference_rig_index_ = 0;
  frequency_backend_index_ = 0;
  omnirig_slot_ = 1;
  cat4om_url_ = QStringLiteral("ws://127.0.0.1:5001/");
  cat4om_radio_id_.clear();
  cat4om_password_.clear();
  cat_port_.clear();
  keying_port_.clear();
  split_enabled_ = false;
  rx_transverter_offset_hz_ = 0;
  tx_transverter_offset_hz_ = 0;
  target_fps_ = 60;
  waterfall_rate_ = 30;
  automatic_range_ = true;
  lower_bound_db_ = -120.0;
  upper_bound_db_ = -20.0;
  averaging_frames_ = 3;
  show_grid_ = true;
  applyReferenceDefaults(0);
}

void AppSettings::setStatusMessage(QString message) {
  if (assign_if_changed(status_message_, message)) {
    emit statusMessageChanged();
  }
}

void AppSettings::showOmniRigConfiguration() {
#ifdef Q_OS_WIN
  if (!ensureOmniRigAutomation()) {
    setStatusMessage(QStringLiteral("OmniRig is not installed or could not be started."));
    return;
  }
  auto* automation = static_cast<IDispatch*>(omnirig_automation_);

  OLECHAR* property_name = const_cast<OLECHAR*>(L"DialogVisible");
  DISPID property_id{};
  HRESULT result = automation->GetIDsOfNames(IID_NULL, &property_name, 1,
                                              LOCALE_USER_DEFAULT, &property_id);
  VARIANT value;
  VariantInit(&value);
  value.vt = VT_BOOL;
  value.boolVal = VARIANT_TRUE;
  DISPID named_argument = DISPID_PROPERTYPUT;
  DISPPARAMS parameters{&value, &named_argument, 1, 1};
  if (SUCCEEDED(result)) {
    result = automation->Invoke(property_id, IID_NULL, LOCALE_USER_DEFAULT,
                                DISPATCH_PROPERTYPUT, &parameters, nullptr,
                                nullptr, nullptr);
  }
  setStatusMessage(SUCCEEDED(result)
                       ? QStringLiteral("OmniRig configuration opened. Match its live CAT values to this profile.")
                       : QStringLiteral("OmniRig configuration could not be opened."));
#else
  setStatusMessage(QStringLiteral("OmniRig integration is available on Windows; use Hamlib on this platform."));
#endif
}

#ifdef Q_OS_WIN
bool AppSettings::ensureOmniRigAutomation() {
  if (omnirig_automation_ != nullptr) {
    return true;
  }
  if (!com_initialization_attempted_) {
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    com_initialized_ = SUCCEEDED(init_result);
    com_initialization_attempted_ = true;
  }
  CLSID class_id{};
  HRESULT result = CLSIDFromProgID(L"OmniRig.OmniRigX", &class_id);
  IDispatch* automation = nullptr;
  if (SUCCEEDED(result)) {
    result = CoCreateInstance(class_id, nullptr, CLSCTX_LOCAL_SERVER,
                              IID_IDispatch,
                              reinterpret_cast<void**>(&automation));
  }
  if (FAILED(result) || automation == nullptr) {
    return false;
  }
  omnirig_automation_ = automation;
  return true;
}
#endif

void AppSettings::testCat4omConnection() {
  cat4om_client_->connectToServer(QUrl(cat4om_url_.trimmed()),
                                  cat4om_radio_id_, {}, true);
}

void AppSettings::connectCat4omControl() {
  cat4om_client_->connectToServer(QUrl(cat4om_url_.trimmed()),
                                  cat4om_radio_id_, cat4om_password_, false);
  cat4om_password_.clear();
  emit settingsChanged();
}

void AppSettings::disconnectCat4om() { cat4om_client_->disconnectFromServer(); }

void AppSettings::requestCat4omOwnership() {
  if (!cat4om_client_->requestOwnership()) {
    setStatusMessage(cat4om_client_->statusText());
  }
}

}  // namespace cwassistant::desktop
