#include "app_settings.hpp"

#include <QSettings>
#include <QSerialPortInfo>
#include <QAudioDevice>
#include <QFileInfo>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <OleAuto.h>
#endif

#include "cwassistant/core/reference_rig_profiles.hpp"
#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/frequency_plan.hpp"
#include "../radio/cat4om_client.hpp"

namespace cwassistant::desktop {
namespace {

constexpr auto kSchemaVersion = 1;

#ifdef Q_OS_WIN
constexpr long kOmniRigOnlineStatus = 4;
constexpr long kOmniRigReceiveState = 0x00200000;

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

std::optional<std::uint64_t> automation_frequency(const VARIANT& value) {
  switch (value.vt) {
    case VT_I4:
    case VT_INT:
      return value.lVal > 0
          ? std::optional<std::uint64_t>(value.lVal) : std::nullopt;
    case VT_UI4:
    case VT_UINT:
      return value.ulVal > 0
          ? std::optional<std::uint64_t>(value.ulVal) : std::nullopt;
    case VT_I8:
      return value.llVal > 0
          ? std::optional<std::uint64_t>(value.llVal) : std::nullopt;
    case VT_UI8:
      return value.ullVal > 0
          ? std::optional<std::uint64_t>(value.ullVal) : std::nullopt;
    case VT_R8:
      return value.dblVal > 0.0 && std::isfinite(value.dblVal)
          ? std::optional<std::uint64_t>(
                static_cast<std::uint64_t>(std::llround(value.dblVal)))
          : std::nullopt;
    default:
      return std::nullopt;
  }
}

std::optional<long> automation_integer(const VARIANT& value) {
  switch (value.vt) {
    case VT_I4:
    case VT_INT:
      return value.lVal;
    case VT_UI4:
    case VT_UINT:
      return value.ulVal <=
                     static_cast<unsigned long>(std::numeric_limits<LONG>::max())
                 ? std::optional<long>(static_cast<long>(value.ulVal))
                 : std::nullopt;
    default:
      return std::nullopt;
  }
}

cwassistant::core::OmniRigRxFrequencyTarget omni_rig_rx_write_target(
    IDispatch* rig) {
  VARIANT status_value;
  VARIANT writable_value;
  VARIANT vfo_value;
  VARIANT tx_value;
  const bool has_status = automation_property(rig, L"Status", &status_value);
  const bool has_writable =
      automation_property(rig, L"WriteableParams", &writable_value);
  const bool has_vfo = automation_property(rig, L"Vfo", &vfo_value);
  const bool has_tx = automation_property(rig, L"Tx", &tx_value);
  const auto status = has_status ? automation_integer(status_value)
                                 : std::nullopt;
  const auto writable = has_writable ? automation_integer(writable_value)
                                     : std::nullopt;
  const auto vfo = has_vfo ? automation_integer(vfo_value) : std::nullopt;
  const auto tx = has_tx ? automation_integer(tx_value) : std::nullopt;
  if (has_status) VariantClear(&status_value);
  if (has_writable) VariantClear(&writable_value);
  if (has_vfo) VariantClear(&vfo_value);
  if (has_tx) VariantClear(&tx_value);

  return cwassistant::core::select_omnirig_rx_frequency_target(
      status && *status == kOmniRigOnlineStatus,
      tx && *tx == kOmniRigReceiveState,
      writable ? static_cast<std::uint32_t>(*writable) : 0U,
      vfo ? static_cast<std::uint32_t>(*vfo) : 0U);
}

bool automation_put_frequency(IDispatch* object, const wchar_t* name,
                              const std::uint64_t frequency_hz) {
  if (frequency_hz == 0 ||
      frequency_hz >
          static_cast<std::uint64_t>(std::numeric_limits<LONG>::max())) {
    return false;
  }
  OLECHAR* property_name = const_cast<OLECHAR*>(name);
  DISPID property_id{};
  if (FAILED(object->GetIDsOfNames(IID_NULL, &property_name, 1,
                                   LOCALE_USER_DEFAULT, &property_id))) {
    return false;
  }
  VARIANT value;
  VariantInit(&value);
  value.vt = VT_I4;
  value.lVal = static_cast<LONG>(frequency_hz);
  DISPID named_argument = DISPID_PROPERTYPUT;
  DISPPARAMS parameters{&value, &named_argument, 1, 1};
  return SUCCEEDED(object->Invoke(property_id, IID_NULL, LOCALE_USER_DEFAULT,
                                  DISPATCH_PROPERTYPUT, &parameters, nullptr,
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
  media_devices_ = std::make_unique<QMediaDevices>();
  connect(media_devices_.get(), &QMediaDevices::audioInputsChanged, this,
          &AppSettings::refreshAudioInputs);
  refreshAudioInputs();
  cat4om_client_ = std::make_unique<Cat4OmClient>(this);
  connect(cat4om_client_.get(), &Cat4OmClient::statusChanged, this, [this] {
    setStatusMessage(cat4om_client_->statusText());
    emit cat4omChanged();
    emit radioFrequencyChanged();
    emit radioFrequencyControlChanged();
  });
  connect(cat4om_client_.get(), &Cat4OmClient::radioStateChanged, this, [this] {
    reconcilePendingRxFrequency();
    emit cat4omChanged();
    emit radioFrequencyChanged();
    emit radioFrequencyControlChanged();
  });
  radio_frequency_timer_.setInterval(200);
  connect(&radio_frequency_timer_, &QTimer::timeout, this,
          &AppSettings::refreshControlledFrequency);
  connect(this, &AppSettings::settingsChanged, this,
          &AppSettings::refreshControlledFrequency);
  connect(this, &AppSettings::settingsChanged, this,
          &AppSettings::radioFrequencyControlChanged);
  radio_frequency_timer_.start();
  radio_frequency_request_timer_.setSingleShot(true);
  radio_frequency_request_timer_.setInterval(2'000);
  connect(&radio_frequency_request_timer_, &QTimer::timeout, this, [this] {
    pending_rx_rf_hz_.reset();
    pending_frequency_backend_index_ = -1;
  });
  refreshControlledFrequency();
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

const QStringList& AppSettings::audioInputNames() const noexcept {
  return audio_input_names_;
}

int AppSettings::audioInputIndex() const noexcept {
  return audio_input_ids_.indexOf(audio_input_id_);
}

QString AppSettings::audioInputDisplayName() const {
  return audio_input_id_.isEmpty()
             ? QStringLiteral("System default input")
             : audio_input_name_;
}

const QString& AppSettings::audioInputId() const noexcept {
  return audio_input_id_;
}
bool AppSettings::audioDcRejection() const noexcept {
  return audio_dc_rejection_;
}
bool AppSettings::audioAutomaticGain() const noexcept {
  return audio_automatic_gain_;
}
double AppSettings::audioGainDb() const noexcept { return audio_gain_db_; }
double AppSettings::audioAutomaticGainTargetDbfs() const noexcept {
  return audio_automatic_gain_target_dbfs_;
}
bool AppSettings::audioAutomaticBandwidth() const noexcept {
  return audio_automatic_bandwidth_;
}
double AppSettings::audioLowerFrequencyHz() const noexcept {
  return audio_lower_frequency_hz_;
}
double AppSettings::audioUpperFrequencyHz() const noexcept {
  return audio_upper_frequency_hz_;
}
bool AppSettings::audioInputRadioLinked() const noexcept {
  return audio_input_radio_linked_;
}

const QString& AppSettings::ownCallsign() const noexcept {
  return own_callsign_;
}

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
int AppSettings::radioTuningStepHz() const noexcept {
  return radio_tuning_step_hz_;
}
bool AppSettings::radioFrequencyWritable() const noexcept {
  if (!radio_enabled_ || !audio_input_radio_linked_ ||
      !controlledRxRfHz().has_value()) {
    return false;
  }
  if (frequency_backend_index_ == 0) {
    return omnirig_rx_write_target_ !=
           cwassistant::core::OmniRigRxFrequencyTarget::None;
  }
  return frequency_backend_index_ == 2 && cat4om_client_ &&
         cat4om_client_->canSetFrequency();
}
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
int AppSettings::cwToneSidebandIndex() const noexcept {
  return cw_tone_sideband_index_;
}
std::optional<cwassistant::core::ResolvedFrequencies>
AppSettings::resolvedControlledFrequencies() const noexcept {
  if (!radio_enabled_ || !audio_input_radio_linked_) {
    return std::nullopt;
  }
  std::optional<cwassistant::core::VfoFrequencyPlan> plan;
  if (frequency_backend_index_ == 0 && omnirig_rx_dial_hz_) {
    plan = cwassistant::core::VfoFrequencyPlan{
        .rx_dial_hz = *omnirig_rx_dial_hz_,
        .tx_dial_hz = *omnirig_rx_dial_hz_,
        .split_enabled = false};
  } else if (frequency_backend_index_ == 2 && cat4om_client_) {
    plan = cat4om_client_->frequencyPlan();
  }
  if (!plan) return std::nullopt;
  return cwassistant::core::resolve_frequencies(
      *plan, {.rx_offset_hz = rx_transverter_offset_hz_,
              .tx_offset_hz = tx_transverter_offset_hz_});
}

std::optional<std::uint64_t> AppSettings::controlledRxRfHz() const noexcept {
  const auto resolved = resolvedControlledFrequencies();
  return resolved ? std::optional<std::uint64_t>(resolved->rx_rf_hz)
                  : std::nullopt;
}

std::optional<std::uint64_t> AppSettings::controlledTxRfHz() const noexcept {
  const auto resolved = resolvedControlledFrequencies();
  return resolved ? std::optional<std::uint64_t>(resolved->tx_rf_hz)
                  : std::nullopt;
}

bool AppSettings::controlledSplitActive() const noexcept {
  const auto resolved = resolvedControlledFrequencies();
  return resolved && resolved->split_enabled;
}

void AppSettings::refreshControlledFrequency() {
  std::optional<std::uint64_t> frequency;
  auto write_target = cwassistant::core::OmniRigRxFrequencyTarget::None;
#ifdef Q_OS_WIN
  if (radio_enabled_ && audio_input_radio_linked_ &&
      frequency_backend_index_ == 0 && ensureOmniRigAutomation()) {
    auto* automation = static_cast<IDispatch*>(omnirig_automation_);
    VARIANT rig_value;
    const auto property = omnirig_slot_ == 2 ? L"Rig2" : L"Rig1";
    if (automation_property(automation, property, &rig_value)) {
      IDispatch* rig = rig_value.vt == VT_DISPATCH
          ? rig_value.pdispVal : nullptr;
      if (rig != nullptr) {
        // Frequency readback stays responsive at 5 Hz. Capability/VFO
        // discovery crosses the out-of-process COM boundary four more times,
        // so cache that UI hint for one second. Every actual write performs a
        // fresh complete check and never relies on this cache for safety.
        if (omnirig_capability_refresh_clock_.isValid() &&
            omnirig_capability_refresh_clock_.elapsed() < 1'000) {
          write_target = omnirig_rx_write_target_;
        } else {
          write_target = omni_rig_rx_write_target(rig);
          omnirig_capability_refresh_clock_.restart();
        }
        VARIANT status_value;
        VARIANT frequency_value;
        const bool has_status =
            automation_property(rig, L"Status", &status_value);
        const bool has_frequency =
            automation_property(rig, L"Freq", &frequency_value);
        const long status = has_status &&
                                 (status_value.vt == VT_I4 ||
                                  status_value.vt == VT_INT)
            ? status_value.lVal : -1;
        if (status == kOmniRigOnlineStatus && has_frequency) {
          frequency = automation_frequency(frequency_value);
        }
        if (has_status) VariantClear(&status_value);
        if (has_frequency) VariantClear(&frequency_value);
      }
      VariantClear(&rig_value);
    }
  } else {
    omnirig_capability_refresh_clock_.invalidate();
  }
#endif
  if (frequency != omnirig_rx_dial_hz_ ||
      write_target != omnirig_rx_write_target_) {
    omnirig_rx_dial_hz_ = frequency;
    omnirig_rx_write_target_ = write_target;
    emit radioFrequencyChanged();
    emit radioFrequencyControlChanged();
  }
  reconcilePendingRxFrequency();
}

void AppSettings::reconcilePendingRxFrequency() {
  if (!pending_rx_rf_hz_ ||
      pending_frequency_backend_index_ != frequency_backend_index_) {
    return;
  }
  const auto reported = controlledRxRfHz();
  if (reported && *reported == *pending_rx_rf_hz_) {
    pending_rx_rf_hz_.reset();
    pending_frequency_backend_index_ = -1;
    radio_frequency_request_timer_.stop();
  }
}

void AppSettings::rememberPendingRxFrequency(
    const std::uint64_t frequency_hz) {
  pending_rx_rf_hz_ = frequency_hz;
  pending_frequency_backend_index_ = frequency_backend_index_;
  radio_frequency_request_timer_.start();
}

bool AppSettings::setControlledRxFrequency(const QString& value,
                                           const qulonglong unit_hz) {
  if (unit_hz != 1'000 && unit_hz != 1'000'000) {
    setStatusMessage(QStringLiteral("Choose a frequency in kHz or MHz."));
    return false;
  }
  const auto requested_rf = cwassistant::core::parse_frequency_value(
      value.toStdString(), static_cast<std::uint64_t>(unit_hz));
  if (!requested_rf) {
    setStatusMessage(
        unit_hz == 1'000
            ? QStringLiteral("Enter a positive frequency in kHz, with at most three decimal places.")
            : QStringLiteral("Enter a positive frequency in MHz, with at most six decimal places."));
    return false;
  }
  const auto dial_frequency = cwassistant::core::resolve_dial_frequency(
      *requested_rf, rx_transverter_offset_hz_);
  if (!dial_frequency) {
    setStatusMessage(QStringLiteral(
        "That actual RF frequency cannot be represented with the configured RX transverter offset."));
    return false;
  }
  if (!writeControlledRxDialFrequency(*dial_frequency)) {
    return false;
  }
  rememberPendingRxFrequency(*requested_rf);
  setStatusMessage(QStringLiteral(
      "RX frequency requested; provider readback remains authoritative. Split TX frequency and mode were not changed."));
  return true;
}

bool AppSettings::stepControlledRxFrequency(const int direction) {
  if (direction != -1 && direction != 1) {
    setStatusMessage(QStringLiteral("Frequency step direction must be down or up."));
    return false;
  }
  const auto current_rf = controlledRxRfHz();
  if (!current_rf || !radioFrequencyWritable()) {
    setStatusMessage(QStringLiteral(
        "RX frequency control requires a linked, online, writable radio provider."));
    return false;
  }
  const auto pending = pending_frequency_backend_index_ ==
                               frequency_backend_index_
                           ? pending_rx_rf_hz_
                           : std::nullopt;
  const auto requested_rf = cwassistant::core::step_rx_frequency(
      *current_rf, pending,
      static_cast<std::uint64_t>(radio_tuning_step_hz_), direction);
  if (!requested_rf) {
    setStatusMessage(direction < 0
                         ? QStringLiteral("The requested RX step would reach or cross zero hertz.")
                         : QStringLiteral("The requested RX step exceeds the supported frequency range."));
    return false;
  }
  const auto dial_frequency = cwassistant::core::resolve_dial_frequency(
      *requested_rf, rx_transverter_offset_hz_);
  if (!dial_frequency || !writeControlledRxDialFrequency(*dial_frequency)) {
    if (!dial_frequency) {
      setStatusMessage(QStringLiteral(
          "That RX step cannot be represented with the configured transverter offset."));
    }
    return false;
  }
  rememberPendingRxFrequency(*requested_rf);
  setStatusMessage(QStringLiteral(
      "RX stepped by %1 kHz; provider readback remains authoritative and split TX was not changed.")
                       .arg(radio_tuning_step_hz_ / 1'000));
  return true;
}

bool AppSettings::writeControlledRxDialFrequency(
    const std::uint64_t dial_frequency_hz) {
  if (!radioFrequencyWritable()) {
    setStatusMessage(QStringLiteral(
        "RX frequency control requires a linked, online, writable radio provider."));
    return false;
  }
  if (frequency_backend_index_ == 0) {
#ifdef Q_OS_WIN
    if (!writeOmniRigRxFrequency(dial_frequency_hz)) {
      setStatusMessage(QStringLiteral(
          "OmniRig did not accept the RX-frequency request. Confirm that the radio is online, receiving, and exposes a writable RX VFO."));
      return false;
    }
    return true;
#else
    setStatusMessage(QStringLiteral("OmniRig frequency control is available only on Windows."));
    return false;
#endif
  }
  if (frequency_backend_index_ == 2 && cat4om_client_ &&
      cat4om_client_->setRxFrequency(dial_frequency_hz)) {
    return true;
  }
  setStatusMessage(
      frequency_backend_index_ == 1
          ? QStringLiteral("Hamlib frequency control is not implemented yet.")
          : QStringLiteral("CAT4OM did not accept the RX-frequency request."));
  return false;
}
const QString& AppSettings::keyingPort() const noexcept { return keying_port_; }
int AppSettings::pttLineIndex() const noexcept { return ptt_line_index_; }
int AppSettings::keyLineIndex() const noexcept { return key_line_index_; }
bool AppSettings::pttActiveHigh() const noexcept { return ptt_active_high_; }
bool AppSettings::keyActiveHigh() const noexcept { return key_active_high_; }
int AppSettings::targetFps() const noexcept { return target_fps_; }
int AppSettings::waterfallRate() const noexcept { return waterfall_rate_; }
int AppSettings::waterfallTimeSpanSeconds() const noexcept {
  return waterfall_time_span_seconds_;
}
int AppSettings::spectrumDisplayMode() const noexcept {
  return spectrum_display_mode_;
}
bool AppSettings::automaticRange() const noexcept { return automatic_range_; }
double AppSettings::lowerBoundDb() const noexcept { return lower_bound_db_; }
double AppSettings::upperBoundDb() const noexcept { return upper_bound_db_; }
double AppSettings::automaticRangeSpanDb() const noexcept {
  return automatic_range_span_db_;
}
bool AppSettings::waterfallNoiseSuppression() const noexcept {
  return waterfall_noise_suppression_;
}
double AppSettings::waterfallNoiseMarginDb() const noexcept {
  return waterfall_noise_margin_db_;
}
bool AppSettings::showCwGuide() const noexcept { return show_cw_guide_; }
double AppSettings::cwGuideCenterHz() const noexcept {
  return cw_guide_center_hz_;
}
double AppSettings::cwGuideWidthHz() const noexcept {
  return cw_guide_width_hz_;
}
int AppSettings::averagingFrames() const noexcept { return averaging_frames_; }
bool AppSettings::showGrid() const noexcept { return show_grid_; }
int AppSettings::decodedSignalTimeoutSeconds() const noexcept {
  return decoded_signal_timeout_seconds_;
}
bool AppSettings::localDecoderEnabled() const noexcept {
  return local_decoder_enabled_;
}
const QString& AppSettings::localDecoderModelPath() const noexcept {
  return local_decoder_model_path_;
}
const QString& AppSettings::localDecoderMetadataPath() const noexcept {
  return local_decoder_metadata_path_;
}
bool AppSettings::localDecoderBackendAvailable() const noexcept {
#if defined(CWA_HAVE_ONNX_RUNTIME) && CWA_HAVE_ONNX_RUNTIME
  return true;
#else
  return false;
#endif
}
QString AppSettings::localDecoderStatus() const {
  if (!localDecoderBackendAvailable()) {
    return QStringLiteral(
        "Local model support is unavailable in this build. Deterministic decoding remains active.");
  }
  if (!local_decoder_enabled_) return QStringLiteral("Local model disabled.");
  if (local_decoder_model_path_.isEmpty() ||
      local_decoder_metadata_path_.isEmpty()) {
    return QStringLiteral("Select both a model and its metadata file.");
  }
  return QStringLiteral("Configuration ready to validate and load.");
}
bool AppSettings::localCallsignDatabaseEnabled() const noexcept {
  return local_callsign_database_enabled_;
}
const QString& AppSettings::localCallsignDatabasePath() const noexcept {
  return local_callsign_database_path_;
}
const QString& AppSettings::localCallsignDatabaseStatus() const noexcept {
  return local_callsign_database_status_;
}
const QString& AppSettings::statusMessage() const noexcept { return status_message_; }

#define CWA_SETTER(Method, Member, Type)            \
  void AppSettings::Method(Type value) {            \
    if (assign_if_changed(Member, value)) {         \
      emit settingsChanged();                       \
    }                                               \
  }

void AppSettings::setFrequencyBackendIndex(const int value) {
  if (assign_if_changed(frequency_backend_index_, value)) {
    pending_rx_rf_hz_.reset();
    pending_frequency_backend_index_ = -1;
    radio_frequency_request_timer_.stop();
    emit settingsChanged();
  }
}
CWA_SETTER(setAudioDcRejection, audio_dc_rejection_, bool)
CWA_SETTER(setAudioAutomaticGain, audio_automatic_gain_, bool)
CWA_SETTER(setAudioGainDb, audio_gain_db_, double)
CWA_SETTER(setAudioAutomaticGainTargetDbfs,
           audio_automatic_gain_target_dbfs_, double)
CWA_SETTER(setAudioAutomaticBandwidth, audio_automatic_bandwidth_, bool)
CWA_SETTER(setAudioLowerFrequencyHz, audio_lower_frequency_hz_, double)
CWA_SETTER(setAudioUpperFrequencyHz, audio_upper_frequency_hz_, double)
CWA_SETTER(setAudioInputRadioLinked, audio_input_radio_linked_, bool)
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
CWA_SETTER(setCwToneSidebandIndex, cw_tone_sideband_index_, int)
CWA_SETTER(setKeyingPort, keying_port_, const QString&)
CWA_SETTER(setPttLineIndex, ptt_line_index_, int)
CWA_SETTER(setKeyLineIndex, key_line_index_, int)
CWA_SETTER(setPttActiveHigh, ptt_active_high_, bool)
CWA_SETTER(setKeyActiveHigh, key_active_high_, bool)
CWA_SETTER(setTargetFps, target_fps_, int)
CWA_SETTER(setWaterfallRate, waterfall_rate_, int)
CWA_SETTER(setWaterfallTimeSpanSeconds, waterfall_time_span_seconds_, int)
CWA_SETTER(setSpectrumDisplayMode, spectrum_display_mode_, int)
CWA_SETTER(setAutomaticRange, automatic_range_, bool)
CWA_SETTER(setLowerBoundDb, lower_bound_db_, double)
CWA_SETTER(setUpperBoundDb, upper_bound_db_, double)
CWA_SETTER(setAutomaticRangeSpanDb, automatic_range_span_db_, double)
CWA_SETTER(setWaterfallNoiseSuppression, waterfall_noise_suppression_, bool)
CWA_SETTER(setWaterfallNoiseMarginDb, waterfall_noise_margin_db_, double)
CWA_SETTER(setShowCwGuide, show_cw_guide_, bool)
CWA_SETTER(setCwGuideCenterHz, cw_guide_center_hz_, double)
CWA_SETTER(setCwGuideWidthHz, cw_guide_width_hz_, double)
CWA_SETTER(setAveragingFrames, averaging_frames_, int)
CWA_SETTER(setShowGrid, show_grid_, bool)
CWA_SETTER(setDecodedSignalTimeoutSeconds, decoded_signal_timeout_seconds_, int)
CWA_SETTER(setLocalDecoderEnabled, local_decoder_enabled_, bool)

void AppSettings::setLocalCallsignDatabaseEnabled(const bool value) {
  if (!assign_if_changed(local_callsign_database_enabled_, value)) return;
  local_callsign_database_status_ = value
      ? (local_callsign_database_path_.isEmpty()
             ? QStringLiteral("Select a local master.scp or Call History text file.")
             : QStringLiteral("Loading the selected local callsign list."))
      : QStringLiteral("Disabled. No local callsign list is in use.");
  emit settingsChanged();
  emit localCallsignDatabaseChanged();
  emit localCallsignDatabaseConfigurationCommitted(
      local_callsign_database_enabled_, local_callsign_database_path_);
}

void AppSettings::setRadioTuningStepHz(const int value) {
  const int clamped = std::clamp(value, 1'000, 100'000);
  if (assign_if_changed(radio_tuning_step_hz_, clamped)) {
    emit settingsChanged();
  }
}

#undef CWA_SETTER

void AppSettings::setOwnCallsign(const QString& value) {
  const QString trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    if (assign_if_changed(own_callsign_, QString{})) {
      emit settingsChanged();
    }
    return;
  }
  const auto normalized =
      cwassistant::core::CallsignPolicy::normalize(trimmed.toStdString());
  if (!normalized.has_value()) {
    setStatusMessage(QStringLiteral(
        "Enter a valid callsign containing letters and digits; portable suffixes may use a single slash."));
    emit settingsChanged();
    return;
  }
  if (assign_if_changed(own_callsign_, QString::fromStdString(*normalized))) {
    setStatusMessage(QStringLiteral("Own callsign normalized and ready to save."));
    emit settingsChanged();
  }
}

namespace {

bool selectLocalFile(const QUrl& url, QString& destination) {
  if (!url.isLocalFile()) return false;
  const QFileInfo file(url.toLocalFile());
  if (!file.exists() || !file.isFile() || !file.isReadable()) return false;
  destination = file.canonicalFilePath();
  return !destination.isEmpty();
}

}  // namespace

bool AppSettings::selectLocalDecoderModel(const QUrl& url) {
  QString path;
  if (!selectLocalFile(url, path)) {
    setStatusMessage(QStringLiteral("Select a readable local model file."));
    return false;
  }
  if (assign_if_changed(local_decoder_model_path_, path)) emit settingsChanged();
  setStatusMessage(QStringLiteral("Local model selected. Apply to save."));
  return true;
}

bool AppSettings::selectLocalDecoderMetadata(const QUrl& url) {
  QString path;
  if (!selectLocalFile(url, path)) {
    setStatusMessage(QStringLiteral("Select a readable local metadata file."));
    return false;
  }
  if (assign_if_changed(local_decoder_metadata_path_, path))
    emit settingsChanged();
  setStatusMessage(QStringLiteral("Local model metadata selected. Apply to save."));
  return true;
}

void AppSettings::clearLocalDecoderModel() {
  if (assign_if_changed(local_decoder_model_path_, QString{}))
    emit settingsChanged();
}

void AppSettings::clearLocalDecoderMetadata() {
  if (assign_if_changed(local_decoder_metadata_path_, QString{}))
    emit settingsChanged();
}

bool AppSettings::selectLocalCallsignDatabase(const QUrl& url) {
  QString path;
  if (!selectLocalFile(url, path)) {
    local_callsign_database_status_ =
        QStringLiteral("Select a readable local text callsign-list file.");
    emit localCallsignDatabaseChanged();
    setStatusMessage(local_callsign_database_status_);
    return false;
  }
  if (assign_if_changed(local_callsign_database_path_, path))
    emit settingsChanged();
  local_callsign_database_status_ = QStringLiteral(
      "Local file selected; suggestions remain separate from decoded text.");
  emit localCallsignDatabaseChanged();
  if (local_callsign_database_enabled_) {
    emit localCallsignDatabaseConfigurationCommitted(
        true, local_callsign_database_path_);
  }
  setStatusMessage(QStringLiteral("Local callsign-list file selected. Apply to save."));
  return true;
}

void AppSettings::clearLocalCallsignDatabase() {
  if (assign_if_changed(local_callsign_database_path_, QString{}))
    emit settingsChanged();
  local_callsign_database_status_ = local_callsign_database_enabled_
      ? QStringLiteral("Select a local master.scp or Call History text file.")
      : QStringLiteral("Disabled. No local callsign list is in use.");
  emit localCallsignDatabaseChanged();
  emit localCallsignDatabaseConfigurationCommitted(
      local_callsign_database_enabled_, QString{});
}

bool AppSettings::reloadLocalCallsignDatabase() {
  if (!local_callsign_database_enabled_) {
    local_callsign_database_status_ =
        QStringLiteral("Enable the local callsign suggestion source before reloading it.");
    emit localCallsignDatabaseChanged();
    return false;
  }
  if (local_callsign_database_path_.isEmpty()) {
    local_callsign_database_status_ = QStringLiteral(
        "Select a local callsign-list file before reloading.");
    emit localCallsignDatabaseChanged();
    return false;
  }
  local_callsign_database_status_ = QStringLiteral("Reloading local callsign list.");
  emit localCallsignDatabaseChanged();
  emit localCallsignDatabaseConfigurationCommitted(
      true, local_callsign_database_path_);
  return true;
}

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

void AppSettings::refreshAudioInputs() {
  QStringList names{QStringLiteral("System default input (recommended)")};
  QStringList ids{QString{}};
  for (const auto& device : QMediaDevices::audioInputs()) {
    const QString id = QString::fromLatin1(
        device.id().toBase64(QByteArray::Base64UrlEncoding |
                             QByteArray::OmitTrailingEquals));
    if (id.isEmpty() || ids.contains(id)) {
      continue;
    }
    QString name = device.description().trimmed();
    if (name.isEmpty()) {
      name = QStringLiteral("Audio input %1").arg(ids.size());
    }
    if (device.isDefault()) {
      name += QStringLiteral(" (current default)");
    }
    names.push_back(name);
    ids.push_back(id);
  }

  if (!audio_input_id_.isEmpty() && !ids.contains(audio_input_id_)) {
    const QString unavailable_name =
        audio_input_name_.isEmpty() ? QStringLiteral("Previously selected input")
                                    : audio_input_name_;
    names.push_back(unavailable_name + QStringLiteral(" (unavailable)"));
    ids.push_back(audio_input_id_);
  }

  const int selected_index = ids.indexOf(audio_input_id_);
  if (selected_index > 0) {
    audio_input_name_ = names.at(selected_index);
    if (audio_input_name_.endsWith(QStringLiteral(" (unavailable)"))) {
      audio_input_name_.chop(QStringLiteral(" (unavailable)").size());
    }
  }

  if (names != audio_input_names_ || ids != audio_input_ids_) {
    audio_input_names_ = std::move(names);
    audio_input_ids_ = std::move(ids);
    emit audioInputsChanged();
  }
}

void AppSettings::selectAudioInput(const int index) {
  if (index < 0 || index >= audio_input_ids_.size()) {
    return;
  }
  audio_input_id_ = audio_input_ids_.at(index);
  audio_input_name_ = index == 0 ? QStringLiteral("System default input")
                                 : audio_input_names_.at(index);
  if (audio_input_name_.endsWith(QStringLiteral(" (unavailable)"))) {
    audio_input_name_.chop(QStringLiteral(" (unavailable)").size());
  }
  setStatusMessage(QStringLiteral("Audio input selected. Live capture remains disarmed until started by the operator."));
  emit audioInputsChanged();
  emit settingsChanged();
}

void AppSettings::refreshDetectedRadios() {
  QStringList names;
  QList<int> detected_slots;
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
        detected_slots.push_back(slot);
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
  const bool changed = names != detected_radio_names_ ||
                       detected_slots != detected_radio_slots_;
  detected_radio_names_ = std::move(names);
  detected_radio_slots_ = std::move(detected_slots);
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
  audio_gain_db_ = std::clamp(audio_gain_db_, -40.0, 40.0);
  audio_automatic_gain_target_dbfs_ =
      std::clamp(audio_automatic_gain_target_dbfs_, -40.0, -1.0);
  audio_lower_frequency_hz_ =
      std::clamp(audio_lower_frequency_hz_, 0.0, 96'000.0);
  audio_upper_frequency_hz_ =
      std::clamp(audio_upper_frequency_hz_, 50.0, 96'000.0);
  if (audio_upper_frequency_hz_ - audio_lower_frequency_hz_ < 50.0) {
    audio_upper_frequency_hz_ =
        std::min(96'000.0, audio_lower_frequency_hz_ + 50.0);
    audio_lower_frequency_hz_ =
        std::min(audio_lower_frequency_hz_, audio_upper_frequency_hz_ - 50.0);
  }
  frequency_backend_index_ = std::clamp(frequency_backend_index_, 0, 2);
  radio_tuning_step_hz_ =
      std::clamp(radio_tuning_step_hz_, 1'000, 100'000);
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
  waterfall_time_span_seconds_ =
      std::clamp(waterfall_time_span_seconds_, 5, 30);
  spectrum_display_mode_ = std::clamp(spectrum_display_mode_, 0, 1);
  averaging_frames_ = std::clamp(averaging_frames_, 1, 32);
  automatic_range_span_db_ =
      std::clamp(automatic_range_span_db_, 30.0, 100.0);
  waterfall_noise_margin_db_ =
      std::clamp(waterfall_noise_margin_db_, 0.0, 30.0);
  cw_guide_center_hz_ = std::clamp(cw_guide_center_hz_, 0.0, 96'000.0);
  cw_guide_width_hz_ = std::clamp(cw_guide_width_hz_, 10.0, 5'000.0);
  decoded_signal_timeout_seconds_ =
      std::clamp(decoded_signal_timeout_seconds_, 5, 300);
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
  settings.setValue(storageKey(QStringLiteral("audio/inputId")), audio_input_id_);
  settings.setValue(storageKey(QStringLiteral("audio/inputName")), audio_input_name_);
  settings.setValue(storageKey(QStringLiteral("audio/dcRejection")), audio_dc_rejection_);
  settings.setValue(storageKey(QStringLiteral("audio/automaticGain")), audio_automatic_gain_);
  settings.setValue(storageKey(QStringLiteral("audio/gainDb")), audio_gain_db_);
  settings.setValue(storageKey(QStringLiteral("audio/automaticGainTargetDbfs")), audio_automatic_gain_target_dbfs_);
  settings.setValue(storageKey(QStringLiteral("audio/automaticBandwidth")), audio_automatic_bandwidth_);
  settings.setValue(storageKey(QStringLiteral("audio/lowerFrequencyHz")), audio_lower_frequency_hz_);
  settings.setValue(storageKey(QStringLiteral("audio/upperFrequencyHz")), audio_upper_frequency_hz_);
  settings.setValue(storageKey(QStringLiteral("audio/inputRadioLinked")), audio_input_radio_linked_);
  settings.setValue(storageKey(QStringLiteral("station/ownCallsign")), own_callsign_);
  settings.setValue(storageKey(QStringLiteral("radio/referenceRigIndex")), reference_rig_index_);
  settings.setValue(storageKey(QStringLiteral("radio/enabled")), radio_enabled_);
  settings.setValue(storageKey(QStringLiteral("radio/frequencyBackendIndex")), frequency_backend_index_);
  settings.setValue(storageKey(QStringLiteral("radio/tuningStepHz")),
                    radio_tuning_step_hz_);
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
  settings.setValue(storageKey(QStringLiteral("radio/cwToneSidebandIndex")), cw_tone_sideband_index_);
  settings.setValue(storageKey(QStringLiteral("keying/port")), keying_port_.trimmed());
  settings.setValue(storageKey(QStringLiteral("keying/pttLineIndex")), ptt_line_index_);
  settings.setValue(storageKey(QStringLiteral("keying/keyLineIndex")), key_line_index_);
  settings.setValue(storageKey(QStringLiteral("keying/pttActiveHigh")), ptt_active_high_);
  settings.setValue(storageKey(QStringLiteral("keying/keyActiveHigh")), key_active_high_);
  settings.setValue(storageKey(QStringLiteral("display/targetFps")), target_fps_);
  settings.setValue(storageKey(QStringLiteral("display/waterfallRate")), waterfall_rate_);
  settings.setValue(storageKey(QStringLiteral("display/waterfallTimeSpanSeconds")), waterfall_time_span_seconds_);
  settings.setValue(storageKey(QStringLiteral("display/spectrumDisplayMode")), spectrum_display_mode_);
  settings.setValue(storageKey(QStringLiteral("display/automaticRange")), automatic_range_);
  settings.setValue(storageKey(QStringLiteral("display/lowerBoundDb")), lower_bound_db_);
  settings.setValue(storageKey(QStringLiteral("display/upperBoundDb")), upper_bound_db_);
  settings.setValue(storageKey(QStringLiteral("display/automaticRangeSpanDb")), automatic_range_span_db_);
  settings.setValue(storageKey(QStringLiteral("display/waterfallNoiseSuppression")), waterfall_noise_suppression_);
  settings.setValue(storageKey(QStringLiteral("display/waterfallNoiseMarginDb")), waterfall_noise_margin_db_);
  settings.setValue(storageKey(QStringLiteral("display/showCwGuide")), show_cw_guide_);
  settings.setValue(storageKey(QStringLiteral("display/cwGuideCenterHz")), cw_guide_center_hz_);
  settings.setValue(storageKey(QStringLiteral("display/cwGuideWidthHz")), cw_guide_width_hz_);
  settings.setValue(storageKey(QStringLiteral("display/averagingFrames")), averaging_frames_);
  settings.setValue(storageKey(QStringLiteral("display/showGrid")), show_grid_);
  settings.setValue(storageKey(QStringLiteral("display/decodedSignalTimeoutSeconds")),
                    decoded_signal_timeout_seconds_);
  settings.setValue(storageKey(QStringLiteral("decoder/localEnabled")),
                    local_decoder_enabled_);
  settings.setValue(storageKey(QStringLiteral("decoder/localModelPath")),
                    local_decoder_model_path_);
  settings.setValue(storageKey(QStringLiteral("decoder/localMetadataPath")),
                    local_decoder_metadata_path_);
  settings.setValue(storageKey(QStringLiteral("decoder/localCallsignDatabaseEnabled")),
                    local_callsign_database_enabled_);
  settings.setValue(storageKey(QStringLiteral("decoder/localCallsignDatabasePath")),
                    local_callsign_database_path_);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    setStatusMessage(QStringLiteral("Settings could not be written."));
    return false;
  }
  emit settingsChanged();
  emit localDecoderConfigurationCommitted(
      local_decoder_enabled_, local_decoder_model_path_,
      local_decoder_metadata_path_);
  setStatusMessage(QStringLiteral("Settings saved. Transmit remains disarmed."));
  refreshProfiles();
  return true;
}

void AppSettings::load() {
  QSettings settings;
  setup_complete_ = settings.value(storageKey(QStringLiteral("configuration/setupComplete")), false).toBool();
  audio_input_id_ =
      settings.value(storageKey(QStringLiteral("audio/inputId"))).toString();
  audio_input_name_ = settings
                          .value(storageKey(QStringLiteral("audio/inputName")),
                                 QStringLiteral("System default input"))
                          .toString();
  audio_dc_rejection_ =
      settings.value(storageKey(QStringLiteral("audio/dcRejection")), true).toBool();
  audio_automatic_gain_ =
      settings.value(storageKey(QStringLiteral("audio/automaticGain")), false).toBool();
  audio_gain_db_ =
      settings.value(storageKey(QStringLiteral("audio/gainDb")), 0.0).toDouble();
  audio_automatic_gain_target_dbfs_ =
      settings.value(storageKey(QStringLiteral("audio/automaticGainTargetDbfs")), -12.0).toDouble();
  audio_automatic_bandwidth_ =
      settings.value(storageKey(QStringLiteral("audio/automaticBandwidth")), true).toBool();
  audio_lower_frequency_hz_ =
      settings.value(storageKey(QStringLiteral("audio/lowerFrequencyHz")), 100.0).toDouble();
  audio_upper_frequency_hz_ =
      settings.value(storageKey(QStringLiteral("audio/upperFrequencyHz")), 3'000.0).toDouble();
  audio_input_radio_linked_ = settings
      .value(storageKey(QStringLiteral("audio/inputRadioLinked")), false)
      .toBool();
  own_callsign_ =
      settings.value(storageKey(QStringLiteral("station/ownCallsign"))).toString();
  radio_enabled_ = settings
                       .value(storageKey(QStringLiteral("radio/enabled")),
                              setup_complete_)
                       .toBool();
  const int saved_index = settings.value(storageKey(QStringLiteral("radio/referenceRigIndex")), 0).toInt();
  const auto profile_count = static_cast<int>(cwassistant::core::reference_rig_profiles().size());
  reference_rig_index_ = std::clamp(saved_index, 0, std::max(0, profile_count - 1));
  applyReferenceDefaults(reference_rig_index_);
  frequency_backend_index_ = settings.value(storageKey(QStringLiteral("radio/frequencyBackendIndex")), 0).toInt();
  radio_tuning_step_hz_ = std::clamp(
      settings.value(storageKey(QStringLiteral("radio/tuningStepHz")), 1'000)
          .toInt(),
      1'000, 100'000);
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
  cw_tone_sideband_index_ = std::clamp(
      settings.value(storageKey(QStringLiteral("radio/cwToneSidebandIndex")), 0)
          .toInt(),
      0, 1);
  keying_port_ = settings.value(storageKey(QStringLiteral("keying/port"))).toString();
  ptt_line_index_ = settings.value(storageKey(QStringLiteral("keying/pttLineIndex")), ptt_line_index_).toInt();
  key_line_index_ = settings.value(storageKey(QStringLiteral("keying/keyLineIndex")), key_line_index_).toInt();
  ptt_active_high_ = settings.value(storageKey(QStringLiteral("keying/pttActiveHigh")), true).toBool();
  key_active_high_ = settings.value(storageKey(QStringLiteral("keying/keyActiveHigh")), true).toBool();
  target_fps_ = settings.value(storageKey(QStringLiteral("display/targetFps")), 60).toInt();
  waterfall_rate_ = settings.value(storageKey(QStringLiteral("display/waterfallRate")), 60).toInt();
  waterfall_time_span_seconds_ =
      settings.value(storageKey(QStringLiteral("display/waterfallTimeSpanSeconds")), 10).toInt();
  spectrum_display_mode_ = std::clamp(settings.value(
      storageKey(QStringLiteral("display/spectrumDisplayMode")), 0).toInt(),
      0, 1);
  automatic_range_ = settings.value(storageKey(QStringLiteral("display/automaticRange")), true).toBool();
  lower_bound_db_ = settings.value(storageKey(QStringLiteral("display/lowerBoundDb")), -120.0).toDouble();
  upper_bound_db_ = settings.value(storageKey(QStringLiteral("display/upperBoundDb")), -20.0).toDouble();
  automatic_range_span_db_ =
      settings.value(storageKey(QStringLiteral("display/automaticRangeSpanDb")), 60.0).toDouble();
  waterfall_noise_suppression_ =
      settings.value(storageKey(QStringLiteral("display/waterfallNoiseSuppression")), true).toBool();
  waterfall_noise_margin_db_ =
      settings.value(storageKey(QStringLiteral("display/waterfallNoiseMarginDb")), 6.0).toDouble();
  show_cw_guide_ =
      settings.value(storageKey(QStringLiteral("display/showCwGuide")), true).toBool();
  cw_guide_center_hz_ =
      settings.value(storageKey(QStringLiteral("display/cwGuideCenterHz")), 700.0).toDouble();
  cw_guide_width_hz_ =
      settings.value(storageKey(QStringLiteral("display/cwGuideWidthHz")), 200.0).toDouble();
  averaging_frames_ = settings.value(storageKey(QStringLiteral("display/averagingFrames")), 3).toInt();
  show_grid_ = settings.value(storageKey(QStringLiteral("display/showGrid")), true).toBool();
  decoded_signal_timeout_seconds_ =
      settings.value(storageKey(QStringLiteral("display/decodedSignalTimeoutSeconds")), 30).toInt();
  local_decoder_enabled_ = settings
      .value(storageKey(QStringLiteral("decoder/localEnabled")), false)
      .toBool();
  local_decoder_model_path_ = settings
      .value(storageKey(QStringLiteral("decoder/localModelPath"))).toString();
  local_decoder_metadata_path_ = settings
      .value(storageKey(QStringLiteral("decoder/localMetadataPath"))).toString();
  local_callsign_database_enabled_ = settings
      .value(storageKey(QStringLiteral("decoder/localCallsignDatabaseEnabled")),
             false)
      .toBool();
  local_callsign_database_path_ = settings
      .value(storageKey(QStringLiteral("decoder/localCallsignDatabasePath")))
      .toString();
  local_callsign_database_status_ = !local_callsign_database_enabled_
      ? QStringLiteral("Disabled. No local callsign list is in use.")
      : (local_callsign_database_path_.isEmpty()
             ? QStringLiteral("Select a local master.scp or Call History text file.")
             : QStringLiteral("Saved local callsign list configured."));
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
  refreshAudioInputs();
  profile_selection_required_ = false;
  emit profileChanged();
  emit setupCompleteChanged();
  emit profileSelectionRequiredChanged();
  emit audioInputsChanged();
  emit settingsChanged();
  emit localDecoderConfigurationCommitted(
      local_decoder_enabled_, local_decoder_model_path_,
      local_decoder_metadata_path_);
  emit localCallsignDatabaseChanged();
  emit localCallsignDatabaseConfigurationCommitted(
      local_callsign_database_enabled_, local_callsign_database_path_);
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
  emit audioInputsChanged();
  emit settingsChanged();
  emit localDecoderConfigurationCommitted(
      local_decoder_enabled_, local_decoder_model_path_,
      local_decoder_metadata_path_);
  emit localCallsignDatabaseChanged();
  emit localCallsignDatabaseConfigurationCommitted(
      local_callsign_database_enabled_, local_callsign_database_path_);
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
  audio_input_id_.clear();
  audio_input_name_ = QStringLiteral("System default input");
  audio_dc_rejection_ = true;
  audio_automatic_gain_ = false;
  audio_gain_db_ = 0.0;
  audio_automatic_gain_target_dbfs_ = -12.0;
  audio_automatic_bandwidth_ = true;
  audio_lower_frequency_hz_ = 100.0;
  audio_upper_frequency_hz_ = 3'000.0;
  audio_input_radio_linked_ = false;
  own_callsign_.clear();
  radio_enabled_ = false;
  reference_rig_index_ = 0;
  frequency_backend_index_ = 0;
  radio_tuning_step_hz_ = 1'000;
  omnirig_slot_ = 1;
  cat4om_url_ = QStringLiteral("ws://127.0.0.1:5001/");
  cat4om_radio_id_.clear();
  cat4om_password_.clear();
  cat_port_.clear();
  keying_port_.clear();
  split_enabled_ = false;
  rx_transverter_offset_hz_ = 0;
  tx_transverter_offset_hz_ = 0;
  cw_tone_sideband_index_ = 0;
  target_fps_ = 60;
  waterfall_rate_ = 60;
  waterfall_time_span_seconds_ = 10;
  spectrum_display_mode_ = 0;
  automatic_range_ = true;
  lower_bound_db_ = -120.0;
  upper_bound_db_ = -20.0;
  automatic_range_span_db_ = 60.0;
  waterfall_noise_suppression_ = true;
  waterfall_noise_margin_db_ = 6.0;
  show_cw_guide_ = true;
  cw_guide_center_hz_ = 700.0;
  cw_guide_width_hz_ = 200.0;
  averaging_frames_ = 3;
  show_grid_ = true;
  decoded_signal_timeout_seconds_ = 30;
  local_decoder_enabled_ = false;
  local_decoder_model_path_.clear();
  local_decoder_metadata_path_.clear();
  local_callsign_database_enabled_ = false;
  local_callsign_database_path_.clear();
  local_callsign_database_status_ =
      QStringLiteral("Disabled. No local callsign list is in use.");
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

bool AppSettings::writeOmniRigRxFrequency(
    const std::uint64_t dial_frequency_hz) {
  if (!ensureOmniRigAutomation()) {
    return false;
  }
  auto* automation = static_cast<IDispatch*>(omnirig_automation_);
  VARIANT rig_value;
  const auto rig_property = omnirig_slot_ == 2 ? L"Rig2" : L"Rig1";
  if (!automation_property(automation, rig_property, &rig_value)) {
    return false;
  }
  IDispatch* rig = rig_value.vt == VT_DISPATCH ? rig_value.pdispVal : nullptr;
  bool written = false;
  if (rig != nullptr) {
    const auto target = omni_rig_rx_write_target(rig);
    const wchar_t* property =
        target == cwassistant::core::OmniRigRxFrequencyTarget::FrequencyA
                                  ? L"FreqA"
        : target == cwassistant::core::OmniRigRxFrequencyTarget::FrequencyB
                                  ? L"FreqB"
        : target == cwassistant::core::OmniRigRxFrequencyTarget::Frequency
                                  ? L"Freq"
                                  : nullptr;
    written = property != nullptr &&
              automation_put_frequency(rig, property, dial_frequency_hz);
  }
  VariantClear(&rig_value);
  return written;
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
