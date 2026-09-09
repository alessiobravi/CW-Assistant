#include "sdr/sdr_runtime_environment.hpp"

#include <QDir>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  QTemporaryDir temporary;
  expect(temporary.isValid(), "temporary package root is available");
  const QDir root(temporary.path());
  expect(root.mkpath("bin"), "executable directory is created");
  expect(root.mkpath("lib/SoapySDR/modules0.8"),
         "portable module directory is created");

  const QString executable_directory = root.filePath("bin");
  const QString module_directory =
      root.filePath("lib/SoapySDR/modules0.8");
  const auto bundled =
      cwassistant::desktop::bundledSoapyModulePaths(executable_directory);
  expect(bundled.size() == 1, "only existing bundled paths are returned");
  expect(QDir::cleanPath(bundled.front()) == QDir::cleanPath(module_directory),
         "portable module path is resolved relative to the executable");

  expect(root.mkpath("CW Buddy.app/Contents/MacOS"),
         "macOS executable directory is created");
  expect(root.mkpath("CW Buddy.app/Contents/PlugIns/SoapySDR"),
         "macOS bundled module directory is created");
  const QString mac_executable_directory =
      root.filePath("CW Buddy.app/Contents/MacOS");
  const QString mac_module_directory =
      root.filePath("CW Buddy.app/Contents/PlugIns/SoapySDR");
  const auto mac_bundled =
      cwassistant::desktop::bundledSoapyModulePaths(mac_executable_directory);
  expect(mac_bundled.size() == 1,
         "macOS lookup returns only the packaged module directory");
  expect(QDir::cleanPath(mac_bundled.front()) ==
             QDir::cleanPath(mac_module_directory),
         "macOS module path is resolved inside the application bundle");

  const QByteArray original = qgetenv("SOAPY_SDR_PLUGIN_PATH");
  const QString external = root.filePath("external-vendor-modules");
  qputenv("SOAPY_SDR_PLUGIN_PATH", external.toLocal8Bit());
  cwassistant::desktop::configureBundledSoapyRuntime(executable_directory);
  const QStringList configured =
      QString::fromLocal8Bit(qgetenv("SOAPY_SDR_PLUGIN_PATH"))
          .split(QDir::listSeparator(), Qt::SkipEmptyParts);
  expect(configured.size() == 2,
         "bundled and operator-provided module paths are both retained");
  expect(QDir::cleanPath(configured.front()) == QDir::cleanPath(module_directory),
         "bundled path precedes operator-provided environment paths");
  expect(configured.back() == external,
         "external vendor module path remains available");

  if (original.isNull()) {
    qunsetenv("SOAPY_SDR_PLUGIN_PATH");
  } else {
    qputenv("SOAPY_SDR_PLUGIN_PATH", original);
  }
  return 0;
}
