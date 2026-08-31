#include <QGuiApplication>
#include <QCommandLineParser>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
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
  cwassistant::desktop::ReplayController replay_controller;
  replay_controller.setAveragingFrames(settings.averagingFrames());
  QObject::connect(
      &settings, &cwassistant::desktop::AppSettings::settingsChanged,
      &replay_controller, [&settings, &replay_controller] {
        replay_controller.setAveragingFrames(settings.averagingFrames());
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
    QTimer::singleShot(100, &application, [&engine] {
      auto* display = engine.rootObjects().isEmpty()
                          ? nullptr
                          : engine.rootObjects()
                                .constFirst()
                                ->findChild<cwassistant::desktop::
                                                SpectrumWaterfallItem*>(
                                    QStringLiteral("spectrumDisplay"));
      if (display == nullptr) {
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
