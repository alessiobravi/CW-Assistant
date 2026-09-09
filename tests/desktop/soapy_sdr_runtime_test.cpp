#include <SoapySDR/Device.hpp>
#include <SoapySDR/Modules.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main(const int argc, char** argv) {
  if (argc != 2) fail("expected the packaged RTL-SDR module path");
  const std::filesystem::path module_path(argv[1]);
  if (!std::filesystem::is_regular_file(module_path)) {
    fail("packaged RTL-SDR module does not exist: " + module_path.string());
  }

  const std::string load_error = SoapySDR::loadModule(module_path.string());
  if (!load_error.empty()) {
    fail("RTL-SDR module or one of its bundled DLLs could not load: " +
         load_error);
  }
  const auto registrations = SoapySDR::getLoaderResult(module_path.string());
  const auto rtl = registrations.find("rtlsdr");
  if (rtl == registrations.end()) {
    fail("loaded module did not register the rtlsdr factory");
  }
  if (!rtl->second.empty()) {
    fail("rtlsdr factory registration failed: " + rtl->second);
  }

  // Enumeration is deliberately allowed to return no hardware in CI. Calling
  // it still validates that the loaded factory and all runtime dependencies
  // remain callable without installing a separate SDR application.
  (void)SoapySDR::Device::enumerate("driver=rtlsdr");
  std::cout << "Bundled SoapySDR RTL-SDR runtime loaded successfully\n";
  return 0;
}
