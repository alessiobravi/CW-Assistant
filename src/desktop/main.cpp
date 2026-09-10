#include <QGuiApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include "cwassistant/core/cw_vocabulary.hpp"
#include <array>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <qqml.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include "replay/replay_controller.hpp"
#include "sdr/sdr_receiver.hpp"
#include "sdr/sdr_runtime_environment.hpp"
#include "settings/app_settings.hpp"
#include "settings/product_migration.hpp"
#include "transmit/transmit_controller.hpp"
#include "updates/callsign_database_updater.hpp"
#include "updates/update_checker.hpp"
#include "visualization/spectrum_waterfall_item.hpp"

namespace {

bool sdr_backend_smoke_requested(const int argc, char* argv[]) {
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--sdr-backend-smoke-test") {
      return true;
    }
  }
  return false;
}

int run_sdr_backend_smoke() {
  cwassistant::desktop::SdrReceiver receiver(
      cwassistant::desktop::makeSoapySdrReceiveBackend());
  const auto report = receiver.discover();
  const bool rtl_module_loaded = std::ranges::find(
                                     report.loaded_drivers, "rtlsdr") !=
                                 report.loaded_drivers.end();
  if (!report.backend_available || !rtl_module_loaded) {
    std::cerr << "SDR backend smoke failed: " << report.diagnostic << '\n';
    return 2;
  }
  std::cout << "SDR backend smoke passed: SoapySDR "
            << report.backend_version << ", RTL-SDR module loaded\n";
  return 0;
}

}  // namespace

namespace {

// Seeds the operator's dictionary directory from the copies inside the binary
// on first run, then loads it. The operator's files win when present so an
// edited vocabulary survives an upgrade; the built-in copies are the fallback
// and guarantee the decoder is never left with no vocabulary at all. Returns
// the number of exchange words available.
std::size_t loadCwDictionaries(const QString& app_data_path) {
  static constexpr std::array<const char*, 2> kFiles{
      "cw-abbreviations.txt", "cw-word-gap-prefixes.txt"};
  const QDir directory(app_data_path + QStringLiteral("/dictionaries"));
  QDir().mkpath(directory.absolutePath());

  const auto read = [&directory](const char* name) {
    const QString editable = directory.filePath(QString::fromLatin1(name));
    QFile file(editable);
    if (!file.exists()) {
      QFile bundled(QStringLiteral(":/dictionaries/") +
                    QString::fromLatin1(name));
      if (bundled.open(QIODevice::ReadOnly)) {
        const QByteArray contents = bundled.readAll();
        // Seed the editable copy, but never fail the load if the directory is
        // read-only: the bundled contents are already in hand.
        if (file.open(QIODevice::WriteOnly)) file.write(contents);
        file.close();
        return contents;
      }
      return QByteArray{};
    }
    if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
    return file.readAll();
  };

  auto& vocabulary = cwassistant::core::cwSharedVocabulary();
  vocabulary.clear();
  const QByteArray words = read(kFiles[0]);
  const QByteArray prefixes = read(kFiles[1]);
  static_cast<void>(vocabulary.importExchangeWords(
      std::string_view(words.constData(),
                       static_cast<std::size_t>(words.size()))));
  static_cast<void>(vocabulary.importWordGapPrefixes(
      std::string_view(prefixes.constData(),
                       static_cast<std::size_t>(prefixes.size()))));
  return vocabulary.exchangeWordCount();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (sdr_backend_smoke_requested(argc, argv)) {
    QCoreApplication application(argc, argv);
    cwassistant::desktop::configureBundledSoapyRuntime(
        QCoreApplication::applicationDirPath());
    return run_sdr_backend_smoke();
  }

  QGuiApplication application(argc, argv);
  // Resolve the legacy locations before adopting the new public identity.
  // The old bundle identifier remains stable for installer compatibility,
  // while Qt settings and the managed SCP cache move forward once and safely.
  QCoreApplication::setOrganizationName(QStringLiteral("CW Assistant"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("cw-assistant.org"));
  QCoreApplication::setApplicationName(QStringLiteral("CW Assistant"));
  const QString legacy_app_data_path =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QSettings legacy_settings;

  QCoreApplication::setOrganizationName(QStringLiteral("CW Buddy"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("cw-buddy.org"));
  QCoreApplication::setApplicationName(QStringLiteral("CW Buddy"));
  const QString current_app_data_path =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QSettings current_settings;
  static_cast<void>(cwassistant::desktop::migrateLegacyProductState(
      legacy_settings, current_settings, legacy_app_data_path,
      current_app_data_path));
  static_cast<void>(loadCwDictionaries(current_app_data_path));
  QCoreApplication::setApplicationVersion(QStringLiteral(CWA_VERSION));
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/cw-buddy.png")));
  cwassistant::desktop::configureBundledSoapyRuntime(
      QCoreApplication::applicationDirPath());

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Cross-platform multi-channel CW operating assistant"));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption profile_option(
      QStringList{QStringLiteral("p"), QStringLiteral("profile")},
      QStringLiteral("Use an isolated station configuration profile."),
      QStringLiteral("name"), QStringLiteral("default"));
  parser.addOption(profile_option);
  QCommandLineOption smoke_test_option(
      QStringLiteral("smoke-test"),
      QStringLiteral("Load and render the desktop shell, then exit."));
  parser.addOption(smoke_test_option);
  parser.process(application);

  QQuickStyle::setStyle(QStringLiteral("Material"));

  cwassistant::desktop::AppSettings settings(parser.value(profile_option),
                                             parser.isSet(profile_option));
  if (parser.isSet(smoke_test_option)) {
    settings.setOwnCallsign(QStringLiteral(" iu0lfq/p "));
  }
  cwassistant::desktop::ReplayController replay_controller;
  cwassistant::desktop::UpdateChecker update_checker;
  cwassistant::desktop::CallsignDatabaseUpdater callsign_database_updater;
  cwassistant::desktop::TransmitController transmit_controller;
  transmit_controller.setOwnCallsign(settings.ownCallsign());
  const auto apply_spectrum_processing = [&settings, &replay_controller] {
    replay_controller.setAveragingFrames(settings.averagingFrames());
    replay_controller.setSpectrumProcessing(
        settings.audioDcRejection(), settings.audioAutomaticGain(),
        settings.audioGainDb(), settings.audioAutomaticGainTargetDbfs(),
        settings.audioAutomaticBandwidth(), settings.audioLowerFrequencyHz(),
        settings.audioUpperFrequencyHz(), settings.waterfallRate());
  };
  const auto apply_radio_frequency = [&settings, &replay_controller] {
    const auto rx_rf_hz = settings.controlledRxRfHz();
    const auto tx_rf_hz = settings.controlledTxRfHz();
    replay_controller.setRadioFrequencyContext(
        rx_rf_hz.has_value(),
        rx_rf_hz ? static_cast<qulonglong>(*rx_rf_hz) : 0,
        tx_rf_hz ? static_cast<qulonglong>(*tx_rf_hz) : 0,
        settings.controlledSplitActive(), settings.cwToneSidebandIndex(),
        settings.cwGuideCenterHz());
  };
  const auto apply_decoded_signal_timeout = [&settings, &replay_controller] {
    replay_controller.setDecodedSignalTimeoutSeconds(
        settings.decodedSignalTimeoutSeconds());
  };
  const auto apply_local_character_decoder = [&settings,
                                               &replay_controller] {
    replay_controller.configureLocalCharacterDecoder(
        settings.localDecoderEnabled(), settings.localDecoderModelPath(),
        settings.localDecoderMetadataPath());
  };
  const auto apply_own_callsign = [&settings, &replay_controller] {
    replay_controller.setOwnCallsign(settings.ownCallsign());
  };
  const auto apply_callsign_database_correction = [&settings,
                                                   &replay_controller] {
    replay_controller.setCallsignDatabaseCorrectionEnabled(
        settings.callsignDatabaseCorrectionEnabled());
  };
  const auto apply_keying_model = [&settings, &replay_controller] {
    replay_controller.setKeyingModel(settings.keyingModel());
  };
  const auto apply_operator_role = [&settings, &replay_controller] {
    replay_controller.setOperatorRole(settings.operatorRole());
  };
  const auto apply_debug_capture_limit = [&settings, &replay_controller] {
    replay_controller.setDebugCaptureMaximumSeconds(
        settings.debugCaptureMaximumSeconds());
  };
  const auto apply_transmit_hardware = [&settings, &transmit_controller] {
    transmit_controller.configureHardware(
        settings.radioEnabled() && settings.directKeyingEnabled(),
        settings.keyingPort(), settings.pttLineIndex(), settings.keyLineIndex(),
        settings.pttActiveHigh(), settings.keyActiveHigh(), settings.catPort(),
        settings.directKeyingValidated());
  };
  const auto apply_transmit_speed = [&settings, &transmit_controller] {
    transmit_controller.configureTxSpeed(settings.txSpeedMode(),
                                         settings.fixedTxWpm());
  };
  const auto apply_transmit_radio_safety = [&settings, &transmit_controller] {
    const auto tx_rf_hz = settings.controlledTxRfHz();
    transmit_controller.configureRadioSafety(
        settings.radioEnabled(),
        tx_rf_hz ? static_cast<qulonglong>(*tx_rf_hz) : 0U,
        settings.radioTxModeTarget(), settings.radioTxModeConfirmed(),
        settings.radioSplitKnown(), settings.controlledSplitActive());
  };
  const auto follow_sdr_to_radio_vfo = [&settings] {
    settings.followSdrToRadioVfo();
  };
  const auto apply_sdr_input = [&settings, &replay_controller] {
    const qint64 sample_rate_hz = settings.sdrSampleRateHz();
    const qint64 decoder_bandwidth_hz = std::clamp<qint64>(
        settings.sdrDecoderBandwidthHz(), 2'000,
        std::max<qint64>(2'000, sample_rate_hz - 2'000));
    const qint64 acquisition_center_hz =
        static_cast<qint64>(settings.sdrCenterFrequencyHz());
    const qint64 edge_margin_hz = decoder_bandwidth_hz / 2 + 1'000;
    const qint64 lowest_decoder_center =
        acquisition_center_hz - sample_rate_hz / 2 + edge_margin_hz;
    const qint64 highest_decoder_center =
        acquisition_center_hz + sample_rate_hz / 2 - edge_margin_hz;
    const qint64 decoder_center_hz = std::clamp<qint64>(
        static_cast<qint64>(settings.sdrDecoderCenterFrequencyHz()),
        lowest_decoder_center, highest_decoder_center);
    if (settings.sdrDecoderBandwidthHz() != decoder_bandwidth_hz)
      settings.setSdrDecoderBandwidthHz(
          static_cast<int>(decoder_bandwidth_hz));
    if (static_cast<qint64>(settings.sdrDecoderCenterFrequencyHz()) !=
        decoder_center_hz)
      settings.setSdrDecoderCenterFrequencyHz(
          static_cast<qulonglong>(decoder_center_hz));
    replay_controller.setSdrInputSelection(
        settings.sdrDeviceId(), settings.sdrDeviceDisplayName(),
        settings.sdrCenterFrequencyHz(), settings.sdrSampleRateHz(),
        settings.sdrBandwidthHz(), settings.sdrAntenna(),
        settings.sdrAutomaticGain(),
        settings.sdrGainDb(),
        static_cast<qulonglong>(decoder_center_hz),
        static_cast<int>(decoder_bandwidth_hz));
  };
  const auto apply_offline_callsign_database =
      [&settings, &replay_controller, &callsign_database_updater] {
    if (callsign_database_updater.managedEnabled()) {
      const QString path = callsign_database_updater.installedFilePath();
      if (!path.isEmpty()) {
        replay_controller.configureOfflineCallsignDatabase(true, path);
        return;
      }
    }
    replay_controller.configureOfflineCallsignDatabase(
        settings.localCallsignDatabaseEnabled(),
        settings.localCallsignDatabasePath());
  };
  apply_spectrum_processing();
  apply_radio_frequency();
  apply_decoded_signal_timeout();
  apply_local_character_decoder();
  apply_callsign_database_correction();
  apply_keying_model();
  apply_operator_role();
  apply_debug_capture_limit();
  apply_own_callsign();
  apply_offline_callsign_database();
  apply_transmit_hardware();
  apply_transmit_speed();
  apply_transmit_radio_safety();
  follow_sdr_to_radio_vfo();
  replay_controller.setAudioInputSelection(settings.audioInputId(),
                                           settings.audioInputDisplayName());
  apply_sdr_input();
  if (settings.receiverInputTypeIndex() == 1)
    replay_controller.setSourceMode(2);
  replay_controller.setMonitorOutputSelection(settings.audioOutputId());
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_spectrum_processing);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_decoded_signal_timeout);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_callsign_database_correction);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_keying_model);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_debug_capture_limit);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_operator_role);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_own_callsign);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &transmit_controller,
      [&settings, &transmit_controller] {
        transmit_controller.setOwnCallsign(settings.ownCallsign());
      });
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &transmit_controller, apply_transmit_hardware);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &transmit_controller, apply_transmit_speed);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &transmit_controller, apply_transmit_radio_safety);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::radioFrequencyChanged,
      &transmit_controller, apply_transmit_radio_safety);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::cat4omChanged,
      &transmit_controller, apply_transmit_radio_safety);
  QObject::connect(&application, &QCoreApplication::aboutToQuit,
                   &transmit_controller, &cwassistant::desktop::TransmitController::disarm);
  QObject::connect(
      &replay_controller, &cwassistant::desktop::ReplayController::decoderChanged,
      &transmit_controller,
      [&replay_controller, &transmit_controller] {
        transmit_controller.observeDecoderChannels(
            replay_controller.decoderChannels());
      });
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::cat4omChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::radioFrequencyChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::radioFrequencyChanged,
      &settings, follow_sdr_to_radio_vfo);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::cat4omChanged,
      &settings, follow_sdr_to_radio_vfo);
  QObject::connect(
      &settings,
      &cwassistant::desktop::AppSettings::localCallsignDatabaseConfigurationCommitted,
      &replay_controller, apply_offline_callsign_database);
  QObject::connect(
      &callsign_database_updater,
      &cwassistant::desktop::CallsignDatabaseUpdater::databaseInstalled,
      &replay_controller, apply_offline_callsign_database);
  QObject::connect(
      &callsign_database_updater,
      &cwassistant::desktop::CallsignDatabaseUpdater::managedEnabledChanged,
      &replay_controller,
      [&callsign_database_updater, &apply_offline_callsign_database,
       smoke_test = parser.isSet(smoke_test_option)] {
        apply_offline_callsign_database();
        if (!smoke_test && callsign_database_updater.managedEnabled() &&
            callsign_database_updater.autoUpdateEnabled()) {
          callsign_database_updater.checkAndInstallIfDue();
        }
      });
  QObject::connect(
      &callsign_database_updater,
      &cwassistant::desktop::CallsignDatabaseUpdater::autoUpdateEnabledChanged,
      &callsign_database_updater,
      [&callsign_database_updater,
       smoke_test = parser.isSet(smoke_test_option)] {
        if (!smoke_test && callsign_database_updater.managedEnabled() &&
            callsign_database_updater.autoUpdateEnabled()) {
          callsign_database_updater.checkAndInstallIfDue();
        }
      });
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::audioInputsChanged,
      &replay_controller, [&settings, &replay_controller] {
        replay_controller.setAudioInputSelection(
            settings.audioInputId(), settings.audioInputDisplayName());
      });
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::sdrSettingsChanged,
      &replay_controller, apply_sdr_input);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::receiverInputTypeChanged,
      &replay_controller, [&settings, &replay_controller] {
        replay_controller.setSourceMode(
            settings.receiverInputTypeIndex() == 1 ? 2 : 0);
      });
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::audioOutputsChanged,
      &replay_controller, [&settings, &replay_controller] {
        replay_controller.setMonitorOutputSelection(settings.audioOutputId());
      });
  QObject::connect(
      &settings,
      &cwassistant::desktop::AppSettings::localDecoderConfigurationCommitted,
      &replay_controller, apply_local_character_decoder);
  qmlRegisterType<cwassistant::desktop::SpectrumWaterfallItem>(
      "CWBuddy", 1, 0, "SpectrumWaterfall");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),
                                           &settings);
  engine.rootContext()->setContextProperty(QStringLiteral("replayController"),
                                           &replay_controller);
  engine.rootContext()->setContextProperty(QStringLiteral("updateChecker"),
                                           &update_checker);
  engine.rootContext()->setContextProperty(
      QStringLiteral("callsignDatabaseUpdater"), &callsign_database_updater);
  engine.rootContext()->setContextProperty(QStringLiteral("transmitController"),
                                           &transmit_controller);
  if (!parser.isSet(smoke_test_option) && update_checker.autoCheckEnabled()) {
    // A short delay so the background check never competes with startup
    // rendering/audio work; never runs during the smoke test, which must
    // stay hermetic (no real network access).
    QTimer::singleShot(4'000, &update_checker,
                       [&update_checker] { update_checker.checkForUpdates(); });
  }
  if (!parser.isSet(smoke_test_option) &&
      callsign_database_updater.managedEnabled() &&
      callsign_database_updater.autoUpdateEnabled()) {
    QTimer::singleShot(6'000, &callsign_database_updater,
                       [&callsign_database_updater] {
                         callsign_database_updater.checkAndInstallIfDue();
                       });
  }
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("CWBuddy"), QStringLiteral("Main"));
  if (parser.isSet(smoke_test_option)) {
    QTimer::singleShot(250, &application, [&engine] {
      QObject* root_object = engine.rootObjects().isEmpty()
                                 ? nullptr
                                 : engine.rootObjects().constFirst();
      auto* display =
          root_object == nullptr
              ? nullptr
              : root_object
                    ->findChild<cwassistant::desktop::SpectrumWaterfallItem*>(
                        QStringLiteral("spectrumDisplay"));
      if (display == nullptr) {
        QCoreApplication::exit(EXIT_FAILURE);
        return;
      }
      auto* next_button =
          root_object->findChild<QQuickItem*>(QStringLiteral("setupNextButton"));
      auto* audio_input_combo = root_object->findChild<QQuickItem*>(
          QStringLiteral("setupAudioInputCombo"));
      auto* live_audio_button = root_object->findChild<QQuickItem*>(
          QStringLiteral("startLiveAudioButton"));
      auto* own_callsign_field = root_object->findChild<QQuickItem*>(
          QStringLiteral("ownCallsignField"));
      auto* dc_rejection_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("audioDcRejectionCheck"));
      auto* automatic_gain_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("audioAutomaticGainCheck"));
      auto* automatic_bandwidth_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("audioAutomaticBandwidthCheck"));
      auto* live_levels_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("liveAutomaticLevelsCheck"));
      auto* live_noise_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("liveNoiseSuppressionCheck"));
      auto* live_cw_guide_check = root_object->findChild<QQuickItem*>(
          QStringLiteral("liveCwGuideCheck"));
      auto* tx_slice_guide = root_object->findChild<QQuickItem*>(
          QStringLiteral("txSliceGuideOverlay"));
      auto* decoder_channel_list = root_object->findChild<QQuickItem*>(
          QStringLiteral("decoderChannelList"));
      auto* live_controls = root_object->findChild<QQuickItem*>(
          QStringLiteral("liveControlsFrame"));
      auto* view_selector = root_object->findChild<QQuickItem*>(
          QStringLiteral("viewSelector"));
      auto* pin_live_controls = root_object->findChild<QQuickItem*>(
          QStringLiteral("pinLiveControlsButton"));
      auto* about_version_label = root_object->findChild<QQuickItem*>(
          QStringLiteral("aboutVersionLabel"));
      if (next_button == nullptr || !next_button->isVisible() ||
          next_button->width() < 1.0 || next_button->height() < 1.0 ||
          next_button->window() == nullptr ||
          next_button->mapToScene(
              QPointF(next_button->width(), next_button->height())).y() >
          next_button->window()->height() || audio_input_combo == nullptr ||
          audio_input_combo->property("count").toInt() < 1 ||
          audio_input_combo->property("currentIndex").toInt() < 0 ||
          live_audio_button == nullptr || own_callsign_field == nullptr ||
          dc_rejection_check == nullptr || automatic_gain_check == nullptr ||
          automatic_bandwidth_check == nullptr ||
          live_levels_check == nullptr || live_noise_check == nullptr ||
          live_cw_guide_check == nullptr || tx_slice_guide == nullptr ||
          decoder_channel_list == nullptr ||
          live_controls == nullptr || view_selector == nullptr ||
          pin_live_controls == nullptr ||
          live_controls->property("expanded").toBool() ||
          about_version_label == nullptr ||
          about_version_label->property("text").toString() !=
              QStringLiteral("Version %1").arg(
                  QCoreApplication::applicationVersion()) ||
          own_callsign_field->property("text").toString() !=
              QStringLiteral("IU0LFQ/P")) {
        QCoreApplication::exit(EXIT_FAILURE);
        return;
      }
      live_controls->setProperty("pinned", true);
      if (!live_controls->property("expanded").toBool()) {
        QCoreApplication::exit(EXIT_FAILURE);
        return;
      }
      QObject* setup_wizard =
          root_object->findChild<QObject*>(QStringLiteral("setupWizard"));
      if (setup_wizard == nullptr ||
          !QMetaObject::invokeMethod(setup_wizard, "goForward",
                                     Qt::DirectConnection) ||
          setup_wizard->property("step").toInt() != 1 ||
          !QMetaObject::invokeMethod(setup_wizard, "goForward",
                                     Qt::DirectConnection) ||
          setup_wizard->property("step").toInt() != 4 ||
          !QMetaObject::invokeMethod(setup_wizard, "goBack",
                                     Qt::DirectConnection) ||
          setup_wizard->property("step").toInt() != 1 ||
          !QMetaObject::invokeMethod(setup_wizard, "goBack",
                                     Qt::DirectConnection) ||
          setup_wizard->property("step").toInt() != 0) {
        QCoreApplication::exit(EXIT_FAILURE);
        return;
      }
      QVector<float> bins(256, -112.0F);
      for (qsizetype i = 92; i < 104; ++i) {
        bins[i] = -42.0F + static_cast<float>(std::abs(i - 98)) * -3.0F;
      }
      cwassistant::desktop::SpectrumFrame frame{
          .bins_dbfs = std::move(bins),
          .sequence = 1,
          .timestamp_ns = 1,
          .lower_frequency_hz = 0.0,
          .upper_frequency_hz = 24'000.0,
      };
      if (!QMetaObject::invokeMethod(
              display, "acceptFrame", Qt::DirectConnection,
              Q_ARG(cwassistant::desktop::SpectrumFrame, frame))) {
        QCoreApplication::exit(EXIT_FAILURE);
      }
    });
    QTimer::singleShot(1'500, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
