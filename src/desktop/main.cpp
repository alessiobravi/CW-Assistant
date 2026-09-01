#include <QGuiApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <qqml.h>

#include <cstdlib>
#include <utility>

#include "replay/replay_controller.hpp"
#include "settings/app_settings.hpp"
#include "visualization/spectrum_waterfall_item.hpp"

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("CW Assistant"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("cw-assistant.org"));
  QCoreApplication::setApplicationName(QStringLiteral("CW Assistant"));
  QCoreApplication::setApplicationVersion(QStringLiteral(CWA_VERSION));
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/cw-assistant.png")));

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
    replay_controller.setRadioFrequencyContext(
        rx_rf_hz.has_value(),
        rx_rf_hz ? static_cast<qulonglong>(*rx_rf_hz) : 0,
        settings.cwToneSidebandIndex(), settings.cwGuideCenterHz());
  };
  apply_spectrum_processing();
  apply_radio_frequency();
  replay_controller.setAudioInputSelection(settings.audioInputId(),
                                           settings.audioInputDisplayName());
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_spectrum_processing);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::cat4omChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::radioFrequencyChanged,
      &replay_controller, apply_radio_frequency);
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::audioInputsChanged,
      &replay_controller, [&settings, &replay_controller] {
        replay_controller.setAudioInputSelection(
            settings.audioInputId(), settings.audioInputDisplayName());
      });
  qmlRegisterType<cwassistant::desktop::SpectrumWaterfallItem>(
      "CWAssistant", 1, 0, "SpectrumWaterfall");
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appSettings"),
                                           &settings);
  engine.rootContext()->setContextProperty(QStringLiteral("replayController"),
                                           &replay_controller);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
      [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("CWAssistant"), QStringLiteral("Main"));
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
      auto* decoder_channel_list = root_object->findChild<QQuickItem*>(
          QStringLiteral("decoderChannelList"));
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
          live_cw_guide_check == nullptr ||
          decoder_channel_list == nullptr ||
          own_callsign_field->property("text").toString() !=
              QStringLiteral("IU0LFQ/P")) {
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
