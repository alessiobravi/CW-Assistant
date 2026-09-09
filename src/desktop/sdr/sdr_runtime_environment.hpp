#pragma once

#include <QString>
#include <QStringList>

namespace cwassistant::desktop {

// Returns existing application-private SoapySDR module directories in stable
// preference order. The executable directory is passed explicitly so the
// policy remains deterministic and testable outside an installed package.
[[nodiscard]] QStringList bundledSoapyModulePaths(
    const QString& executable_directory);

// Prepends existing application-private module directories to SoapySDR's
// additive plugin search path. Existing operator-provided paths are retained,
// which allows separately installed vendor modules such as SDRplay.
void configureBundledSoapyRuntime(const QString& executable_directory);

}  // namespace cwassistant::desktop
