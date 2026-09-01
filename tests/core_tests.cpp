#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "cwassistant/core/adif.hpp"
#include "cwassistant/core/callsign_policy.hpp"
#include "cwassistant/core/cat4om_protocol.hpp"
#include "cwassistant/core/channel_scheduler.hpp"
#include "cwassistant/core/cw_channel_bank.hpp"
#include "cwassistant/core/cw_decoder.hpp"
#include "cwassistant/core/frequency_plan.hpp"
#include "cwassistant/core/remote_control.hpp"
#include "cwassistant/core/reference_rig_profiles.hpp"
#include "cwassistant/core/spectrum_visualization_settings.hpp"
#include "cwassistant/core/spectrum_analyzer.hpp"
#include "cwassistant/core/station_equipment.hpp"
#include "cwassistant/core/spsc_ring_buffer.hpp"
#include "cwassistant/core/transmit_guard.hpp"
#include "cwassistant/core/wav_replay_source.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void write_u16(std::ostream& stream, const std::uint16_t value) {
  stream.put(static_cast<char>(value & 0xFFU));
  stream.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write_u32(std::ostream& stream, const std::uint32_t value) {
  stream.put(static_cast<char>(value & 0xFFU));
  stream.put(static_cast<char>((value >> 8U) & 0xFFU));
  stream.put(static_cast<char>((value >> 16U) & 0xFFU));
  stream.put(static_cast<char>((value >> 24U) & 0xFFU));
}

std::filesystem::path write_test_wav() {
  constexpr std::uint32_t sample_rate = 8'000;
  constexpr std::uint16_t channels = 2;
  constexpr std::uint32_t frame_count = 5'000;
  constexpr std::uint32_t data_size = frame_count * channels * 2U;
  const auto path =
      std::filesystem::temp_directory_path() / "cwassistant-replay-test.wav";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write("RIFF", 4);
  write_u32(output, 36U + data_size);
  output.write("WAVEfmt ", 8);
  write_u32(output, 16);
  write_u16(output, 1);
  write_u16(output, channels);
  write_u32(output, sample_rate);
  write_u32(output, sample_rate * channels * 2U);
  write_u16(output, channels * 2U);
  write_u16(output, 16);
  output.write("data", 4);
  write_u32(output, data_size);
  for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
    write_u16(output, static_cast<std::uint16_t>(16'384));
    write_u16(output, static_cast<std::uint16_t>(8'192));
  }
  return path;
}

void test_ring_buffer() {
  cwassistant::core::SpscRingBuffer<int, 2> queue;
  expect(queue.empty(), "new ring is empty");
  expect(queue.try_push(10), "push first item");
  expect(queue.try_push(20), "push second item");
  expect(!queue.try_push(30), "bounded ring reports full");

  int value = 0;
  expect(queue.try_pop(value) && value == 10, "ring preserves FIFO order");
  expect(queue.try_pop(value) && value == 20, "ring pops second item");
  expect(!queue.try_pop(value), "empty ring reports empty");
}

void test_scheduler() {
  using namespace cwassistant::core;
  const std::vector<DetectedChannel> channels{
      {.id = 1, .snr_db = 4.0F, .arrival_sequence = 30},
      {.id = 2, .snr_db = 18.0F, .arrival_sequence = 20},
      {.id = 3, .snr_db = 8.0F, .arrival_sequence = 10,
       .user_selected = true},
  };
  ChannelScheduler scheduler;
  expect(scheduler.select(channels, 2, ChannelSelectionPolicy::StrongestSignal) ==
             std::vector<std::uint64_t>({2, 3}),
         "strongest policy ranks by SNR");
  expect(scheduler.select(channels, 2, ChannelSelectionPolicy::ArrivalQueue) ==
             std::vector<std::uint64_t>({3, 2}),
         "queue policy ranks by arrival");
  expect(scheduler.select(channels, 2,
                          ChannelSelectionPolicy::UserSelectedFirst) ==
             std::vector<std::uint64_t>({3, 2}),
         "manual choice is scheduled first");
}

void test_cw_timing_decoder() {
  using cwassistant::core::CwTimingDecoder;
  CwTimingDecoder decoder({.initial_wpm = 20.0});
  std::uint64_t now = 0;
  const auto feed = [&](const bool down, const int milliseconds) {
    const int steps = milliseconds / 10;
    for (int i = 0; i < steps; ++i) {
      now += 10'000'000;
      static_cast<void>(decoder.process(now, down ? 12.0F : 0.0F));
    }
  };
  feed(false, 100);
  feed(true, 60); feed(false, 60); feed(true, 60); feed(false, 200);
  feed(true, 60); feed(false, 60); feed(true, 60); feed(false, 60);
  feed(true, 60); feed(false, 200);
  const auto result = decoder.flush(now + 500'000'000);
  expect(result.text.find("IS") != std::string::npos,
         "adaptive CW timing decodes deterministic dit sequences");
  expect(result.wpm > 18.0 && result.wpm < 22.0,
         "adaptive CW timing reports the keyed speed");
  expect(result.provisional_text.empty(),
         "flush promotes every provisional character to stable text");
  expect(result.key_down_probability < 0.1F,
         "soft key evidence returns near zero after a completed signal");

  CwTimingDecoder staged_decoder({.initial_wpm = 20.0});
  std::uint64_t staged_now = 0;
  cwassistant::core::CwDecoderUpdate staged;
  const auto staged_feed = [&](const bool down, const int milliseconds) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
      staged_now += 10'000'000;
      staged = staged_decoder.process(staged_now, down ? 12.0F : 0.0F);
    }
  };
  staged_feed(false, 100);
  staged_feed(true, 60);
  staged_feed(false, 150);
  expect(staged.text.empty() && staged.provisional_text == "E",
         "completed character is exposed provisionally before confirmation");
  staged_feed(false, 60);
  expect(staged.text == "E" && staged.provisional_text.empty(),
         "confirmation delay promotes provisional text to append-only stable text");
}

void test_cw_channel_bank() {
  using cwassistant::core::CwChannelBank;
  CwChannelBank bank({.minimum_verification_symbols = 0});
  std::vector<float> bins(101, -100.0F);
  std::uint64_t now = 0;
  double phase_low = 0.0;
  double phase_high = 0.0;
  constexpr double sample_rate = 8'000.0;
  const auto feed = [&](const bool low_tone, const bool high_tone,
                        const int milliseconds) {
    const int steps = milliseconds / 10;
    for (int step = 0; step < steps; ++step) {
      bins.assign(bins.size(), -100.0F);
      if (low_tone) bins[30] = -80.0F;
      if (high_tone) bins[70] = -78.0F;
      static_cast<void>(bank.updateSpectrum(now, 0.0, 1'000.0, bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = now;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        const float sample =
            (low_tone ? 0.35F * static_cast<float>(std::sin(phase_low))
                      : 0.0F) +
            (high_tone ? 0.35F * static_cast<float>(std::sin(phase_high))
                       : 0.0F);
        block.samples[index] = {sample, 0.0F};
        phase_low += 2.0 * std::numbers::pi * 300.0 / sample_rate;
        phase_high += 2.0 * std::numbers::pi * 700.0 / sample_rate;
      }
      static_cast<void>(bank.processSamples(block));
      now += 10'000'000;
    }
  };

  feed(true, true, 60);
  feed(false, true, 120);
  feed(false, false, 350);
  const auto& channels = bank.channels();
  expect(channels.size() == 2,
         "full-passband channel bank retains two independent CW signals");
  if (channels.size() == 2) {
    const auto low_id = channels[0].id;
    const auto high_id = channels[1].id;
    const auto low_color = channels[0].color_index;
    const auto high_color = channels[1].color_index;
    expect(std::abs(channels[0].frequency_hz - 300.0) < 5.0 &&
               (channels[0].text + channels[0].provisional_text)
                       .find('E') != std::string::npos,
           "lower-frequency slice decodes its own dit");
    expect(std::abs(channels[1].frequency_hz - 700.0) < 5.0 &&
               (channels[1].text + channels[1].provisional_text)
                       .find('T') != std::string::npos,
           "upper-frequency slice decodes its own dah");
    expect(channels[0].color_index != channels[1].color_index,
           "simultaneous tracks receive stable distinct colors");
    feed(false, false, 1'000);
    const auto& held = bank.channels();
    expect(held.size() == 2 && held[0].id == low_id &&
               held[1].id == high_id && held[0].color_index == low_color &&
               held[1].color_index == high_color,
           "frequency track identity and colors survive keyed gaps");
    feed(false, false, 7'200);
    expect(bank.channels().empty(),
           "silent decoded tracks expire from the full-spectrum model");
  }

  CwChannelBank rejection_bank;
  bins.assign(bins.size(), -100.0F);
  bins[70] = -75.0F;
  static_cast<void>(rejection_bank.updateSpectrum(
      0, 0.0, 1'000.0, bins));
  double interference_phase = 0.0;
  for (int step = 0; step < 30; ++step) {
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = static_cast<std::uint64_t>(step) * 10'000'000;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          0.45F * static_cast<float>(std::sin(interference_phase)), 0.0F};
      interference_phase +=
          2.0 * std::numbers::pi * 880.0 / sample_rate;
    }
    static_cast<void>(rejection_bank.processSamples(block));
  }
  expect(rejection_bank.channels().empty(),
         "an adjacent non-tracked carrier is never published as verified CW");

  CwChannelBank shaped_noise_bank;
  std::uint32_t noise_state = 0x13579BDFU;
  double noise_time = 0.0;
  for (int step = 0; step < 500; ++step) {
    for (std::size_t bin = 0; bin < bins.size(); ++bin) {
      bins[bin] = -88.0F +
          8.0F * static_cast<float>(std::sin(0.08 * bin + noise_time)) +
          2.0F * static_cast<float>(std::sin(0.91 * bin - noise_time));
    }
    const auto timestamp = static_cast<std::uint64_t>(step) * 10'000'000;
    static_cast<void>(shaped_noise_bank.updateSpectrum(
        timestamp, 0.0, 1'000.0, bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = timestamp;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      noise_state = noise_state * 1'664'525U + 1'013'904'223U;
      const float noise = static_cast<float>((noise_state >> 8U) & 0xFFFFU) /
                              32'767.5F -
                          1.0F;
      block.samples[index] = {0.18F * noise, 0.0F};
    }
    static_cast<void>(shaped_noise_bank.processSamples(block));
    noise_time += 0.03;
  }
  expect(shaped_noise_bank.channels().empty(),
         "five seconds of shaped broadband noise publishes no CW traces");

  CwChannelBank drift_bank({.minimum_verification_symbols = 0});
  std::vector<float> fine_bins(1'001, -110.0F);
  double drifting_phase = 0.0;
  constexpr double initial_tone_hz = 500.0;
  constexpr double requested_drift_hz_per_second = 40.0;
  for (int step = 0; step < 120; ++step) {
    const double elapsed = static_cast<double>(step) * 0.01;
    const double tone_hz = initial_tone_hz +
                           requested_drift_hz_per_second * elapsed;
    fine_bins.assign(fine_bins.size(), -110.0F);
    fine_bins[static_cast<std::size_t>(std::llround(tone_hz))] = -68.0F;
    const auto timestamp = static_cast<std::uint64_t>(step) * 10'000'000;
    static_cast<void>(drift_bank.updateSpectrum(
        timestamp, 0.0, 1'000.0, fine_bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = timestamp;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          0.32F * static_cast<float>(std::sin(drifting_phase)), 0.0F};
      drifting_phase += 2.0 * std::numbers::pi * tone_hz / sample_rate;
    }
    static_cast<void>(drift_bank.processSamples(block));
  }
  expect(drift_bank.channels().size() == 1,
         "a steadily drifting signal retains one channel identity");
  if (!drift_bank.channels().empty()) {
    const auto& drifting = drift_bank.channels().front();
    expect(std::abs(drifting.frequency_hz - 547.6) < 4.0,
           "sub-bin tracker follows the current drifting tone frequency");
    expect(drifting.drift_hz_per_second > 20.0 &&
               drifting.drift_hz_per_second < 65.0,
           "frequency tracker reports a bounded tone drift estimate");
    expect(drifting.filter_width_hz == 240.0,
           "automatic narrowband selection widens for a fast drifting tone");
  }

  CwChannelBank slow_bank;
  double slow_phase = 0.0;
  std::vector<bool> slow_keying;
  const auto append_units = [&slow_keying](const bool keyed, const int units) {
    slow_keying.insert(slow_keying.end(), units * 10, keyed);
  };
  const auto append_letter = [&append_units](const std::string_view elements) {
    for (std::size_t index = 0; index < elements.size(); ++index) {
      append_units(true, elements[index] == '.' ? 1 : 3);
      append_units(false, index + 1 == elements.size() ? 3 : 1);
    }
  };
  append_letter("...");
  append_letter("---");
  append_letter("...");
  append_units(false, 4);  // Complete the seven-unit word gap.
  const int slow_steps = static_cast<int>(slow_keying.size()) * 5;
  for (int step = 0; step < slow_steps; ++step) {
    const bool keyed = slow_keying[static_cast<std::size_t>(step) %
                                    slow_keying.size()];
    fine_bins.assign(fine_bins.size(), -110.0F);
    if (keyed) fine_bins[400] = -68.0F;
    const auto timestamp = static_cast<std::uint64_t>(step) * 10'000'000;
    static_cast<void>(slow_bank.updateSpectrum(
        timestamp, 0.0, 1'000.0, fine_bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = timestamp;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          keyed ? 0.25F * static_cast<float>(std::sin(slow_phase)) : 0.0F,
          0.0F};
      slow_phase += 2.0 * std::numbers::pi * 400.0 / sample_rate;
    }
    static_cast<void>(slow_bank.processSamples(block));
  }
  expect(!slow_bank.channels().empty(),
         "clean slow keyed signal retains a tracked channel");
  if (!slow_bank.channels().empty()) {
    const auto& verified = slow_bank.channels().front();
    expect(verified.filter_width_hz == 60.0,
           "automatic narrowband selection narrows a clean slow signal");
    expect(verified.verification_state ==
               cwassistant::core::CwTrackState::Verified &&
               verified.verification_confidence >= 0.55F &&
               verified.verification_cadence_quality >= 0.55F &&
               verified.verification_timing_quality >= 0.55F &&
               verified.verification_character_confidence >= 0.55F &&
               verified.key_transitions >= 6,
           "published CW exposes the evidence that verified its cadence");
    expect(verified.characters.size() >= 3 &&
               verified.characters.back().known,
           "stable decoded characters retain bounded per-character evidence");
  }
  const auto slow_diagnostics = slow_bank.verificationDiagnostics();
  expect(slow_diagnostics.verified_tracks == 1 &&
             slow_diagnostics.verified_transitions == 1,
         "verification diagnostics report the candidate lifecycle transition");

  cwassistant::core::SpectrumAnalyzer pipeline_analyzer({
      .fft_size = 2'048,
      .averaging_frames = 1,
      .frame_rate_hz = 60,
      .audio_dc_rejection = true,
      .audio_automatic_gain = false,
      .audio_gain_db = 0.0F,
      .audio_automatic_gain_target_dbfs = -12.0F,
      .audio_automatic_bandwidth = false,
      .audio_lower_frequency_hz = 0.0,
      .audio_upper_frequency_hz = 24'000.0,
  });
  CwChannelBank pipeline_bank({.minimum_spectral_observations = 1,
                               .minimum_verification_symbols = 0});
  cwassistant::core::RealtimeSampleBlock pipeline_block;
  pipeline_block.stream.sample_rate_hz = 48'000.0;
  pipeline_block.sample_count = 2'048;
  for (std::size_t index = 0; index < pipeline_block.sample_count; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> * 1'000.0F *
                        static_cast<float>(index) / 48'000.0F;
    pipeline_block.samples[index] = {0.5F * std::sin(phase), 0.0F};
  }
  const auto pipeline_frames = pipeline_analyzer.process(pipeline_block);
  for (const auto& frame : pipeline_frames) {
    static_cast<void>(pipeline_bank.updateSpectrum(
        frame.timestamp_ns, frame.lower_frequency_hz,
        frame.upper_frequency_hz, frame.bins_dbfs));
  }
  static_cast<void>(pipeline_bank.processSamples(pipeline_block));
  expect(pipeline_frames.size() == 1 && pipeline_bank.channels().size() == 1 &&
             std::abs(pipeline_bank.channels().front().frequency_hz -
                      1'000.0) < 30.0 &&
             pipeline_bank.channels().front().snr_db > 6.0F,
         "shared FFT discovery feeds raw narrowband channel evidence");
}

void test_transmit_guard() {
  using cwassistant::core::CallsignPolicy;
  using cwassistant::core::TransmitGuard;
  CallsignPolicy policy;
  TransmitGuard guard(policy);
  expect(!guard.begin_transmission(), "cannot transmit while disarmed");
  expect(guard.arm(), "operator can arm TX");
  expect(guard.request_qso("i1abc"), "valid selected call requests QSO");
  expect(!guard.confirm("I1XYZ"), "confirmation must match selected call");
  expect(guard.confirm("I1ABC"), "operator confirms selected call");
  expect(guard.begin_transmission(), "confirmed QSO permits TX");
  expect(guard.finish_transmission(), "TX completes back to armed state");
  expect(policy.add_ignored("w1aw"), "operator can ignore a callsign");
  expect(!guard.request_qso("W1AW"), "ignored callsign cannot request QSO");
  expect(guard.request_qso("K1ABC"), "another call can request QSO");
  expect(policy.add_ignored("K1ABC"), "pending call can become ignored");
  expect(!guard.confirm("K1ABC"), "new ignore rule cancels confirmation");
  expect(guard.state() == cwassistant::core::TransmitState::Armed,
         "ignore rule returns TX guard to armed state");
  guard.trip_fault();
  expect(!guard.arm(), "fault cannot be bypassed by arming");
  expect(guard.reset_fault(), "fault reset returns to disarmed");
}

void test_callsign_policy() {
  using cwassistant::core::CallsignPolicy;
  CallsignPolicy policy;
  expect(policy.add_ignored("  i1abc/p  "), "ignore list normalizes callsign");
  expect(policy.is_ignored("I1ABC/P"), "ignore matching is case insensitive");
  expect(!policy.add_ignored("I1ABC/P"), "ignore list rejects duplicates");
  expect(!policy.add_ignored("NOT A CALL"), "ignore list rejects invalid text");
  expect(policy.remove_ignored("i1abc/p"), "ignored callsign can be restored");
  expect(!policy.is_ignored("I1ABC/P"), "removed call is no longer ignored");
  expect(CallsignPolicy::latest_in_text("CQ TEST DE iu0lfq/p K") ==
             std::optional<std::string>("IU0LFQ/P"),
         "latest decoded callsign extraction supports portable calls");
  expect(!CallsignPolicy::latest_in_text("CQ TEST 599 ?"),
         "reports and operating words are not mistaken for callsigns");
  expect(!CallsignPolicy::latest_complete_in_text("CQ IU0LF"),
         "an unfinished decoded callsign is not presented as confirmed");
  expect(CallsignPolicy::latest_complete_in_text("CQ IU0LFQ ") ==
             std::optional<std::string>("IU0LFQ"),
         "a stable word gap confirms a structurally valid decoded callsign");
}

void test_spectrum_settings() {
  cwassistant::core::SpectrumVisualizationSettings settings{
      .target_fps = 500,
      .waterfall_lines_per_second = 0,
      .lower_bound_db = -20.0F,
      .upper_bound_db = -19.0F,
      .averaging_frames = 100,
  };
  const auto safe = settings.sanitized();
  expect(safe.target_fps == 120, "spectrum FPS is bounded");
  expect(safe.waterfall_lines_per_second == 1,
         "waterfall speed is independently bounded");
  expect(safe.upper_bound_db - safe.lower_bound_db >= 10.0F,
         "manual range retains visible span");
  expect(safe.averaging_frames == 32, "averaging is bounded");
}

void test_wav_replay_source() {
  using namespace std::chrono_literals;
  using namespace cwassistant::core;
  const auto path = write_test_wav();
  WavReplaySource source;
  expect(source.open(path.string(), {}), "PCM16 stereo WAV opens for replay");
  expect(source.stream_descriptor().sample_rate_hz == 8'000.0 &&
             source.stream_descriptor().channel_count == 1,
         "WAV replay exposes deterministic mono output metadata");
  expect(source.total_frames() == 5'000 &&
             std::abs(source.duration_seconds() - 0.625) < 1.0e-9,
         "WAV replay reports exact frame count and duration");
  expect(source.start(), "WAV replay starts from frame zero");

  RealtimeSampleBlock first;
  expect(source.read(first, 0ms) && first.sample_count == 4'096 &&
             first.sequence == 0 && first.timestamp_ns == 0,
         "first WAV block has bounded size and deterministic origin");
  expect(std::abs(first.samples[0].real() - 0.375F) < 1.0e-6F,
         "stereo WAV channels downmix to normalized mono");

  RealtimeSampleBlock second;
  expect(source.read(second, 0ms) && second.sample_count == 904 &&
             second.sequence == 1 && second.timestamp_ns == 512'000'000,
         "second WAV block timestamp derives exactly from frame position");
  expect(!source.read(second, 0ms), "WAV replay stops cleanly at end of file");
  expect(source.start() && source.read(first, 0ms) && first.sequence == 0,
         "restarting WAV replay is deterministic");
  source.stop();
  std::error_code removal_error;
  std::filesystem::remove(path, removal_error);
}

void test_spectrum_analyzer() {
  using namespace cwassistant::core;
  constexpr std::size_t fft_size = 1'024;
  constexpr std::size_t tone_bin = 75;
  RealtimeSampleBlock block;
  block.stream = {.kind = StreamKind::Audio,
                  .sample_rate_hz = 48'000.0,
                  .center_frequency_hz = 0.0,
                  .channel_count = 1};
  block.sample_count = fft_size;
  for (std::size_t index = 0; index < fft_size; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(tone_bin * index) /
                        static_cast<float>(fft_size);
    block.samples[index] = {std::sin(phase), 0.0F};
  }

  SpectrumAnalyzer analyzer({.fft_size = fft_size, .averaging_frames = 1});
  const auto snapshots = analyzer.process(block);
  expect(snapshots.size() == 1 && snapshots[0].bins_dbfs.size() == 513,
         "audio FFT emits one-sided bins including Nyquist");
  const auto peak = static_cast<std::size_t>(std::distance(
      snapshots[0].bins_dbfs.begin(),
      std::max_element(snapshots[0].bins_dbfs.begin(),
                       snapshots[0].bins_dbfs.end())));
  expect(peak == tone_bin, "windowed FFT locates a bin-centered CW tone");
  expect(std::abs(snapshots[0].bins_dbfs[peak]) < 0.05F,
         "window coherent-gain normalization reports a full-scale tone near 0 dBFS");
  expect(snapshots[0].lower_frequency_hz == 0.0 &&
             snapshots[0].upper_frequency_hz == 24'000.0 &&
             std::abs(snapshots[0].bin_width_hz - 46.875) < 1.0e-9,
         "audio FFT publishes exact frequency coordinates");

  SpectrumAnalyzer conditioned(
      {.fft_size = fft_size,
       .averaging_frames = 1,
       .audio_dc_rejection = true,
       .audio_automatic_gain = true,
       .audio_gain_db = 0.0F,
       .audio_automatic_gain_target_dbfs = -6.0F,
       .audio_automatic_bandwidth = false,
       .audio_lower_frequency_hz = 300.0,
       .audio_upper_frequency_hz = 3'000.0});
  constexpr std::size_t conditioned_tone_bin = 16;
  for (std::size_t index = 0; index < fft_size; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(conditioned_tone_bin * index) /
                        static_cast<float>(fft_size);
    block.samples[index] = {0.25F + 0.25F * std::sin(phase), 0.0F};
  }
  const auto conditioned_snapshots = conditioned.process(block);
  expect(conditioned_snapshots.size() == 1 &&
             conditioned_snapshots[0].bins_dbfs.size() == 58 &&
             conditioned_snapshots[0].lower_frequency_hz == 328.125 &&
             conditioned_snapshots[0].upper_frequency_hz == 3'000.0,
         "manual audio bandwidth crops bins and reports exact displayed coordinates");
  const auto conditioned_peak = std::max_element(
      conditioned_snapshots[0].bins_dbfs.begin(),
      conditioned_snapshots[0].bins_dbfs.end());
  expect(conditioned_peak != conditioned_snapshots[0].bins_dbfs.end() &&
             std::abs(*conditioned_peak + 6.0F) < 0.1F,
         "automatic input gain reaches its configured dBFS target after DC rejection");

  SpectrumAnalyzer dc_rejected(
      {.fft_size = fft_size,
       .averaging_frames = 1,
       .audio_dc_rejection = true});
  const auto dc_rejected_snapshots = dc_rejected.process(block);
  expect(dc_rejected_snapshots.size() == 1 &&
             dc_rejected_snapshots[0].bins_dbfs.front() < -100.0F,
         "audio DC rejection removes a constant left-edge spectral peak");

  SpectrumAnalyzer automatic_bandwidth(
      {.fft_size = fft_size,
       .averaging_frames = 1,
       .audio_dc_rejection = true,
       .audio_automatic_gain = false,
       .audio_gain_db = 0.0F,
       .audio_automatic_gain_target_dbfs = -12.0F,
       .audio_automatic_bandwidth = true});
  const auto automatic_snapshots = automatic_bandwidth.process(block);
  expect(automatic_snapshots.size() == 1 &&
             automatic_snapshots[0].lower_frequency_hz == 140.625 &&
             automatic_snapshots[0].upper_frequency_hz == 3'000.0,
         "automatic audio bandwidth derives a CW-oriented view from sample rate");

  expect(!analyzer.configure({.fft_size = 1'000, .averaging_frames = 1}),
         "spectrum analyzer rejects a non-radix-two transform");

  RealtimeSampleBlock overlap_block = block;
  overlap_block.sample_count = 2'048;
  for (std::size_t index = 0; index < overlap_block.sample_count; ++index) {
    const float phase = 2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(tone_bin * index) /
                        static_cast<float>(fft_size);
    overlap_block.samples[index] = {std::sin(phase), 0.0F};
  }
  SpectrumAnalyzer overlapping(
      {.fft_size = fft_size, .averaging_frames = 1, .frame_rate_hz = 120});
  const auto overlap_snapshots = overlapping.process(overlap_block);
  expect(overlap_snapshots.size() == 3 &&
             overlap_snapshots[1].timestamp_ns == 8'333'333,
         "overlapping FFT hops add genuine high-rate waterfall timing frames");
  overlap_block.sample_count = fft_size;
  overlap_block.timestamp_ns = 2'000'000'000;
  const auto after_gap = overlapping.process(overlap_block);
  expect(after_gap.size() == 1 &&
             after_gap.front().timestamp_ns == overlap_block.timestamp_ns,
         "analysis resets overlap at a capture gap so waterfall time is not compressed");
}

void test_remote_control_lease() {
  using namespace std::chrono_literals;
  using cwassistant::core::ControlLeaseManager;
  ControlLeaseManager leases;
  const auto start = ControlLeaseManager::TimePoint{};

  expect(leases.acquire("client-a", "rig-1", start, 10s),
         "first remote client acquires rig lease");
  expect(!leases.acquire("client-b", "rig-1", start, 10s),
         "second client cannot steal active rig lease");
  expect(leases.acquire("client-b", "rig-2", start, 10s),
         "different rigs have independent leases");
  expect(leases.owns("client-a", "rig-1", start + 1s),
         "lease owner is recognized");
  expect(leases.renew("client-a", "rig-1", start + 1s, 20s),
         "lease owner can renew heartbeat");
  expect(leases.owns("client-a", "rig-1", start + 15s),
         "renewed lease remains active");
  expect(!leases.owns("client-a", "rig-1", start + 22s),
         "lease expires without heartbeat");
  expect(leases.acquire("client-b", "rig-1", start + 22s, 1ms),
         "new client can acquire expired lease");
  expect(leases.owns("client-b", "rig-1", start + 23s),
         "minimum TTL clamp prevents unsafe instant expiry");
  expect(!leases.release("client-a", "rig-1"),
         "non-owner cannot release another lease");
  expect(leases.release("client-b", "rig-1"),
         "owner can explicitly release lease");
}

void test_adif() {
  const cwassistant::core::QsoRecord qso{
      .callsign = "I1ABC",
      .qso_date = "20260830",
      .time_on = "143512",
      .band = "20M",
      .mode = "CW",
      .frequency_mhz = "14.025000",
      .rst_sent = "599",
      .rst_received = "579",
      .station_callsign = "IU0XYZ",
  };
  const auto adif = cwassistant::core::to_adif(qso);
  expect(adif.find("<CALL:5>I1ABC") != std::string::npos,
         "ADIF encodes field length");
  expect(adif.ends_with("<EOR>"), "ADIF terminates the record");
}

void test_split_transverter_and_satellite_adif() {
  using namespace cwassistant::core;
  const VfoFrequencyPlan plan{
      .rx_dial_hz = 29'900'000,
      .tx_dial_hz = 28'300'000,
      .split_enabled = true,
  };
  const TransverterOffsets offsets{
      .rx_offset_hz = 116'000'000,
      .tx_offset_hz = 407'000'000,
  };
  const auto resolved = resolve_frequencies(plan, offsets);
  expect(resolved.has_value(), "split transverter frequencies resolve");
  expect(resolved && resolved->rx_rf_hz == 145'900'000,
         "positive RX transverter offset produces actual downlink RF");
  expect(resolved && resolved->tx_rf_hz == 435'300'000,
         "independent positive TX offset produces actual uplink RF");

  QsoRecord qso{
      .callsign = "I1ABC",
      .qso_date = "20260830",
      .time_on = "143512",
      .mode = "CW",
      .rst_sent = "599",
      .rst_received = "579",
      .station_callsign = "IU0XYZ",
  };
  const SatelliteQsoDetails satellite{.name = "AO-7", .mode = "U/V"};
  expect(populate_qso_frequencies(qso, plan, offsets, &satellite),
         "satellite QSO receives calculated RF fields");
  expect(qso.band == "70CM" && qso.band_rx == "2M",
         "ADIF TX and RX bands derive from actual RF frequencies");
  expect(qso.frequency_mhz == "435.300000" &&
             qso.frequency_rx_mhz == "145.900000",
         "ADIF keeps exact TX and RX frequencies to one hertz");

  const auto adif = to_adif(qso);
  expect(adif.find("<BAND:4>70CM") != std::string::npos,
         "ADIF exports transmit band");
  expect(adif.find("<BAND_RX:2>2M") != std::string::npos,
         "ADIF exports receive band");
  expect(adif.find("<FREQ:10>435.300000") != std::string::npos,
         "ADIF exports actual transmit frequency");
  expect(adif.find("<FREQ_RX:10>145.900000") != std::string::npos,
         "ADIF exports actual receive frequency");
  expect(adif.find("<PROP_MODE:3>SAT") != std::string::npos &&
             adif.find("<SAT_NAME:4>AO-7") != std::string::npos &&
             adif.find("<SAT_MODE:3>U/V") != std::string::npos,
         "ADIF exports satellite propagation, name, and mode");
}

void test_negative_transverter_offset_and_invalid_frequency() {
  using namespace cwassistant::core;
  const auto resolved = resolve_frequencies(
      {.rx_dial_hz = 145'900'000, .split_enabled = false},
      {.rx_offset_hz = -116'000'000, .tx_offset_hz = -116'000'000});
  expect(resolved && resolved->rx_rf_hz == 29'900'000 &&
             resolved->tx_rf_hz == 29'900'000,
         "negative transverter offsets are supported for RX and TX");
  expect(adif_band_from_frequency(29'900'000).empty(),
         "out-of-band frequency is not mislabeled in ADIF");
  expect(!resolve_frequencies(
              {.rx_dial_hz = 10'000'000, .split_enabled = false},
              {.rx_offset_hz = -10'000'000, .tx_offset_hz = 0}),
         "offset calculation rejects zero or underflowed actual RF");
  expect(resolve_audio_tone_rf(14'074'700, 725.0, 700.0, true) ==
             std::optional<std::uint64_t>(14'074'725),
         "CW-U audio offset maps upward from the actual-RF reference");
  expect(resolve_audio_tone_rf(14'074'700, 725.0, 700.0, false) ==
             std::optional<std::uint64_t>(14'074'675),
         "CW-L audio offset maps downward from the actual-RF reference");
  expect(!resolve_audio_tone_rf(10, 800.0, 700.0, false),
         "audio-to-RF mapping rejects an underflow below zero hertz");
}

void test_band_selected_station_equipment_adif() {
  using namespace cwassistant::core;
  const std::vector<StationEquipmentRule> rules{
      {
          .bands = {"6M", "10M", "12M", "15M", "17M", "20M"},
          .equipment = {.radio = "Yaesu FT-450D", .antenna = "Dipole"},
      },
      {
          .bands = {"13CM"},
          .equipment = {
              .radio = "Microwave IF radio",
              .transverter = "DXPatrol Transverter",
              .antenna = "Offset parabolic dish",
          },
      },
  };
  const ResolvedFrequencies hf{
      .rx_rf_hz = 14'025'000,
      .tx_rf_hz = 14'025'000,
  };
  const auto hf_equipment = resolve_station_equipment(hf, rules);
  expect(hf_equipment && describe_station_rig(*hf_equipment) == "Yaesu FT-450D",
         "HF band rule selects its configured radio");
  expect(hf_equipment && describe_station_antenna(*hf_equipment) == "Dipole",
         "HF band rule selects its configured antenna");

  const ResolvedFrequencies cross_band{
      .rx_rf_hz = 14'025'000,
      .tx_rf_hz = 2'320'100'000,
      .split_enabled = true,
  };
  QsoRecord qso;
  expect(populate_qso_station_equipment(qso, cross_band, rules),
         "cross-band equipment chains resolve from actual RF bands");
  expect(qso.station_rig ==
             "TX: Microwave IF radio + DXPatrol Transverter; RX: Yaesu FT-450D",
         "different TX/RX radio chains are explicit");
  expect(qso.station_antenna ==
             "TX: Offset parabolic dish; RX: Dipole",
         "different TX/RX antennas are explicit");
  const auto adif = to_adif(qso);
  expect(adif.find("<MY_RIG:") != std::string::npos &&
             adif.find("<MY_ANTENNA:") != std::string::npos,
         "ADIF exports logging-station rig and antenna fields");
}

void test_reference_rig_profiles() {
  using namespace cwassistant::core;
  const auto profiles = reference_rig_profiles();
  expect(profiles.size() == 2, "two Yaesu reference profiles are available");

  const auto* ft450d = find_reference_rig_profile("yaesu-ft-450d");
  expect(ft450d != nullptr, "FT-450D profile is selectable");
  expect(ft450d != nullptr && ft450d->cat.baud_rate == 4'800 &&
             ft450d->cat.data_bits == 8 && ft450d->cat.stop_bits == 1,
         "FT-450D starts with documented 4800 8-N-1 CAT framing");
  expect(ft450d != nullptr && ft450d->omnirig_rig_type == "FT-450",
         "FT-450D maps to the OmniRig FT-450 command description");

  const auto* ft818 = find_reference_rig_profile("yaesu-ft-818");
  expect(ft818 != nullptr, "FT-818 profile is selectable");
  expect(ft818 != nullptr && ft818->cat.baud_rate == 4'800 &&
             ft818->cat.data_bits == 8 && ft818->cat.stop_bits == 2,
         "FT-818 starts with documented 4800 8-N-2 CAT framing");
  expect(ft818 != nullptr && ft818->omnirig_rig_type == "FT-817",
         "FT-818 uses the compatible OmniRig FT-817 command description");

  expect(ft450d != nullptr && ft450d->cat.port.empty() &&
             ft450d->keying.port.empty(),
         "reference profiles never guess physical COM ports");
  expect(ft450d != nullptr && ft450d->ptt_line != ft450d->key_line,
         "direct keying defaults PTT and KEY to different lines");
}

void test_cat4om_protocol_contract() {
  using namespace cwassistant::core;
  expect(cat4om_protocol_compatible("1.0.0") &&
             cat4om_protocol_compatible("1.99.3"),
         "CAT4OM accepts additive changes within protocol major 1");
  expect(!cat4om_protocol_compatible("2.0.0") &&
             !cat4om_protocol_compatible("invalid"),
         "CAT4OM rejects incompatible or malformed protocol versions");
  expect(cat4om_role_from_string("master") == Cat4OmRole::Master &&
             cat4om_role_from_string("new-role") == Cat4OmRole::Unknown,
         "CAT4OM roles degrade safely when a future value is unknown");

  const Cat4OmRadioState simplex{
      .radio_id = "run",
      .connection_status = "connected",
      .active_vfo = "MAIN",
      .tx_vfo = "SUB",
      .split = false,
      .vfos = {{.id = "MAIN", .frequency_hz = 14'025'000},
               {.id = "SUB", .frequency_hz = 7'010'000}},
      .available_commands = {"SetFrequency", "SetSplit"},
  };
  const auto simplex_plan = cat4om_frequency_plan(simplex);
  expect(simplex_plan && simplex_plan->rx_dial_hz == 14'025'000 &&
             simplex_plan->tx_dial_hz == 14'025'000 &&
             !simplex_plan->split_enabled,
         "CAT4OM simplex state uses the active VFO for RX and TX");
  expect(cat4om_has_command(simplex, "setfrequency"),
         "CAT4OM command capability matching tolerates case only");

  auto split = simplex;
  split.split = true;
  const auto split_plan = cat4om_frequency_plan(split);
  expect(split_plan && split_plan->rx_dial_hz == 14'025'000 &&
             split_plan->tx_dial_hz == 7'010'000 &&
             split_plan->split_enabled,
         "CAT4OM split state preserves independent opaque VFO names");
  split.tx_vfo = "missing";
  expect(!cat4om_frequency_plan(split),
         "CAT4OM refuses an incomplete split frequency snapshot");
}

}  // namespace

int main() {
  test_ring_buffer();
  test_scheduler();
  test_cw_timing_decoder();
  test_cw_channel_bank();
  test_callsign_policy();
  test_spectrum_settings();
  test_wav_replay_source();
  test_spectrum_analyzer();
  test_remote_control_lease();
  test_transmit_guard();
  test_adif();
  test_split_transverter_and_satellite_adif();
  test_negative_transverter_offset_and_invalid_frequency();
  test_band_selected_station_equipment_adif();
  test_reference_rig_profiles();
  test_cat4om_protocol_contract();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
