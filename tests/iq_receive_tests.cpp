#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/iq_receive.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

cwassistant::core::RealtimeSampleBlock iqBlock(const std::uint64_t sequence,
                                                const std::uint64_t timestamp) {
  cwassistant::core::RealtimeSampleBlock block;
  block.stream = {.kind = cwassistant::core::StreamKind::ComplexIq,
                  .sample_rate_hz = 48'000.0,
                  .center_frequency_hz = 14'000'000.0,
                  .channel_count = 1};
  block.sequence = sequence;
  block.timestamp_ns = timestamp;
  block.sample_count = 480;
  return block;
}

void testValidationAndTelemetry() {
  using namespace cwassistant::core;
  IqReceiveValidator validator;
  auto first = iqBlock(10, 1'000'000'000);
  expect(validator.validate(first) == IqBlockStatus::Accepted,
         "valid complex IQ is accepted");

  auto second = iqBlock(12, 1'020'000'000);
  expect(validator.validate(second) ==
             IqBlockStatus::AcceptedAfterDiscontinuity,
         "a sequence and timestamp gap is accepted but marked discontinuous");
  expect(validator.telemetry().accepted_blocks == 2 &&
             validator.telemetry().accepted_samples == 960 &&
             validator.telemetry().discontinuities == 1 &&
             validator.telemetry().missing_block_sequences == 1,
         "continuity telemetry counts accepted work and the missing sequence");

  auto invalid = iqBlock(13, 1'030'000'000);
  invalid.samples[7] = {std::numeric_limits<float>::quiet_NaN(), 0.0F};
  expect(validator.validate(invalid) ==
             IqBlockStatus::RejectedNonFiniteSample &&
             validator.telemetry().rejected_blocks == 1 &&
             validator.telemetry().non_finite_samples == 1,
         "non-finite provider samples fail closed with bounded diagnostics");

  auto audio = iqBlock(14, 1'040'000'000);
  audio.stream.kind = StreamKind::Audio;
  expect(validator.validate(audio) == IqBlockStatus::RejectedStreamKind,
         "the IQ boundary rejects audio blocks");
}

void testWideSpectrumCoordinates() {
  using namespace cwassistant::core;
  constexpr std::size_t fft_size = 1'024;
  constexpr std::size_t shifted_bin = 128;
  RealtimeSampleBlock block;
  block.stream = {.kind = StreamKind::ComplexIq,
                  .sample_rate_hz = 48'000.0,
                  .center_frequency_hz = 14'100'000.0,
                  .channel_count = 1};
  block.sample_count = fft_size;
  for (std::size_t index = 0; index < fft_size; ++index) {
    const double phase = 2.0 * std::numbers::pi *
                         static_cast<double>(shifted_bin * index) /
                         static_cast<double>(fft_size);
    block.samples[index] = {static_cast<float>(std::cos(phase)),
                            static_cast<float>(std::sin(phase))};
  }
  SpectrumAnalyzer analyzer({.fft_size = fft_size, .averaging_frames = 1});
  const auto spectra = analyzer.process(block);
  expect(spectra.size() == 1 && spectra.front().bins_dbfs.size() == fft_size &&
             spectra.front().lower_frequency_hz == 14'076'000.0 &&
             spectra.front().upper_frequency_hz == 14'124'000.0,
         "complex IQ exposes the complete centered hardware passband");
  const auto peak = static_cast<std::size_t>(std::distance(
      spectra.front().bins_dbfs.begin(),
      std::max_element(spectra.front().bins_dbfs.begin(),
                       spectra.front().bins_dbfs.end())));
  expect(peak == fft_size / 2 + shifted_bin,
         "wide-spectrum FFT retains the selected carrier's RF offset");
}

void testWideIqAcrossBlocksAndDiscovery() {
  using namespace cwassistant::core;
  constexpr std::size_t fft_size = 16'384;
  constexpr double sample_rate = 245'760.0;
  constexpr double center_hz = 14'100'000.0;
  constexpr std::size_t shifted_bin = 120;
  constexpr double carrier_hz =
      center_hz + shifted_bin * sample_rate / fft_size;

  SpectrumAnalyzer analyzer({.fft_size = fft_size,
                             .averaging_frames = 1,
                             .frame_rate_hz = 60});
  std::vector<SpectrumSnapshot> spectra;
  std::uint64_t sample_offset = 0;
  for (std::uint64_t block_index = 0; block_index < 4; ++block_index) {
    RealtimeSampleBlock block;
    block.stream = {.kind = StreamKind::ComplexIq,
                    .sample_rate_hz = sample_rate,
                    .center_frequency_hz = center_hz,
                    .channel_count = 1};
    block.sequence = block_index;
    block.timestamp_ns = static_cast<std::uint64_t>(
        static_cast<long double>(sample_offset) * 1'000'000'000.0L /
        sample_rate);
    block.sample_count = block.samples.size();
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      const double phase = 2.0 * std::numbers::pi * shifted_bin *
                           static_cast<double>(sample_offset + index) /
                           static_cast<double>(fft_size);
      block.samples[index] = {static_cast<float>(std::cos(phase)),
                              static_cast<float>(std::sin(phase))};
    }
    sample_offset += block.sample_count;
    auto produced = analyzer.process(block);
    spectra.insert(spectra.end(), produced.begin(), produced.end());
  }

  expect(spectra.size() == 1 && spectra.front().bins_dbfs.size() == fft_size,
         "a bounded 16384-point SDR FFT accumulates across input blocks");
  if (spectra.empty()) {
    return;
  }
  const auto peak = static_cast<std::size_t>(std::distance(
      spectra.front().bins_dbfs.begin(),
      std::max_element(spectra.front().bins_dbfs.begin(),
                       spectra.front().bins_dbfs.end())));
  expect(peak == fft_size / 2 + shifted_bin &&
             std::abs((spectra.front().lower_frequency_hz +
                       peak * spectra.front().bin_width_hz) -
                      carrier_hz) < 0.01 &&
             std::abs(spectra.front().bin_width_hz - 15.0) < 1.0e-9,
         "wide IQ retains exact absolute RF coordinates at 15 Hz bins");

  CwChannelBank bank({.acquisition_snr_db = 3.0F,
                      .minimum_peak_prominence_db = 0.0F,
                      .minimum_near_peak_prominence_db = 0.0F,
                      .minimum_spectral_observations = 1,
                      .minimum_verification_symbols = 0});
  static_cast<void>(bank.updateSpectrum(
      spectra.front().timestamp_ns, spectra.front().lower_frequency_hz,
      spectra.front().upper_frequency_hz, spectra.front().bins_dbfs));
  const auto selected_track = bank.selectFrequency(carrier_hz);
  const bool selected = std::any_of(
      bank.channels().cbegin(), bank.channels().cend(),
      [selected_track, &spectra](const CwChannelSnapshot& channel) {
        return channel.id == selected_track &&
               std::abs(channel.frequency_hz - carrier_hz) <
                   spectra.front().bin_width_hz;
      });
  expect(selected,
         "existing channel bank accepts an absolute-RF selection from wide IQ");
}

void testChannelizerBridge() {
  using namespace cwassistant::core;
  IqToAudioChannelizer bridge({.target_frequency_hz = 14'004'000.0,
                               .output_sample_rate_hz = 12'000.0,
                               .output_tone_hz = 703.125,
                               .channel_bandwidth_hz = 500.0});
  RealtimeSampleBlock input;
  input.stream = {.kind = StreamKind::ComplexIq,
                  .sample_rate_hz = 48'000.0,
                  .center_frequency_hz = 14'000'000.0,
                  .channel_count = 1};
  input.sample_count = input.samples.size();
  for (std::size_t index = 0; index < input.sample_count; ++index) {
    const double selected_phase =
        2.0 * std::numbers::pi * 4'000.0 * static_cast<double>(index) /
        input.stream.sample_rate_hz;
    const double adjacent_phase =
        2.0 * std::numbers::pi * 5'500.0 * static_cast<double>(index) /
        input.stream.sample_rate_hz;
    input.samples[index] =
        std::complex<float>{static_cast<float>(0.8 * std::cos(selected_phase) +
                                              0.5 * std::cos(adjacent_phase)),
                            static_cast<float>(0.8 * std::sin(selected_phase) +
                                              0.5 * std::sin(adjacent_phase))};
  }

  RealtimeSampleBlock audio;
  expect(bridge.process(input, audio) == IqBlockStatus::Accepted &&
             audio.stream.kind == StreamKind::Audio &&
             audio.stream.sample_rate_hz == 12'000.0 &&
             audio.sample_count == 1'024,
         "channelizer emits a bounded decoder-compatible audio block");

  SpectrumAnalyzer analyzer({.fft_size = 512, .averaging_frames = 1});
  const auto spectra = analyzer.process(audio);
  expect(!spectra.empty(), "channelized audio feeds the existing analyzer");
  if (!spectra.empty()) {
    const auto& bins = spectra.back().bins_dbfs;
    const std::size_t selected_bin = 30;
    const std::size_t adjacent_bin = 94;
    expect(bins[selected_bin] > -8.0F,
           "selected RF carrier is reconstructed at the configured CW tone");
    expect(bins[adjacent_bin] < bins[selected_bin] - 25.0F,
           "narrow channel filter suppresses an adjacent RF carrier");
  }
  const auto telemetry = bridge.telemetry();
  expect(telemetry.input.accepted_blocks == 1 &&
             telemetry.output_blocks == 1 &&
             telemetry.output_samples == 1'024,
         "bridge publishes input and output workload telemetry");

  input.sequence = 2;
  input.timestamp_ns = 2'000'000'000;
  expect(bridge.process(input, audio) ==
             IqBlockStatus::AcceptedAfterDiscontinuity &&
             bridge.telemetry().state_resets >= 2,
         "capture gaps reset phase and filter state instead of joining samples");
}

void testConfigurationBounds() {
  using namespace cwassistant::core;
  IqToAudioChannelizer bridge;
  expect(!bridge.configure({.target_frequency_hz = 14'000'000.0,
                            .output_sample_rate_hz = 8'000.0,
                            .output_tone_hz = 3'900.0,
                            .channel_bandwidth_hz = 500.0}),
         "audio tone plus channel width must remain below output Nyquist");
  RealtimeSampleBlock input = iqBlock(0, 0);
  input.stream.sample_rate_hz = 16'000.0;
  input.sample_count = 160;
  IqToAudioChannelizer outside({.target_frequency_hz = 14'009'000.0,
                                .output_sample_rate_hz = 8'000.0,
                                .output_tone_hz = 700.0,
                                .channel_bandwidth_hz = 500.0});
  RealtimeSampleBlock output;
  expect(outside.process(input, output) ==
             IqBlockStatus::RejectedDescriptor &&
             output.sample_count == 0,
         "a requested channel outside the captured passband fails closed");

  SpectrumAnalyzer analyzer;
  expect(analyzer.configure({.fft_size = 16'384, .averaging_frames = 1}),
         "largest bounded SDR transform is accepted");
  expect(!analyzer.configure({.fft_size = 32'768, .averaging_frames = 1}),
         "an SDR transform above the resource bound is rejected");
}

}  // namespace

int main() {
  testValidationAndTelemetry();
  testWideSpectrumCoordinates();
  testWideIqAcrossBlocksAndDiscovery();
  testChannelizerBridge();
  testConfigurationBounds();
  if (failures == 0) {
    std::cout << "All IQ receive tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
