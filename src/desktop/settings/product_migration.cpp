#include "product_migration.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace cwassistant::desktop {
namespace {

constexpr auto kMigrationMarker = "migration/cwAssistantProductStateImported";
constexpr auto kManagedCallsignRelativePath =
    "callsign-databases/supercheckpartial/MASTER.SCP";

bool copyManagedCallsignDatabase(const QString& legacy_app_data_path,
                                 const QString& current_app_data_path) {
  if (legacy_app_data_path.isEmpty() || current_app_data_path.isEmpty())
    return true;
  const QString source =
      QDir(legacy_app_data_path).filePath(kManagedCallsignRelativePath);
  const QString destination =
      QDir(current_app_data_path).filePath(kManagedCallsignRelativePath);
  if (!QFileInfo::exists(source) || QFileInfo::exists(destination)) return true;
  if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return false;
  return QFile::copy(source, destination);
}

}  // namespace

bool migrateLegacyProductState(QSettings& legacy_settings,
                               QSettings& current_settings,
                               const QString& legacy_app_data_path,
                               const QString& current_app_data_path) {
  if (current_settings.value(QString::fromLatin1(kMigrationMarker), false)
          .toBool()) {
    return true;
  }
  for (const QString& key : legacy_settings.allKeys()) {
    if (!current_settings.contains(key))
      current_settings.setValue(key, legacy_settings.value(key));
  }
  if (!copyManagedCallsignDatabase(legacy_app_data_path,
                                   current_app_data_path)) {
    current_settings.sync();
    return false;
  }
  current_settings.setValue(QString::fromLatin1(kMigrationMarker), true);
  current_settings.sync();
  return current_settings.status() == QSettings::NoError;
}

}  // namespace cwassistant::desktop
