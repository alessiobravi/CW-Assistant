#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

bool contains(const std::string_view source, const std::string_view text) {
  return source.find(text) != std::string_view::npos;
}

}  // namespace

int main() {
  const std::string workflow = readFile(CWA_DESKTOP_WORKFLOW_PATH);
  const std::string root_cmake = readFile(CWA_ROOT_CMAKE_PATH);
  const std::string launcher = readFile(CWA_LINUX_SDR_LAUNCHER_PATH);
  if (workflow.empty() || root_cmake.empty() || launcher.empty()) return 1;

  // Official Linux builds must compile the real backend and install an RTL-SDR
  // module instead of silently publishing the unavailable stub.
  if (!contains(workflow,
                "sdr_configure_arguments: -DCWA_ENABLE_SOAPY_SDR=ON") ||
      !contains(workflow, "libsoapysdr-dev soapysdr-module-rtlsdr") ||
      !contains(workflow, "readelf -d stage/bin/cw-buddy-desktop") ||
      !contains(workflow, "libSoapySDR.so.0.8")) {
    return 2;
  }

  // The Debian package declares the dynamically loaded hardware provider;
  // dpkg-shlibdeps cannot discover that relationship by inspecting DT_NEEDED.
  if (!contains(root_cmake,
                "CPACK_DEBIAN_PACKAGE_DEPENDS \"soapysdr-module-rtlsdr\"") ||
      !contains(workflow, "debian-dependencies.txt")) {
    return 3;
  }

  // The portable archive carries the Soapy runtime, RTL module, its direct
  // USB dependencies, and the corresponding distribution copyright records.
  for (const std::string_view required : {
           "librtlsdrSupport.so", "librtlsdr.so.0", "libusb-1.0.so.0",
           "libudev.so.1", "libcap.so.2", "libcap2", "third-party",
           "--sdr-backend-smoke-test",
           "ci-portable-sdr-smoke",
           "Unexpected external SDR runtime dependency",
           "isolated_soapy_root", "cp -L \"${module_path}\""}) {
    if (!contains(workflow, required)) return 4;
  }

  // The wrapper locates only paths inside its extracted package, preserves an
  // existing operator plugin path, and cannot accidentally invoke itself.
  if (!contains(launcher, "SOAPY_SDR_PLUGIN_PATH") ||
      !contains(launcher, "LD_LIBRARY_PATH") ||
      !contains(launcher, "while [ -L \"${launcher}\" ]") ||
      !contains(launcher, "readlink \"${launcher}\"") ||
      !contains(launcher, "cw-buddy-desktop.bin") ||
      contains(launcher, "SOAPY_SDR_ROOT") ||
      contains(launcher, "eval ")) {
    return 5;
  }

  return 0;
}
