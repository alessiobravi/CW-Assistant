#include "sdr/sdr_runtime_environment.hpp"

#include <QDir>
#include <QFileInfo>

namespace cwassistant::desktop {
namespace {

void append_existing_unique(QStringList& paths, const QString& candidate) {
  const QFileInfo info(QDir::cleanPath(candidate));
  if (!info.isDir()) {
    return;
  }
  const QString absolute = info.absoluteFilePath();
  if (!paths.contains(absolute)) {
    paths.append(absolute);
  }
}

}  // namespace

QStringList bundledSoapyModulePaths(const QString& executable_directory) {
  const QDir executable_dir(executable_directory);
  QStringList paths;

  // macOS bundle layout, followed by the common Unix/Windows staged layouts.
  append_existing_unique(
      paths, executable_dir.filePath("../Frameworks/SoapySDR/modules0.8"));
  // Keep macOS modules directly in this directory. A child named
  // "modules0.8" is interpreted as a malformed bundle by codesign --deep.
  append_existing_unique(paths,
                         executable_dir.filePath("../PlugIns/SoapySDR"));
  append_existing_unique(
      paths, executable_dir.filePath("../PlugIns/SoapySDR/modules0.8"));
  append_existing_unique(paths,
                         executable_dir.filePath("../lib/SoapySDR/modules0.8"));
  append_existing_unique(paths,
                         executable_dir.filePath("SoapySDR/modules0.8"));
  return paths;
}

void configureBundledSoapyRuntime(const QString& executable_directory) {
  QStringList paths = bundledSoapyModulePaths(executable_directory);
  const QString existing = QString::fromLocal8Bit(qgetenv("SOAPY_SDR_PLUGIN_PATH"));
  for (const QString& path :
       existing.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
    if (!paths.contains(path)) {
      paths.append(path);
    }
  }
  if (!paths.isEmpty()) {
    qputenv("SOAPY_SDR_PLUGIN_PATH",
            paths.join(QDir::listSeparator()).toLocal8Bit());
  }
}

}  // namespace cwassistant::desktop
