#include <QGuiApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <qqml.h>

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
  return application.exec();
}
