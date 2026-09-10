#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

bool contains(const std::string_view source, const std::string_view needle,
              const std::string_view message) {
  if (source.find(needle) != std::string_view::npos) return true;
  std::cerr << message << '\n';
  return false;
}

bool excludes(const std::string_view source, const std::string_view needle,
              const std::string_view message) {
  if (source.find(needle) == std::string_view::npos) return true;
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  const std::string settings = readFile(CWA_SETTINGS_QML_PATH);
  const std::string wizard = readFile(CWA_SETUP_QML_PATH);
  const std::string header = readFile(CWA_APP_SETTINGS_HPP_PATH);
  const std::string implementation = readFile(CWA_APP_SETTINGS_CPP_PATH);
  bool passed = !settings.empty() && !wizard.empty() && !header.empty() &&
                !implementation.empty();

  for (const auto* source : {&settings, &wizard}) {
    passed =
        excludes(*source, "Label { text: \"Baud rate\" }",
                 "active network/provider pages must not expose local baud") &&
        passed;
    passed =
        excludes(*source, "Label { text: \"CAT COM port\" }",
                 "active providers must not expose an unused CAT COM port") &&
        passed;
    passed =
        excludes(*source, "Label { text: \"Framing\" }",
                 "active providers must not expose unused serial framing") &&
        passed;
    passed =
        contains(*source, "visible: appSettings.frequencyBackendIndex === 0",
                 "OmniRig-only configuration must be capability-gated") &&
        passed;
  }

  passed =
      contains(settings, "rigctld owns the physical radio and serial framing",
               "Settings must explain rigctld transport ownership") &&
      passed;
  passed = contains(wizard, "function frequencyProviderSummary()",
                    "wizard review must summarize the selected provider") &&
           passed;
  passed = excludes(wizard, "appSettings.catBaudRate + \" baud\"",
                    "wizard review must not claim an unused local baud rate") &&
           passed;
  passed =
      contains(header,
               "supportedCatBaudRates READ supportedCatBaudRates CONSTANT",
               "future direct serial UI needs one supported baud-rate model") &&
      passed;
  passed =
      contains(implementation, "radio_state_.tx_vfo.identifier == \"A\"",
               "OmniRig TX writes must use authoritative TX-VFO identity") &&
      passed;
  passed = contains(implementation, "radio_state_.tx_vfo.identifier == \"B\"",
                    "OmniRig TX writes must support authoritative VFO B") &&
           passed;
  passed = contains(implementation,
                    "property = omni_rig_frequency_property(*vfo, true)",
                    "OmniRig TX writes need a live-VFO fallback") &&
           passed;

  return passed ? 0 : 1;
}
