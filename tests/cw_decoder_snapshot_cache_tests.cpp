#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "cwassistant/core/cw_decoder.hpp"

namespace {

void expect(const bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

}  // namespace

int main() {
  cwassistant::core::CwTimingDecoder decoder;
  std::uint64_t now_ns = 0;
  const auto advance = [&](const std::uint32_t frames, const float evidence) {
    const cwassistant::core::CwDecoderUpdate* latest = nullptr;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      now_ns += 10'000'000U;
      latest = &decoder.process(now_ns, evidence);
    }
    return latest;
  };

  static_cast<void>(advance(10, 0.0F));
  static_cast<void>(advance(6, 12.0F));
  const auto* decoded = advance(40, 0.0F);
  expect(decoded != nullptr && decoded->text == "E ",
         "fixture establishes stable dynamic transcript state");

  const auto* update_address = decoded;
  const auto* text_storage = decoded->text.data();
  const auto state_bytes = decoder.stateBytes();
  for (std::uint32_t frame = 0; frame < 2'000; ++frame) {
    now_ns += 10'000'000U;
    const auto& update = decoder.process(now_ns, 0.0F);
    expect(&update == update_address,
           "per-frame decoder updates reuse one stable snapshot object");
    expect(update.text.data() == text_storage,
           "unchanged transcript storage is not rebuilt per frame");
  }
  expect(decoder.stateBytes() == state_bytes,
         "steady evidence does not grow decoder snapshot state");

  std::cout << "decoder snapshot cache: 2000 steady frames, 0 transcript "
               "storage replacements\n";
  return EXIT_SUCCESS;
}
