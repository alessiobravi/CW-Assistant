#pragma once

#include <QString>

class QSettings;

namespace cwassistant::desktop {

// Imports the previous product identity once. Existing CW Buddy values always
// win, and the managed callsign cache is copied rather than moved so rolling
// back to an earlier build remains safe.
[[nodiscard]] bool migrateLegacyProductState(
    QSettings& legacy_settings, QSettings& current_settings,
    const QString& legacy_app_data_path,
    const QString& current_app_data_path);

}  // namespace cwassistant::desktop
