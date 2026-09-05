#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
#include "cwassistant/core/wav_writer.hpp"

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

  CwTimingDecoder immediate_flush({.initial_wpm = 20.0});
  static_cast<void>(immediate_flush.process(0, 12.0F));
  const auto forced_up = immediate_flush.flush(2'000'000);
  expect(!forced_up.key_down && forced_up.key_down_probability == 0.0F,
         "flush forces key up even before probability smoothing naturally "
         "crosses the off threshold");

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

  cwassistant::core::CwMultiSpeedDecoder cadence_decoder;
  std::uint64_t cadence_now = 0;
  cwassistant::core::CwDecoderUpdate cadence;
  const auto cadence_feed = [&](const bool down, const int milliseconds) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 5) {
      cadence_now += 5'000'000;
      cadence = cadence_decoder.process(cadence_now,
                                        down ? 12.0F : 0.0F);
    }
  };
  cadence_feed(false, 200);
  for (int word = 0; word < 3; ++word) {
    for (int element = 0; element < 3; ++element) {
      cadence_feed(true, 50);
      cadence_feed(false, element == 2 ? 150 : 50);
    }
    for (int element = 0; element < 3; ++element) {
      cadence_feed(true, 150);
      cadence_feed(false, element == 2 ? 150 : 50);
    }
  }
  expect(cadence.acoustic_wpm >= 22.0 && cadence.acoustic_wpm <= 26.0 &&
             cadence.acoustic_cadence_confidence >= 0.70F,
         "independent run-length fitting derives CW speed from 1:3 marks "
         "and 1:3 gaps without using decoded characters");
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
    expect(channels[0].active && channels[1].active,
           "short Morse word gaps retain presentation activity without "
           "flickering the stream areas");
    feed(false, false, 1'000);
    const auto& held = bank.channels();
    expect(held.size() == 2 && held[0].id == low_id &&
               held[1].id == high_id && held[0].color_index == low_color &&
               held[1].color_index == high_color && !held[0].active &&
               !held[1].active && !held[0].key_down && !held[1].key_down,
           "frequency identity survives keyed gaps without presenting a "
           "retained track as active or keyed");
    feed(false, false, 3'000);
    const auto& silent_held = bank.channels();
    expect(silent_held.size() == 2 &&
               silent_held[0].id == low_id && silent_held[1].id == high_id &&
               silent_held[0].color_index == low_color &&
               silent_held[1].color_index == high_color &&
               !silent_held[0].active && !silent_held[1].active,
           "silence cannot bypass the decoded-signal retention timeout by "
           "demoting verified tracks or keeping their carrier active");
    bank.configure({.empty_track_retention_seconds = 2.0,
                    .decoded_track_retention_seconds = 2.0,
                    .minimum_verification_symbols = 0});
    feed(false, false, 2'500);
    expect(bank.channels().empty(),
           "silent decoded tracks expire from the full-spectrum model");
    // Exercise the lease close to its promised five-minute boundary without
    // making the deterministic test wait in real time.
    now += 292'000'000'000ULL;
    feed(true, false, 100);
    const auto& reacquired = bank.channels();
    expect(reacquired.size() == 1 && reacquired.front().id != low_id &&
               reacquired.front().color_index == low_color,
           "a verified frequency reuses its color after track expiry within "
           "the five-minute identity lease");
  }

  {
    CwChannelBank nearby_bank({.minimum_separation_hz = 15.0,
                               .color_identity_tolerance_hz = 35.0,
                               .minimum_spectral_observations = 1,
                               .minimum_verification_symbols = 0,
                               .track_identity_tolerance_hz = 10.0});
    std::vector<float> nearby_bins(201, -110.0F);
    std::uint64_t nearby_now = 0;
    double first_phase = 0.0;
    double second_phase = 0.0;
    for (int step = 0; step < 20; ++step) {
      nearby_bins.assign(nearby_bins.size(), -110.0F);
      nearby_bins[60] = -55.0F;
      nearby_bins[66] = -57.0F;
      static_cast<void>(nearby_bank.updateSpectrum(
          nearby_now, 0.0, 1'000.0, nearby_bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = nearby_now;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        block.samples[index] = {
            0.30F * static_cast<float>(std::sin(first_phase)) +
                0.30F * static_cast<float>(std::sin(second_phase)),
            0.0F};
        first_phase += 2.0 * std::numbers::pi * 300.0 / sample_rate;
        second_phase += 2.0 * std::numbers::pi * 330.0 / sample_rate;
      }
      static_cast<void>(nearby_bank.processSamples(block));
      nearby_now += 10'000'000;
    }
    const auto first_nearby = nearby_bank.channels();
    expect(first_nearby.size() == 2 &&
               first_nearby[0].id != first_nearby[1].id &&
               first_nearby[0].color_index != first_nearby[1].color_index,
           "simultaneous verified nearby identities keep separate retained "
           "observations and exclusive colors");
    if (first_nearby.size() == 2) {
      const auto first_id = first_nearby[0].id;
      const auto second_id = first_nearby[1].id;
      const auto first_color = first_nearby[0].color_index;
      const auto second_color = first_nearby[1].color_index;
      for (int refresh = 0; refresh < 8; ++refresh) {
        static_cast<void>(nearby_bank.updateSpectrum(
            nearby_now, 0.0, 1'000.0, nearby_bins));
        nearby_now += 10'000'000;
      }
      const auto& stable_nearby = nearby_bank.channels();
      expect(stable_nearby.size() == 2 &&
                 stable_nearby[0].id == first_id &&
                 stable_nearby[1].id == second_id &&
                 stable_nearby[0].color_index == first_color &&
                 stable_nearby[1].color_index == second_color,
             "nearby retained identities cannot overwrite or ping-pong "
             "during repeated presentation rebuilds");
    }
  }

  {
    CwChannelBank reservation_bank({
        .minimum_separation_hz = 45.0,
        .tracking_tolerance_hz = 70.0,
        .empty_track_retention_seconds = 10.0,
        .unverified_track_retention_seconds = 10.0,
        .minimum_spectral_observations = 1,
        .minimum_verification_symbols = 100,
        .track_identity_tolerance_hz = 50.0,
    });
    std::vector<float> reservation_bins(1'001, -110.0F);
    reservation_bins[500] = -55.0F;
    static_cast<void>(reservation_bank.updateSpectrum(
        0, 0.0, 1'000.0, reservation_bins));
    reservation_bins.assign(reservation_bins.size(), -110.0F);
    reservation_bins[460] = -55.0F;
    reservation_bins[540] = -56.0F;
    static_cast<void>(reservation_bank.updateSpectrum(
        10'000'000, 0.0, 1'000.0, reservation_bins));
    const auto diagnostics = reservation_bank.allTrackDiagnostics();
    const auto matched = std::count_if(
        diagnostics.cbegin(), diagnostics.cend(),
        [](const auto& track) { return track.matched; });
    expect(diagnostics.size() == 1 && matched == 1,
           "changing sidelobes inside one identity cell cannot clone an "
           "automatic carrier track");
  }

  for (const bool keep_unverified : {false, true}) {
    CwChannelBank alternating_bank({
        .minimum_separation_hz = 45.0,
        .empty_track_retention_seconds = 10.0,
        .decoded_track_retention_seconds = 10.0,
        .unverified_track_retention_seconds = 10.0,
        .minimum_spectral_observations = 1,
        .minimum_verification_symbols = static_cast<std::uint16_t>(
            keep_unverified ? 100 : 0),
        .minimum_key_transitions = 0,
        .minimum_cadence_observations = 0,
        .minimum_verification_timing_quality = 0.0F,
        .minimum_verification_cadence_quality = 0.0F,
        .minimum_character_confidence = 0.0F,
        .minimum_narrowband_coherence = 0.0F,
        .maximum_verification_unknown_fraction = 1.0F,
        .track_identity_tolerance_hz = 35.0,
        .verification_enter_seconds = 0.0,
    });
    constexpr double first_hz = 500.0;
    constexpr double neighbor_hz = 585.0;
    std::vector<float> alternating_bins(1'001, -110.0F);
    std::uint64_t alternating_now = 0;
    double first_phase = 0.0;
    double neighbor_phase = 0.0;
    const auto step = [&](const int keyed_carrier) {
      alternating_bins.assign(alternating_bins.size(), -110.0F);
      if (keyed_carrier == 0) alternating_bins[500] = -62.0F;
      if (keyed_carrier == 1) alternating_bins[585] = -58.0F;
      static_cast<void>(alternating_bank.updateSpectrum(
          alternating_now, 0.0, 1'000.0, alternating_bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = alternating_now;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        float sample = 0.0F;
        if (keyed_carrier == 0)
          sample = 0.32F * static_cast<float>(std::sin(first_phase));
        if (keyed_carrier == 1)
          sample = 0.42F * static_cast<float>(std::sin(neighbor_phase));
        block.samples[index] = {sample, 0.0F};
        first_phase += 2.0 * std::numbers::pi * first_hz / sample_rate;
        neighbor_phase +=
            2.0 * std::numbers::pi * neighbor_hz / sample_rate;
      }
      static_cast<void>(alternating_bank.processSamples(block));
      alternating_now += 10'000'000;
    };
    const auto feed = [&](const int keyed_carrier, const int milliseconds) {
      for (int elapsed = 0; elapsed < milliseconds; elapsed += 10)
        step(keyed_carrier);
    };
    const auto dits = [&](const int carrier, const int count) {
      for (int index = 0; index < count; ++index) {
        feed(carrier, 60);
        feed(-1, 180);
      }
    };

    dits(0, 12);
    step(0);
    auto diagnostics = alternating_bank.allTrackDiagnostics();
    auto first = std::find_if(diagnostics.begin(), diagnostics.end(),
                              [](const auto& track) {
                                return std::abs(track.frequency_hz - first_hz) <
                                       10.0;
                              });
    expect(first != diagnostics.end(),
           "alternating-tone fixture acquires the first carrier");
    if (first == diagnostics.end()) continue;
    const std::uint64_t first_id = first->id;
    const std::uint32_t symbols_before_gap = first->decoded_symbols;
    if (keep_unverified) {
      expect(first->verification_state ==
                 cwassistant::core::CwTrackState::MorseLikely,
             "unverified alternating-tone fixture reaches Morse-likely but "
             "stays private");
      alternating_bins.assign(alternating_bins.size(), -110.0F);
      alternating_bins[500] = -62.0F;
      alternating_bins[530] = -48.0F;
      static_cast<void>(alternating_bank.updateSpectrum(
          alternating_now, 0.0, 1'000.0, alternating_bins));
      const auto skirt_diagnostics = alternating_bank.allTrackDiagnostics();
      const auto protected_first = std::find_if(
          skirt_diagnostics.cbegin(), skirt_diagnostics.cend(),
          [first_id](const auto& track) { return track.id == first_id; });
      expect(protected_first != skirt_diagnostics.cend() &&
                 protected_first->matched &&
                 std::abs(protected_first->frequency_hz - first_hz) < 10.0,
             "a Morse-likely track reserves its carrier ahead of a stronger "
             "within-cell skirt during model acquisition");
    } else {
      expect(first->verification_state ==
                 cwassistant::core::CwTrackState::Verified,
             "verified alternating-tone fixture reaches publication");
    }

    feed(-1, 600);
    dits(0, 3);
    diagnostics = alternating_bank.allTrackDiagnostics();
    first = std::find_if(diagnostics.begin(), diagnostics.end(),
                         [first_id](const auto& track) {
                           return track.id == first_id;
                         });
    expect(first != diagnostics.end() &&
               first->decoded_symbols > symbols_before_gap,
           "a same-frequency sender resumes through a normal 600 ms word "
           "gap without freezing or replacing its decoder");

    dits(1, 10);
    diagnostics = alternating_bank.allTrackDiagnostics();
    first = std::find_if(diagnostics.begin(), diagnostics.end(),
                         [first_id](const auto& track) {
                           return track.id == first_id;
                         });
    expect(first != diagnostics.end() && !first->key_down,
           "an unmatched adjacent carrier forces the old decoder key up");
    if (first == diagnostics.end()) continue;
    const std::string frozen_text = first->text;
    const std::string frozen_provisional = first->provisional_text;
    const std::uint32_t frozen_symbols = first->decoded_symbols;
    const std::uint32_t frozen_transitions = first->key_transitions;

    dits(1, 14);
    diagnostics = alternating_bank.allTrackDiagnostics();
    first = std::find_if(diagnostics.begin(), diagnostics.end(),
                         [first_id](const auto& track) {
                           return track.id == first_id;
                         });
    expect(first != diagnostics.end() && first->text == frozen_text &&
               first->provisional_text == frozen_provisional &&
               first->decoded_symbols == frozen_symbols &&
               first->key_transitions == frozen_transitions &&
               !first->key_down,
           "verified and unverified tracks freeze all decoder output after "
           "the unmatched gap hold despite a stronger 85 Hz neighbor");

    if (keep_unverified && first != diagnostics.end()) {
      expect(!alternating_bank.acceptCharacterRefinement(
                 first_id, "NOISE", alternating_now),
             "local character evidence without a complete callsign cannot "
             "confirm a stream");
      expect(alternating_bank.acceptCharacterRefinement(
                 first_id, "CQ DE 4X5LL ", alternating_now),
             "overlap-confirmed local callsign evidence is accepted for an "
             "already Morse-likely acoustic stream");
      expect(!alternating_bank.acceptCharacterRefinement(
                 first_id, "CQ DE 4X5LL ", alternating_now),
             "retained model text cannot refresh the same acoustic evidence "
             "timestamp");
      expect(!alternating_bank.acceptCharacterRefinement(
                 first_id, "CQ DE 4X5LL ",
                 alternating_now + 2'000'000'000ULL),
             "future model evidence cannot advance verification state");
      feed(0, 700);
      diagnostics = alternating_bank.allTrackDiagnostics();
      first = std::find_if(diagnostics.begin(), diagnostics.end(),
                           [first_id](const auto& track) {
        return track.id == first_id;
      });
      expect(first != diagnostics.end() &&
                 first->verification_state ==
                     cwassistant::core::CwTrackState::Verified,
             "local character evidence confirms only after the ordinary "
             "sustained acoustic entry interval");
    }
  }

  {
    CwChannelBank replacement_bank({.empty_track_retention_seconds = 0.5,
                                    .decoded_track_retention_seconds = 10.0,
                                    .minimum_spectral_observations = 1,
                                    .minimum_verification_symbols = 0,
                                    .verification_enter_seconds = 0.0,
                                    .verification_exit_seconds = 0.0});
    std::vector<float> replacement_bins(201, -110.0F);
    std::uint64_t replacement_now = 0;
    double replacement_phase = 0.0;
    const auto replacement_step = [&](const bool keyed) {
      replacement_bins.assign(replacement_bins.size(), -110.0F);
      if (keyed) replacement_bins[60] = -55.0F;
      static_cast<void>(replacement_bank.updateSpectrum(
          replacement_now, 0.0, 1'000.0, replacement_bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = replacement_now;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        block.samples[index] = {
            keyed ? 0.40F * static_cast<float>(std::sin(replacement_phase))
                  : 0.0F,
            0.0F};
        replacement_phase +=
            2.0 * std::numbers::pi * 300.0 / sample_rate;
      }
      static_cast<void>(replacement_bank.processSamples(block));
      replacement_now += 10'000'000;
    };
    const auto replacement_mark = [&](const int steps) {
      for (int step = 0; step < steps; ++step) replacement_step(true);
    };
    const auto replacement_gap = [&](const int steps) {
      for (int step = 0; step < steps; ++step) replacement_step(false);
    };
    const auto replacement_character = [&](const std::string_view elements) {
      for (std::size_t index = 0; index < elements.size(); ++index) {
        replacement_mark(elements[index] == '.' ? 6 : 18);
        if (index + 1U < elements.size()) replacement_gap(6);
      }
      // Standard Morse spacing: three dots between characters. This fixture
      // previously used five, which is neither a character gap (three) nor a
      // word gap (seven) but exactly between them, so whether it produced a
      // word space depended on the decoder's element-length estimate being
      // wrong by a specific amount. The fixture exists to exercise the
      // replacement/inheritance lifecycle, not gap classification, so it now
      // sends unambiguous spacing.
      replacement_gap(18);
    };
    // Extend the preceding three-dot character gap to a seven-dot word gap.
    const auto replacement_word_gap = [&] { replacement_gap(24); };
    // Establish an acoustic callsign on the predecessor so the replacement
    // lifecycle test can distinguish transcript continuity from station-name
    // continuity. A new tracker may inherit readable session history, but it
    // must earn its own callsign from its own acoustic suffix.
    replacement_character("-.-.");  // C
    replacement_character("--.-");  // Q
    replacement_word_gap();
    replacement_character("-..");   // D
    replacement_character(".");     // E
    replacement_word_gap();
    replacement_character("-..");   // D
    replacement_character("-.-");   // K
    replacement_character("--..."); // 7
    replacement_character("...");   // S
    replacement_character("...");   // S
    replacement_word_gap();
    replacement_character(".");     // Close the preceding callsign token.
    const auto predecessor = replacement_bank.channels();
    expect(predecessor.size() == 1 && !predecessor.front().text.empty(),
           "replacement fixture starts with stable predecessor text");
    if (predecessor.size() == 1 && !predecessor.front().text.empty()) {
      const auto predecessor_id = predecessor.front().id;
      const auto predecessor_color = predecessor.front().color_index;
      const std::string predecessor_text = predecessor.front().text;
      expect(predecessor.front().callsign == "DK7SS",
             "replacement fixture confirms a predecessor callsign");

      replacement_bank.configure({
          .empty_track_retention_seconds = 0.5,
          .decoded_track_retention_seconds = 10.0,
          .minimum_spectral_observations = 1,
          .minimum_verification_symbols = 20,
          .verification_enter_seconds = 0.0,
          .verification_exit_seconds = 0.0});
      replacement_step(true);
      replacement_now += 1'000'000'000;
      replacement_bins.assign(replacement_bins.size(), -110.0F);
      static_cast<void>(replacement_bank.updateSpectrum(
          replacement_now, 0.0, 1'000.0, replacement_bins));

      replacement_bank.configure({
          .empty_track_retention_seconds = 0.5,
          .decoded_track_retention_seconds = 10.0,
          .minimum_spectral_observations = 1,
          .minimum_verification_symbols = 0,
          .verification_enter_seconds = 0.0,
          .verification_exit_seconds = 0.0});
      replacement_step(true);
      const auto& replacement = replacement_bank.channels();
      expect(replacement.size() == 1 &&
                 replacement.front().id != predecessor_id &&
                 replacement.front().color_index == predecessor_color &&
                 replacement.front().text == predecessor_text &&
                 replacement.front().callsign.empty(),
             "genuine replacement inherits its predecessor text exactly "
             "once and reuses the identity color without inheriting its "
             "callsign");
      replacement_step(true);
      expect(replacement_bank.channels().size() == 1 &&
                 replacement_bank.channels().front().text == predecessor_text &&
                 replacement_bank.channels().front().callsign.empty(),
             "refreshing a replacement cannot append its inherited prefix "
             "again or restore a predecessor callsign");
    }
  }

  {
    CwChannelBank admission_bank({.maximum_tracks = 2,
                                  .minimum_spectral_observations = 50});
    std::vector<float> admission_bins(1'001, -110.0F);
    admission_bins[200] = -76.0F;
    admission_bins[400] = -74.0F;
    static_cast<void>(admission_bank.updateSpectrum(
        0, 0.0, 1'000.0, admission_bins));
    expect(admission_bank.allTrackDiagnostics().size() == 2,
           "track bank reaches its configured candidate capacity");
    admission_bins[800] = -45.0F;
    static_cast<void>(admission_bank.updateSpectrum(
        20'000'000, 0.0, 1'000.0, admission_bins));
    const auto admitted = admission_bank.allTrackDiagnostics();
    const bool admitted_strong_new_peak = std::any_of(
        admitted.begin(), admitted.end(), [](const auto& track) {
          return std::abs(track.frequency_hz - 800.0) < 2.0;
        });
    expect(admitted.size() == 2 && admitted_strong_new_peak,
           "a saturated track bank replaces weak unverified occupancy with a stronger new carrier");
  }

  {
    CwChannelBank identity_bank({.minimum_spectral_observations = 3,
                                 .track_identity_tolerance_hz = 35.0});
    std::vector<float> identity_bins(1'001, -110.0F);
    for (std::uint64_t frame = 0; frame < 3; ++frame) {
      identity_bins.assign(identity_bins.size(), -110.0F);
      identity_bins[250] = -65.0F;
      static_cast<void>(identity_bank.updateSpectrum(
          frame * 20'000'000, 0.0, 1'000.0, identity_bins));
    }
    const auto original_tracks = identity_bank.allTrackDiagnostics();
    const auto original_id = original_tracks.front().id;
    identity_bins.assign(identity_bins.size(), -110.0F);
    identity_bins[300] = -55.0F;
    static_cast<void>(identity_bank.updateSpectrum(
        80'000'000, 0.0, 1'000.0, identity_bins));
    const auto separated_tracks = identity_bank.allTrackDiagnostics();
    const auto new_signal = std::min_element(
        separated_tracks.begin(), separated_tracks.end(),
        [](const auto& left, const auto& right) {
          return std::abs(left.frequency_hz - 300.0) <
                 std::abs(right.frequency_hz - 300.0);
        });
    expect(separated_tracks.size() == 2 &&
               new_signal != separated_tracks.end() &&
               new_signal->id != original_id,
           "an established track cannot carry decoder history across an identity-breaking frequency jump");
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
    expect(std::abs(drifting.presentation_frequency_hz - initial_tone_hz) <
               2.0,
           "operator marker remains anchored while internal tracking follows "
           "bounded oscillator drift");
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
               verified.verification_cadence_quality >= 0.45F &&
               verified.verification_timing_quality >= 0.55F &&
               verified.verification_character_confidence >= 0.55F &&
               verified.key_transitions >= 6,
           "published CW exposes the evidence that verified its cadence");
    expect(verified.characters.size() >= 3 &&
               verified.characters.back().known,
           "stable decoded characters retain bounded per-character evidence");
    expect(std::abs(verified.verification_timing_quality -
                    verified.verification_character_confidence) > 0.01F,
           "timing quality and character confidence are genuinely "
           "independent signals, not the same value reported twice");
  }
  const auto slow_diagnostics = slow_bank.verificationDiagnostics();
  expect(slow_diagnostics.verified_tracks == 1 &&
             slow_diagnostics.verified_transitions == 1,
         "verification diagnostics report the candidate lifecycle transition");
  const auto slow_track_diagnostics = slow_bank.allTrackDiagnostics();
  expect(slow_track_diagnostics.size() == 1 &&
             slow_track_diagnostics.front().verification_state ==
                 cwassistant::core::CwTrackState::Verified &&
             !slow_track_diagnostics.front().text.empty(),
         "per-track diagnostics for operator-consented debug capture expose "
         "full private state including decoded text");

  // A VFO retune (a known, deliberate audio-domain shift) must preserve the
  // track's identity and decoded history, unlike an unexplained jump that
  // exceeds normal tracking tolerance and would be treated as a lost track.
  const auto text_before_shift = slow_track_diagnostics.front().text;
  const auto frequency_before_shift = slow_bank.channels().front().frequency_hz;
  slow_bank.shiftTrackedFrequencies(300.0);
  expect(!slow_bank.channels().empty() &&
             std::abs(slow_bank.channels().front().frequency_hz -
                      (frequency_before_shift + 300.0)) < 0.01 &&
             std::abs(slow_bank.channels().front().presentation_frequency_hz -
                      700.0) < 2.0 &&
             slow_bank.channels().front().verification_state ==
                 cwassistant::core::CwTrackState::Verified &&
             slow_bank.channels().front().text == text_before_shift,
         "shiftTrackedFrequencies re-centers a track by exactly the given "
         "delta while preserving its verification state and decoded text");
  for (int step = 0; step < slow_steps; ++step) {
    const bool keyed = slow_keying[static_cast<std::size_t>(step) %
                                    slow_keying.size()];
    fine_bins.assign(fine_bins.size(), -110.0F);
    if (keyed) fine_bins[700] = -68.0F;
    const auto timestamp =
        static_cast<std::uint64_t>(slow_steps + step) * 10'000'000;
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
      slow_phase += 2.0 * std::numbers::pi * 700.0 / sample_rate;
    }
    static_cast<void>(slow_bank.processSamples(block));
  }
  expect(slow_bank.channels().size() == 1 &&
             slow_bank.channels().front().verification_state ==
                 cwassistant::core::CwTrackState::Verified &&
             slow_bank.channels().front().text.size() >
                 text_before_shift.size(),
         "decoding continues on the same track identity at the shifted "
         "frequency, growing its text, rather than starting a new track");

  // A large shift (an operator tuning across the band, not centering on one
  // station -- or several small shifts accumulating the same way) can carry
  // a track's audio frequency past 0 Hz, where it no longer corresponds to
  // anything real. It must be dropped outright rather than left behind as a
  // nonsensical negative-frequency candidate. Uses its own bank so the
  // retention test just below still has slow_bank's live verified track.
  {
    CwChannelBank drop_bank;
    std::vector<float> drop_bins(1'001, -110.0F);
    double drop_phase = 0.0;
    std::vector<bool> drop_keying;
    const auto append_drop_units = [&drop_keying](const bool keyed,
                                                   const int units) {
      drop_keying.insert(drop_keying.end(), units * 10, keyed);
    };
    const auto append_drop_letter =
        [&append_drop_units](const std::string_view elements) {
          for (std::size_t index = 0; index < elements.size(); ++index) {
            append_drop_units(true, elements[index] == '.' ? 1 : 3);
            append_drop_units(false,
                              index + 1 == elements.size() ? 3 : 1);
          }
        };
    append_drop_letter("...");
    append_drop_letter("---");
    append_drop_letter("...");
    append_drop_units(false, 4);
    const int drop_steps = static_cast<int>(drop_keying.size()) * 5;
    for (int step = 0; step < drop_steps; ++step) {
      const bool keyed = drop_keying[static_cast<std::size_t>(step) %
                                     drop_keying.size()];
      drop_bins.assign(drop_bins.size(), -110.0F);
      if (keyed) drop_bins[500] = -68.0F;
      const auto timestamp = static_cast<std::uint64_t>(step) * 10'000'000;
      static_cast<void>(
          drop_bank.updateSpectrum(timestamp, 0.0, 1'000.0, drop_bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = timestamp;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        block.samples[index] = {
            keyed ? 0.25F * static_cast<float>(std::sin(drop_phase)) : 0.0F,
            0.0F};
        drop_phase += 2.0 * std::numbers::pi * 500.0 / sample_rate;
      }
      static_cast<void>(drop_bank.processSamples(block));
    }
    expect(!drop_bank.channels().empty(),
           "the drop-test scenario actually creates a track before "
           "exercising the shift, so the check below is not vacuous");
    drop_bank.shiftTrackedFrequencies(-100'000.0);
    expect(drop_bank.channels().empty(),
           "a shift that would carry a track past 0 Hz drops it instead of "
           "leaving a negative-frequency candidate behind");
  }

  slow_bank.configure({.empty_track_retention_seconds = 2.0,
                       .decoded_track_retention_seconds = 2.0});
  for (int silence_step = 0; silence_step < 250; ++silence_step) {
    fine_bins.assign(fine_bins.size(), -110.0F);
    const auto timestamp = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(2 * slow_steps) + silence_step) * 10'000'000;
    static_cast<void>(slow_bank.updateSpectrum(
        timestamp, 0.0, 1'000.0, fine_bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = timestamp;
    block.sample_count = 80;
    static_cast<void>(slow_bank.processSamples(block));
  }
  expect(slow_bank.channels().empty(),
         "configure() applies a shorter decoded-signal timeout immediately "
         "to an already-verified track rather than only at construction");

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

void test_established_cw_track_reserves_its_carrier_ridge() {
  using namespace cwassistant::core;
  CwChannelBank bank({
      .minimum_separation_hz = 45.0,
      .minimum_spectral_observations = 1,
      .minimum_verification_symbols = 0,
      .minimum_key_transitions = 0,
      .minimum_cadence_observations = 0,
      .minimum_verification_timing_quality = 0.0F,
      .minimum_verification_cadence_quality = 0.0F,
      .minimum_character_confidence = 0.0F,
      .minimum_plausibility_check_characters = 10,
      .maximum_simple_character_fraction = 0.80F,
      .minimum_narrowband_coherence = 0.0F,
      .maximum_verification_unknown_fraction = 1.0F,
      .verification_enter_seconds = 0.0,
  });
  constexpr double sample_rate_hz = 8'000.0;
  constexpr double carrier_hz = 500.0;
  constexpr double stronger_skirt_hz = 530.0;
  std::vector<float> bins(1'001, -110.0F);
  std::uint64_t now = 0;
  double phase = 0.0;

  const auto step = [&](const bool keyed, const bool stronger_skirt) {
    bins.assign(bins.size(), -110.0F);
    if (keyed) {
      bins[static_cast<std::size_t>(carrier_hz)] = -65.0F;
      if (stronger_skirt) {
        bins[static_cast<std::size_t>(stronger_skirt_hz)] = -55.0F;
      }
    }
    static_cast<void>(bank.updateSpectrum(now, 0.0, 1'000.0, bins));

    RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate_hz;
    block.timestamp_ns = now;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          keyed ? 0.35F * static_cast<float>(std::sin(phase)) : 0.0F,
          0.0F};
      phase += 2.0 * std::numbers::pi * carrier_hz / sample_rate_hz;
    }
    static_cast<void>(bank.processSamples(block));
    now += 10'000'000U;
  };
  const auto feed = [&](const bool keyed, const int milliseconds,
                        const bool stronger_skirt = false) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
      step(keyed, stronger_skirt);
    }
  };

  for (int symbol = 0; symbol < 8; ++symbol) {
    feed(true, 60);
    feed(false, 60);
  }
  auto diagnostics = bank.allTrackDiagnostics();
  auto established = std::find_if(
      diagnostics.cbegin(), diagnostics.cend(), [](const auto& track) {
        return track.verification_state == CwTrackState::Verified &&
               std::abs(track.frequency_hz - carrier_hz) < 2.0;
      });
  expect(established != diagnostics.cend(),
         "ridge-reservation fixture establishes the true CW carrier");
  if (established == diagnostics.cend()) return;
  const std::uint64_t established_id = established->id;

  // Add enough deliberately implausible one-element text to demote the live
  // verification state while retaining its established identity. This is the
  // field lifecycle in which an adjacent duplicate used to appear.
  for (int symbol = 0; symbol < 30; ++symbol) {
    feed(true, 60, true);
    feed(false, 180, true);
  }
  diagnostics = bank.allTrackDiagnostics();
  established = std::find_if(
      diagnostics.cbegin(), diagnostics.cend(),
      [established_id](const auto& track) {
        return track.id == established_id;
      });
  expect(established != diagnostics.cend() &&
             established->verification_state == CwTrackState::Candidate,
         "ridge reservation survives demotion of an established identity");

  // The 30 Hz neighbor is deliberately 10 dB stronger but remains inside the
  // 45 Hz global peak-separation radius. The established decoder must reserve
  // its own nearest raw ridge before strength ranking considers that neighbor.
  feed(true, 1'200, true);
  diagnostics = bank.allTrackDiagnostics();
  established = std::find_if(
      diagnostics.cbegin(), diagnostics.cend(),
      [established_id](const auto& track) {
        return track.id == established_id;
      });
  const auto adjacent_duplicates = std::count_if(
      diagnostics.cbegin(), diagnostics.cend(), [](const auto& track) {
        return std::abs(track.frequency_hz - carrier_hz) <= 45.0;
      });
  expect(established != diagnostics.cend() &&
             std::abs(established->frequency_hz - carrier_hz) < 2.0 &&
             adjacent_duplicates == 1,
         "an established CW track retains the true carrier and does not "
         "spawn a duplicate when a stronger adjacent skirt appears");
}

void test_cw_channel_bank_state_reason_consistency() {
  using cwassistant::core::CwChannelBank;
  using cwassistant::core::CwVerificationReason;
  constexpr double sample_rate = 8'000.0;
  // An impossible symbol requirement guarantees tracks can reach
  // Morse-likely but never Verified, and default coherence/cadence
  // thresholds against a plain tone naturally flicker (confirmed by direct
  // measurement: even a clean single tone's narrowband_coherence oscillates
  // above and below its threshold from one keying edge to the next). That
  // flicker is exactly what must never leave a track reporting an
  // inconsistent state/reason pair.
  CwChannelBank bank({.minimum_verification_symbols = 1'000});
  std::vector<float> bins(1'001, -100.0F);
  double phase = 0.0;
  std::uint64_t now = 0;
  bool observed_any_morse_likely = false;

  std::vector<bool> keying;
  const auto append_units = [&keying](const bool keyed, const int units) {
    keying.insert(keying.end(), units * 10, keyed);
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
  append_units(false, 4);
  const int steps = static_cast<int>(keying.size()) * 40;
  for (int step = 0; step < steps; ++step) {
    const bool keyed = keying[static_cast<std::size_t>(step) % keying.size()];
    bins.assign(bins.size(), -100.0F);
    if (keyed) bins[700] = -68.0F;
    static_cast<void>(bank.updateSpectrum(now, 0.0, 1'000.0, bins));
    cwassistant::core::RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = now;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          keyed ? 0.32F * static_cast<float>(std::sin(phase)) : 0.0F, 0.0F};
      phase += 2.0 * std::numbers::pi * 700.0 / sample_rate;
    }
    static_cast<void>(bank.processSamples(block));
    now += 10'000'000;

    const auto diagnostics = bank.verificationDiagnostics();
    if (diagnostics.morse_likely_tracks > 0) observed_any_morse_likely = true;
    std::size_t pre_morse_likely_reasons = 0;
    std::size_t post_morse_likely_reasons = 0;
    for (std::size_t reason = 0;
         reason < diagnostics.current_reason_counts.size(); ++reason) {
      const auto count = diagnostics.current_reason_counts[reason];
      if (reason <= static_cast<std::size_t>(
                        CwVerificationReason::LowCadenceQuality)) {
        pre_morse_likely_reasons += count;
      } else if (reason <= static_cast<std::size_t>(
                                CwVerificationReason::NeedsSustainedEvidence)) {
        post_morse_likely_reasons += count;
      }
    }
    expect(pre_morse_likely_reasons == diagnostics.candidate_tracks,
           "every candidate-state track reports a pre-Morse-likely gate "
           "reason, and no other track does, at every measured instant");
    expect(post_morse_likely_reasons == diagnostics.morse_likely_tracks,
           "every Morse-likely track reports a post-Morse-likely gate "
           "reason, and no other track does, at every measured instant");
  }
  expect(observed_any_morse_likely,
         "the test scenario actually exercises the Morse-likely state at "
         "least once, so the consistency checks above are not vacuous");
}

void test_operator_selected_cw_probe() {
  using namespace cwassistant::core;
  CwChannelBank bank({.decoded_track_retention_seconds = 30.0,
                      .maximum_tracks = 2});
  std::vector<float> quiet_spectrum(101, -100.0F);

  expect(bank.selectFrequency(700.0) == 0,
         "manual probe requires a current spectrum range");
  static_cast<void>(bank.updateSpectrum(1'000'000'000ULL, 200.0, 1'200.0,
                                        quiet_spectrum));
  expect(bank.selectFrequency(150.0) == 0,
         "manual probe rejects a frequency outside the displayed passband");

  const std::uint64_t selected_id = bank.selectFrequency(700.0);
  expect(selected_id != 0 && bank.channels().size() == 1,
         "manual probe is published immediately for its operator session");
  if (!bank.channels().empty()) {
    const auto& selected = bank.channels().front();
    expect(selected.id == selected_id && selected.operator_selected,
           "manual probe retains its explicit operator-selected identity");
    expect(!selected.verified_cw &&
               selected.verification_state != CwTrackState::Verified,
           "manual selection does not bypass CW verification");
    expect(selected.callsign.empty() && selected.text.empty() &&
               selected.provisional_text.empty(),
           "a newly selected probe exposes no unverified decoded identity");
  }

  expect(bank.selectFrequency(710.0) == selected_id &&
             bank.channels().size() == 1,
         "nearby repeated clicks refresh one bounded probe");
  const std::uint64_t close_pileup_id = bank.selectFrequency(739.0);
  expect(close_pileup_id != 0 && close_pileup_id != selected_id &&
             bank.channels().size() == 2,
         "manual clicks create distinct probes for pileup lanes 29 Hz apart");

  std::vector<float> weak_spectrum(101, -100.0F);
  weak_spectrum[50] = -96.0F;  // 4 dB: below normal 7 dB acquisition.
  static_cast<void>(bank.updateSpectrum(1'100'000'000ULL, 200.0, 1'200.0,
                                        weak_spectrum));
  const auto diagnostics = bank.allTrackDiagnostics();
  const auto weak_probe = std::find_if(
      diagnostics.cbegin(), diagnostics.cend(),
      [selected_id](const CwTrackDiagnostic& diagnostic) {
        return diagnostic.id == selected_id;
      });
  expect(diagnostics.size() == 2 && weak_probe != diagnostics.cend() &&
             weak_probe->operator_selected && weak_probe->matched &&
             weak_probe->spectral_observations == 1 &&
             std::abs(weak_probe->frequency_hz - 710.0) < 0.1,
         "manual probe accumulates measured sub-threshold center evidence");
  expect(!bank.channels().empty() && !bank.channels().front().verified_cw,
         "manual weak-signal priority still does not bypass verification");

  static_cast<void>(bank.updateSpectrum(32'000'000'001ULL, 200.0, 1'200.0,
                                        quiet_spectrum));
  expect(bank.channels().empty(),
         "an unverified manual probe expires after the bounded hold");

  CwChannelBank close_lane_bank({.maximum_tracks = 3});
  static_cast<void>(close_lane_bank.updateSpectrum(
      1'000'000'000ULL, 200.0, 1'200.0, quiet_spectrum));
  const std::uint64_t upper_lane_id =
      close_lane_bank.selectFrequency(739.0);
  std::vector<float> lower_lane_spectrum(101, -100.0F);
  lower_lane_spectrum[51] = -80.0F;  // 710 Hz, 29 Hz below the click.
  static_cast<void>(close_lane_bank.updateSpectrum(
      1'100'000'000ULL, 200.0, 1'200.0, lower_lane_spectrum));
  const auto close_lane_diagnostics = close_lane_bank.allTrackDiagnostics();
  const auto upper_lane = std::find_if(
      close_lane_diagnostics.cbegin(), close_lane_diagnostics.cend(),
      [upper_lane_id](const CwTrackDiagnostic& diagnostic) {
        return diagnostic.id == upper_lane_id;
      });
  expect(close_lane_diagnostics.size() == 2 &&
             upper_lane != close_lane_diagnostics.cend() &&
             std::abs(upper_lane->frequency_hz - 739.0) < 0.1 &&
             !upper_lane->matched,
         "manual probe cannot collapse onto a stronger lane 29 Hz away");
}

void test_cw_channel_presentation_frequency_model() {
  using namespace cwassistant::core;
  CwChannelBank bank({
      .decoded_track_retention_seconds = 4.0,
      .minimum_verification_symbols = 1,
      .minimum_key_transitions = 2,
      .minimum_cadence_observations = 1,
      .minimum_verification_timing_quality = 0.0F,
      .minimum_verification_cadence_quality = 0.0F,
      .minimum_character_confidence = 0.0F,
      .minimum_narrowband_coherence = 0.0F,
      .maximum_verification_unknown_fraction = 1.0F,
      .presentation_follow_stable_seconds = 0.5,
      .verification_enter_seconds = 0.0,
  });
  constexpr double sample_rate = 8'000.0;
  constexpr double acquired_hz = 500.0;
  constexpr double carrier_hz = 558.0;
  std::vector<float> bins(1'001, -110.0F);
  std::uint64_t now = 0;
  double phase = 0.0;

  // The first acquisition is deliberately 58 Hz low. Subsequent observations
  // remain within the immutable origin's hard association radius and converge
  // the adaptive DSP center before verification.
  bins[static_cast<std::size_t>(acquired_hz)] = -68.0F;
  static_cast<void>(bank.updateSpectrum(now, 0.0, 1'000.0, bins));
  now += 10'000'000U;

  const auto step = [&](const bool keyed, const double frequency_hz,
                        const bool adjacent = false) {
    bins.assign(bins.size(), -110.0F);
    if (keyed) {
      bins[static_cast<std::size_t>(std::llround(frequency_hz))] = -68.0F;
      if (adjacent) bins[630] = -67.0F;
    }
    static_cast<void>(bank.updateSpectrum(now, 0.0, 1'000.0, bins));
    RealtimeSampleBlock block;
    block.stream.sample_rate_hz = sample_rate;
    block.timestamp_ns = now;
    block.sample_count = 80;
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      block.samples[index] = {
          keyed ? 0.32F * static_cast<float>(std::sin(phase)) : 0.0F,
          0.0F};
      phase += 2.0 * std::numbers::pi * frequency_hz / sample_rate;
    }
    static_cast<void>(bank.processSamples(block));
    now += 10'000'000U;
  };
  const auto feed = [&](const bool keyed, const int milliseconds,
                        const double frequency_hz) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10)
      step(keyed, frequency_hz);
  };
  const auto letter = [&](const std::string_view elements,
                          const double frequency_hz) {
    for (std::size_t index = 0; index < elements.size(); ++index) {
      feed(true, elements[index] == '.' ? 60 : 180, frequency_hz);
      feed(false, index + 1U == elements.size() ? 180 : 60, frequency_hz);
    }
  };
  for (int repeat = 0; repeat < 3; ++repeat) {
    letter("...", carrier_hz);
    letter("---", carrier_hz);
    letter("...", carrier_hz);
  }

  auto diagnostics = bank.allTrackDiagnostics();
  auto main_track = std::find_if(
      diagnostics.begin(), diagnostics.end(), [](const auto& track) {
        return track.verification_state == CwTrackState::Verified &&
               std::abs(track.frequency_hz - carrier_hz) < 10.0;
      });
  expect(main_track != diagnostics.end(),
         "biased acquisition still produces one verified carrier track");
  if (main_track == diagnostics.end()) return;
  const std::uint64_t main_id = main_track->id;
  expect(std::abs(main_track->identity_origin_frequency_hz - acquired_hz) < 2.0 &&
             std::abs(main_track->presentation_frequency_hz - carrier_hz) < 3.0,
         "first verification robustly centers presentation without moving identity origin");
  expect(!main_track->matched && main_track->active &&
             main_track->match_age_seconds <= 0.75 &&
             main_track->color_index == 0,
         "diagnostics expose match, activity, and stable color state");

  // Symmetric carrier jitter remains inside the deadband and cannot flicker
  // the presentation center.
  const double centered = main_track->presentation_frequency_hz;
  for (int index = 0; index < 160; ++index) {
    const double jittered = carrier_hz + (index % 2 == 0 ? -3.0 : 3.0);
    step(index % 12 < 6, jittered);
  }
  diagnostics = bank.allTrackDiagnostics();
  main_track = std::find_if(diagnostics.begin(), diagnostics.end(),
                            [main_id](const auto& track) {
                              return track.id == main_id;
                            });
  expect(main_track != diagnostics.end() &&
             std::abs(main_track->presentation_frequency_hz - centered) < 1.0,
         "bounded jitter does not move the presentation center");

  // A genuine slow carrier motion must persist beyond the robust-window window
  // and stable-time guard, then follows at the configured slew rate. Its
  // absolute center remains capped relative to the immutable identity origin.
  for (int index = 0; index < 700; ++index) {
    const double moving = carrier_hz + 9.0 *
        static_cast<double>(index) / 699.0;
    step(index % 12 < 6, moving);
  }
  diagnostics = bank.allTrackDiagnostics();
  main_track = std::find_if(diagnostics.begin(), diagnostics.end(),
                            [main_id](const auto& track) {
                              return track.id == main_id;
                            });
  expect(main_track != diagnostics.end() &&
             main_track->presentation_frequency_hz > centered + 2.0 &&
             main_track->presentation_frequency_hz <= acquired_hz + 65.01 &&
             std::abs(main_track->identity_origin_frequency_hz - acquired_hz) < 2.0,
         "sustained slow motion follows within an immutable-origin hard bound");

  // An adjacent strong carrier gets a separate candidate and cannot pull the
  // verified marker. Small cumulative innovations likewise stop at the same
  // absolute origin bound rather than moving that bound themselves.
  const double before_adjacent = main_track->presentation_frequency_hz;
  for (int index = 0; index < 80; ++index)
    step(index % 12 < 6, 565.0, true);
  for (int index = 0; index < 120; ++index)
    step(true, 565.0 + 0.5 * static_cast<double>(index));
  diagnostics = bank.allTrackDiagnostics();
  main_track = std::find_if(diagnostics.begin(), diagnostics.end(),
                            [main_id](const auto& track) {
                              return track.id == main_id;
                            });
  expect(main_track != diagnostics.end() &&
             main_track->presentation_frequency_hz <= acquired_hz + 65.01 &&
             main_track->identity_origin_frequency_hz < acquired_hz + 2.0 &&
             main_track->presentation_frequency_hz >= before_adjacent - 1.0,
         "adjacent and cumulative peaks cannot walk the identity origin or presentation cap");

  const double before_silence = main_track->presentation_frequency_hz;
  for (int index = 0; index < 120; ++index) step(false, 565.0);
  diagnostics = bank.allTrackDiagnostics();
  main_track = std::find_if(diagnostics.begin(), diagnostics.end(),
                            [main_id](const auto& track) {
                              return track.id == main_id;
                            });
  expect(main_track != diagnostics.end() && !main_track->active &&
             !main_track->key_down && main_track->match_age_seconds > 0.75 &&
             std::abs(main_track->presentation_frequency_hz - before_silence) < 0.01,
         "silence exposes inactive match age without moving presentation");

  const double dsp_before_shift = main_track->frequency_hz;
  const double origin_before_shift = main_track->identity_origin_frequency_hz;
  const double presentation_before_shift = main_track->presentation_frequency_hz;
  bank.shiftTrackedFrequencies(200.0);
  diagnostics = bank.allTrackDiagnostics();
  main_track = std::find_if(diagnostics.begin(), diagnostics.end(),
                            [main_id](const auto& track) {
                              return track.id == main_id;
                            });
  expect(main_track != diagnostics.end() &&
             std::abs(main_track->frequency_hz - (dsp_before_shift + 200.0)) < 0.01 &&
             std::abs(main_track->identity_origin_frequency_hz -
                      (origin_before_shift + 200.0)) < 0.01 &&
             std::abs(main_track->presentation_frequency_hz -
                      (presentation_before_shift + 200.0)) < 0.01,
         "known VFO retune shifts DSP, identity, and presentation exactly together");

  // Let the original track expire, then reacquire directly at the corrected
  // carrier. Its new association origin is about 58 Hz away from the old
  // biased origin, but the frozen color lease is tied to the robust verified
  // carrier center and must preserve the operator-facing color.
  feed(false, 2'200, carrier_hz + 200.0);
  for (int repeat = 0; repeat < 3; ++repeat) {
    letter("...", carrier_hz + 200.0);
    letter("---", carrier_hz + 200.0);
    letter("...", carrier_hz + 200.0);
  }
  diagnostics = bank.allTrackDiagnostics();
  const auto replacement = std::find_if(
      diagnostics.begin(), diagnostics.end(), [main_id](const auto& track) {
        return track.id != main_id &&
               track.verification_state == CwTrackState::Verified;
      });
  expect(replacement != diagnostics.end() && replacement->color_index == 0 &&
             std::abs(replacement->presentation_frequency_hz -
                      (carrier_hz + 200.0)) < 3.0,
         "reacquisition after a biased origin preserves the verified-carrier color lease");
}

void test_cw_channel_bank_implausible_character_distribution() {
  using cwassistant::core::isCharacterDistributionImplausible;

  // Real field data (a genuine debug capture of noise misclassified as CW)
  // showed E+T fractions of 0.59, 0.45, and 0.76 for false-positive tracks,
  // versus 0.27 for the most plausible real candidate and 0.25 for the
  // benchmark's own legitimate decoded text ("SOSCQTEST123") — the default
  // 0.35 threshold sits in the gap between those two groups.
  expect(!isCharacterDistributionImplausible("SOSCQTEST123", 10, 0.35F),
         "legitimate varied decoded text is never flagged implausible");
  expect(isCharacterDistributionImplausible("TETETETETETETETETET", 10, 0.35F),
         "a run of only E/T characters is flagged implausible");
  expect(!isCharacterDistributionImplausible("TETETETETETETETETET", 40, 0.35F),
         "the check does not fire before minimum_characters worth of text "
         "has accumulated, since the fraction is not yet meaningful");
  expect(!isCharacterDistributionImplausible("", 0, 0.35F),
         "empty decoded text is never flagged (no letters to judge)");
  expect(isCharacterDistributionImplausible(
             "TE TE TE TE TE TE TE TE TE TE", 10, 0.35F),
         "inter-character spaces are ignored when computing the fraction, "
         "so a spaced-out run of only E/T is still flagged");
  expect(!isCharacterDistributionImplausible("SOS DE W1AW K", 10, 0.60F),
         "raising the threshold config value relaxes the gate accordingly");

  using cwassistant::core::CwChannelBank;
  constexpr double sample_rate = 8'000.0;
  CwChannelBank recovery_bank({
      .minimum_verification_symbols = 20,
      .minimum_plausibility_check_characters = 6,
      .decoder_recovery_seconds = 0.5,
  });
  std::vector<float> bins(1'001, -110.0F);
  double phase = 0.0;
  std::uint64_t now = 0;
  const auto feed = [&](const bool keyed, const int milliseconds) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 10) {
      bins.assign(bins.size(), -110.0F);
      if (keyed) bins[500] = -65.0F;
      static_cast<void>(recovery_bank.updateSpectrum(
          now, 0.0, 1'000.0, bins));
      cwassistant::core::RealtimeSampleBlock block;
      block.stream.sample_rate_hz = sample_rate;
      block.timestamp_ns = now;
      block.sample_count = 80;
      for (std::size_t index = 0; index < block.sample_count; ++index) {
        block.samples[index] = {
            keyed ? 0.30F * static_cast<float>(std::sin(phase)) : 0.0F,
            0.0F};
        phase += 2.0 * std::numbers::pi * 500.0 / sample_rate;
      }
      static_cast<void>(recovery_bank.processSamples(block));
      now += 10'000'000;
    }
  };
  for (int repetition = 0; repetition < 12; ++repetition) {
    feed(true, 60);   // E
    feed(false, 180);
    feed(true, 180);  // T
    feed(false, 180);
  }
  expect(recovery_bank.verificationDiagnostics().decoder_reacquisitions > 0 &&
             recovery_bank.allTrackDiagnostics().size() == 1,
         "a cadence-confirmed unverified decoder trapped in implausible "
         "single-element output reacquires while retaining its carrier");
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
  expect(CallsignPolicy::latest_in_text("DE EA8/W1AW ") ==
             std::optional<std::string>("EA8/W1AW"),
         "latest decoded callsign extraction supports operating prefixes");
  expect(!CallsignPolicy::latest_in_text("CQ TEST 599 ?"),
         "reports and operating words are not mistaken for callsigns");
  expect(!CallsignPolicy::latest_complete_in_text("CQ IU0LF"),
         "an unfinished decoded callsign is not presented as confirmed");
  expect(CallsignPolicy::latest_complete_in_text("CQ IU0LFQ ") ==
             std::optional<std::string>("IU0LFQ"),
         "a stable word gap confirms a structurally valid decoded callsign");
  expect(CallsignPolicy::latest_complete_in_text(
             "GW0KRL KN28N6 RANDOM7 ") ==
             std::optional<std::string>("GW0KRL"),
         "callsign extraction ignores noise tokens with trailing or embedded "
         "digits after the district numeral");
  expect(CallsignPolicy::best_complete_in_text("CQ IU0LFQ ") ==
             std::optional<std::string>("IU0LFQ"),
         "CQ context supplies enough evidence for an automatic call label");
  expect(CallsignPolicy::best_complete_in_text(
             "CQ TEST IU0LFQ IU0LFQ 599 ") ==
             std::optional<std::string>("IU0LFQ"),
         "an exactly repeated decoded callsign supplies label evidence");
  expect(!CallsignPolicy::best_complete_in_text("QA1RRK 599 "),
         "a lone random call-shaped token is not promoted to a stream label");
  expect(CallsignPolicy::best_complete_in_text("CQ TEST IU0LFQ ") ==
             std::optional<std::string>("IU0LFQ"),
         "a runner callsign after a CQ qualifier is identified");
  expect(CallsignPolicy::best_complete_in_text("TU IK3EYN CQ ") ==
             std::optional<std::string>("IK3EYN"),
         "a runner callsign in the acknowledgement pattern is identified");
  expect(CallsignPolicy::best_complete_in_text("IK3EYN UP ") ==
             std::optional<std::string>("IK3EYN"),
         "a split runner callsign before UP is identified");
  expect(CallsignPolicy::best_complete_in_text("EM90ZMV PSE K ") ==
             std::optional<std::string>("EM90ZMV"),
         "an ordinary-QSO callsign before PSE K is identified");
  expect(CallsignPolicy::best_complete_in_text("K3YL K3YL AR ") ==
             std::optional<std::string>("K3YL"),
         "a repeated ordinary-QSO callsign before AR is identified");
  expect(CallsignPolicy::best_complete_in_text("W1AW W1AW ") ==
             std::optional<std::string>("W1AW"),
         "an exactly repeated standalone caller callsign is identified");
  expect(CallsignPolicy::best_complete_in_text("IZ3ERM IZ3ERM ") ==
             std::optional<std::string>("IZ3ERM"),
         "a repeated acoustic-consensus callsign is eligible for a stream "
         "label without decoded conversation context");
  expect(CallsignPolicy::best_complete_in_text("CQ SN100PKP ") ==
             std::optional<std::string>("SN100PKP"),
         "a special-event callsign with one multi-digit block is identified");
  expect(CallsignPolicy::best_complete_in_text("DE 3DA0RU ") ==
             std::optional<std::string>("3DA0RU"),
         "a valid numeric-leading international prefix is retained");
  expect(!CallsignPolicy::best_complete_in_text("EA7G2NX 599 P7FN "),
         "report context and random callsign-shaped fragments are not labels");
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

void test_wav_writer() {
  using namespace std::chrono_literals;
  using namespace cwassistant::core;
  const auto path =
      std::filesystem::temp_directory_path() / "cwa_wav_writer_test.wav";
  {
    WavWriter writer;
    expect(writer.open(path.string(), 8'000.0),
           "wav writer opens a capture file");
    expect(writer.isOpen(), "wav writer reports open after open()");
    RealtimeSampleBlock block;
    block.stream.sample_rate_hz = 8'000.0;
    block.sample_count = 4;
    block.samples[0] = {0.5F, 0.0F};
    block.samples[1] = {-0.5F, 0.0F};
    block.samples[2] = {1.0F, 0.0F};
    block.samples[3] = {-1.0F, 0.0F};
    expect(writer.writeBlock(block) && writer.framesWritten() == 4,
           "wav writer accepts a sample block and counts written frames");
    writer.close();
    expect(!writer.isOpen(), "wav writer reports closed after close()");
  }

  WavReplaySource reader;
  expect(reader.open(path.string(), {}) &&
             reader.stream_descriptor().sample_rate_hz == 8'000.0 &&
             reader.total_frames() == 4,
         "a captured file round-trips through the WAV reader with exact "
         "sample rate and frame count");
  expect(reader.start(), "round-tripped capture starts playback");
  RealtimeSampleBlock read_block;
  expect(reader.read(read_block, 0ms) && read_block.sample_count == 4,
         "round-tripped capture reports the exact frame count written");
  if (read_block.sample_count == 4) {
    expect(std::abs(read_block.samples[0].real() - 0.5F) < 0.001F &&
               std::abs(read_block.samples[1].real() + 0.5F) < 0.001F &&
               std::abs(read_block.samples[2].real() - 1.0F) < 0.001F &&
               std::abs(read_block.samples[3].real() + 1.0F) < 0.001F,
           "round-tripped capture preserves sample values within PCM16 "
           "quantization precision");
  }
  reader.stop();
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
  expect(snapshots.size() == 1 && snapshots[0].bins_dbfs.size() == 513 &&
             snapshots[0].instantaneous_bins_dbfs.size() == 513,
         "audio FFT emits averaged and instantaneous one-sided bins including Nyquist");
  const auto peak = static_cast<std::size_t>(std::distance(
      snapshots[0].bins_dbfs.begin(),
      std::max_element(snapshots[0].bins_dbfs.begin(),
                       snapshots[0].bins_dbfs.end())));
  expect(peak == tone_bin, "windowed FFT locates a bin-centered CW tone");
  expect(std::abs(snapshots[0].bins_dbfs[peak]) < 0.05F,
         "window coherent-gain normalization reports a full-scale tone near 0 dBFS");
  expect(std::abs(snapshots[0].instantaneous_bins_dbfs[peak] -
                  snapshots[0].bins_dbfs[peak]) < 1.0e-6F,
         "one-frame averaging preserves the instantaneous CW-symbol raster bins");
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
  cwassistant::core::QsoRecord qso{};
  qso.callsign = "I1ABC";
  qso.qso_date = "20260830";
  qso.time_on = "143512";
  qso.band = "20M";
  qso.mode = "CW";
  qso.frequency_mhz = "14.025000";
  qso.rst_sent = "599";
  qso.rst_received = "579";
  qso.station_callsign = "IU0XYZ";
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

  QsoRecord qso{};
  qso.callsign = "I1ABC";
  qso.qso_date = "20260830";
  qso.time_on = "143512";
  qso.mode = "CW";
  qso.rst_sent = "599";
  qso.rst_received = "579";
  qso.station_callsign = "IU0XYZ";
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
  expect(resolve_dial_frequency(145'900'000, 116'000'000) ==
             std::optional<std::uint64_t>(29'900'000),
         "actual RF is converted back to a positive-offset radio dial value");
  expect(resolve_dial_frequency(14'000'000, -116'000'000) ==
             std::optional<std::uint64_t>(130'000'000),
         "actual RF is converted back through a negative transverter offset");
  expect(!resolve_dial_frequency(116'000'000, 116'000'000),
         "inverse offset rejects a zero dial frequency");
  expect(!resolve_dial_frequency(
             std::numeric_limits<std::uint64_t>::max(), -1),
         "inverse offset rejects unsigned overflow");
  expect(resolve_dial_frequency(
             1, std::numeric_limits<std::int64_t>::min()) ==
             std::optional<std::uint64_t>(9'223'372'036'854'775'809ULL) &&
             resolve_dial_frequency(
                 9'223'372'036'854'775'808ULL,
                 std::numeric_limits<std::int64_t>::max()) ==
                 std::optional<std::uint64_t>(1),
         "inverse offset handles both signed limits without overflow");
  expect(parse_frequency_value("14040.49", 1'000) ==
             std::optional<std::uint64_t>(14'040'490) &&
             parse_frequency_value(" 7010,5 ", 1'000) ==
                 std::optional<std::uint64_t>(7'010'500) &&
             parse_frequency_value("1", 1'000) ==
                 std::optional<std::uint64_t>(1'000),
         "operator kHz entry accepts exact dot/comma fractional values");
  expect(parse_frequency_value("144.300025", 1'000'000) ==
             std::optional<std::uint64_t>(144'300'025),
         "operator MHz entry preserves exact integer-hertz precision");
  expect(!parse_frequency_value("14,040.5", 1'000) &&
             !parse_frequency_value("14040.1234", 1'000) &&
             !parse_frequency_value("0", 1'000) &&
             !parse_frequency_value("14 MHz", 1'000) &&
             !parse_frequency_value("14.1", 60),
         "operator kHz entry rejects grouping, excessive precision, zero, and units");

  using Target = OmniRigRxFrequencyTarget;
  expect(select_omnirig_rx_frequency_target(true, true, 0x04, 0x80) ==
                 Target::FrequencyA &&
             select_omnirig_rx_frequency_target(true, true, 0x04, 0x100) ==
                 Target::FrequencyA &&
             select_omnirig_rx_frequency_target(true, true, 0x04, 0x800) ==
                 Target::FrequencyA,
         "OmniRig A/AA/AB receive states select a writable FreqA property");
  expect(select_omnirig_rx_frequency_target(true, true, 0x08, 0x200) ==
                 Target::FrequencyB &&
             select_omnirig_rx_frequency_target(true, true, 0x08, 0x400) ==
                 Target::FrequencyB &&
             select_omnirig_rx_frequency_target(true, true, 0x08, 0x1000) ==
                 Target::FrequencyB,
         "OmniRig B/BA/BB receive states select a writable FreqB property");
  expect(select_omnirig_rx_frequency_target(true, true, 0x02, 0x80) ==
                 Target::Frequency &&
             select_omnirig_rx_frequency_target(false, true, 0x0e, 0x80) ==
                 Target::None &&
             select_omnirig_rx_frequency_target(true, false, 0x0e, 0x80) ==
                 Target::None &&
             select_omnirig_rx_frequency_target(true, true, 0, 0x80) ==
                 Target::None,
         "OmniRig uses generic Freq only when writable and blocks offline, TX, and read-only states");

  const auto first_step = step_rx_frequency(14'040'000, std::nullopt,
                                            1'000, 1);
  const auto second_step = step_rx_frequency(14'040'000, first_step,
                                             1'000, 1);
  expect(first_step == std::optional<std::uint64_t>(14'041'000) &&
             second_step == std::optional<std::uint64_t>(14'042'000),
         "accepted RX steps accumulate while provider readback is pending");
  expect(!step_rx_frequency(500, std::nullopt, 1'000, -1) &&
             !step_rx_frequency(14'040'000, std::nullopt, 1'000, 0),
         "RX stepping rejects underflow and invalid direction");
}

void test_band_selected_station_equipment_adif() {
  using namespace cwassistant::core;
  const std::vector<StationEquipmentRule> rules{
      {
          .bands = {"6M", "10M", "12M", "15M", "17M", "20M"},
          .equipment = {
              .radio = "Yaesu FT-450D",
              .transverter = {},
              .antenna = "Dipole",
          },
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
  test_established_cw_track_reserves_its_carrier_ridge();
  test_cw_channel_bank_state_reason_consistency();
  test_operator_selected_cw_probe();
  test_cw_channel_presentation_frequency_model();
  test_cw_channel_bank_implausible_character_distribution();
  test_callsign_policy();
  test_spectrum_settings();
  test_wav_replay_source();
  test_wav_writer();
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
