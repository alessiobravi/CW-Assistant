#include "cwassistant/core/cw_channel_bank.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

constexpr std::array<double, 3> kNarrowbandWidthsHz{60.0, 120.0, 240.0};

}  // namespace

CwChannelBank::Track::Track(const std::uint64_t track_id,
                            const double frequency,
                            const std::uint64_t timestamp_ns)
    : id(track_id),
      frequency_hz(frequency),
      last_detected_ns(timestamp_ns),
      last_frequency_update_ns(timestamp_ns) {}

CwChannelBank::CwChannelBank(CwChannelBankConfig config) : config_(config) {
  config_.acquisition_snr_db =
      std::clamp(config_.acquisition_snr_db, 3.0F, 30.0F);
  config_.retention_snr_db = std::clamp(
      config_.retention_snr_db, 0.0F, config_.acquisition_snr_db);
  config_.detection_dynamic_range_db =
      std::clamp(config_.detection_dynamic_range_db, 40.0F, 140.0F);
  config_.minimum_separation_hz =
      std::clamp(config_.minimum_separation_hz, 5.0, 500.0);
  config_.tracking_tolerance_hz = std::clamp(
      config_.tracking_tolerance_hz, config_.minimum_separation_hz, 1'000.0);
  config_.empty_track_retention_seconds =
      std::clamp(config_.empty_track_retention_seconds, 0.5, 30.0);
  config_.decoded_track_retention_seconds = std::clamp(
      config_.decoded_track_retention_seconds,
      config_.empty_track_retention_seconds, 120.0);
  config_.narrowband_width_hz =
      std::clamp(config_.narrowband_width_hz, 40.0, 500.0);
  config_.noise_reference_offset_hz = std::clamp(
      config_.noise_reference_offset_hz, kNarrowbandWidthsHz.back(), 2'000.0);
  config_.evidence_rate_hz =
      std::clamp(config_.evidence_rate_hz, 100.0, 2'000.0);
  config_.maximum_tracks =
      std::clamp<std::size_t>(config_.maximum_tracks, 1, 64);
}

void CwChannelBank::reset() noexcept {
  tracks_.clear();
  snapshots_.clear();
  next_track_id_ = 1;
  expected_sample_timestamp_ns_ = 0;
  stream_initialized_ = false;
  sample_timing_initialized_ = false;
}

const std::vector<CwChannelSnapshot>& CwChannelBank::updateSpectrum(
    const std::uint64_t timestamp_ns, const double lower_frequency_hz,
    const double upper_frequency_hz, const std::span<const float> bins_dbfs) {
  if (bins_dbfs.size() < 3 ||
      !std::isfinite(lower_frequency_hz) ||
      !std::isfinite(upper_frequency_hz) ||
      upper_frequency_hz <= lower_frequency_hz) {
    return snapshots_;
  }

  const double bin_width_hz =
      (upper_frequency_hz - lower_frequency_hz) /
      static_cast<double>(bins_dbfs.size() - 1);
  const float measured_noise_dbfs = estimateNoise(bins_dbfs);
  const float strongest_dbfs = *std::max_element(
      bins_dbfs.begin(), bins_dbfs.end());
  const float noise_dbfs = std::max(
      measured_noise_dbfs,
      strongest_dbfs - config_.detection_dynamic_range_db);
  std::vector<Candidate> candidates;
  candidates.reserve(std::min<std::size_t>(bins_dbfs.size(), 64));
  for (std::size_t bin = 1; bin + 1 < bins_dbfs.size(); ++bin) {
    const float level = bins_dbfs[bin];
    if (!std::isfinite(level) || level < bins_dbfs[bin - 1] ||
        level < bins_dbfs[bin + 1]) {
      continue;
    }
    const float left = bins_dbfs[bin - 1];
    const float right = bins_dbfs[bin + 1];
    const float denominator = left - 2.0F * level + right;
    const double fractional_bin = std::abs(denominator) > 1.0e-6F
        ? std::clamp(0.5 * static_cast<double>(left - right) /
                         static_cast<double>(denominator),
                     -0.5, 0.5)
        : 0.0;
    const float interpolated_level = level - static_cast<float>(
        0.25 * static_cast<double>(left - right) * fractional_bin);
    const float snr_db = interpolated_level - noise_dbfs;
    if (snr_db < config_.acquisition_snr_db) continue;
    candidates.push_back({
        .frequency_hz = lower_frequency_hz +
                        (static_cast<double>(bin) + fractional_bin) *
                            bin_width_hz,
        .snr_db = snr_db,
    });
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.snr_db > right.snr_db;
            });

  std::vector<Candidate> separated;
  separated.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    const bool overlaps = std::any_of(
        separated.cbegin(), separated.cend(), [&](const Candidate& selected) {
          return std::abs(candidate.frequency_hz - selected.frequency_hz) <
                 config_.minimum_separation_hz;
        });
    if (!overlaps) separated.push_back(candidate);
  }

  for (auto& track : tracks_) track.matched = false;
  for (const auto& candidate : separated) {
    auto nearest = tracks_.end();
    double nearest_distance = config_.tracking_tolerance_hz;
    for (auto track = tracks_.begin(); track != tracks_.end(); ++track) {
      if (track->matched) continue;
      const double elapsed_seconds = timestamp_ns > track->last_frequency_update_ns
          ? static_cast<double>(timestamp_ns - track->last_frequency_update_ns) /
                1'000'000'000.0
          : 0.0;
      const double predicted_frequency = track->frequency_hz +
          track->drift_hz_per_second * elapsed_seconds;
      const double distance =
          std::abs(predicted_frequency - candidate.frequency_hz);
      if (distance <= nearest_distance) {
        nearest = track;
        nearest_distance = distance;
      }
    }
    if (nearest == tracks_.end()) {
      if (tracks_.size() >= config_.maximum_tracks) continue;
      tracks_.emplace_back(next_track_id_++, candidate.frequency_hz,
                           timestamp_ns);
      nearest = std::prev(tracks_.end());
    }
    nearest->matched = true;
    const double elapsed_seconds = timestamp_ns > nearest->last_frequency_update_ns
        ? static_cast<double>(timestamp_ns - nearest->last_frequency_update_ns) /
              1'000'000'000.0
        : 0.0;
    if (elapsed_seconds > 0.0 && elapsed_seconds <= 1.0) {
      const double predicted_frequency = nearest->frequency_hz +
          nearest->drift_hz_per_second * elapsed_seconds;
      const double innovation = candidate.frequency_hz - predicted_frequency;
      nearest->frequency_hz = predicted_frequency + 0.45 * innovation;
      nearest->drift_hz_per_second = std::clamp(
          nearest->drift_hz_per_second +
              0.08 * innovation / elapsed_seconds,
          -200.0, 200.0);
    } else {
      nearest->frequency_hz +=
          0.45 * (candidate.frequency_hz - nearest->frequency_hz);
      nearest->drift_hz_per_second = 0.0;
    }
    nearest->last_frequency_update_ns = timestamp_ns;
    nearest->last_detected_ns = timestamp_ns;
  }

  for (auto& track : tracks_) {
    track.spectral_snr_db = spectralSnr(
        track, lower_frequency_hz, bin_width_hz, bins_dbfs, noise_dbfs);
    if (track.spectral_snr_db >= config_.retention_snr_db)
      track.last_detected_ns = timestamp_ns;
  }

  const auto expired = [&](const Track& track) {
    if (timestamp_ns < track.last_detected_ns || track.update.key_down)
      return false;
    const double age_seconds =
        static_cast<double>(timestamp_ns - track.last_detected_ns) /
        1'000'000'000.0;
    const bool has_decode = !track.update.text.empty() ||
                            !track.update.provisional_text.empty();
    const double retention = has_decode
        ? config_.decoded_track_retention_seconds
        : config_.empty_track_retention_seconds;
    return age_seconds > retention;
  };
  std::erase_if(tracks_, expired);
  rebuildSnapshots();
  return snapshots_;
}

const std::vector<CwChannelSnapshot>& CwChannelBank::processSamples(
    const RealtimeSampleBlock& block) {
  if (block.sample_count == 0 || block.sample_count > block.samples.size() ||
      !std::isfinite(block.stream.sample_rate_hz) ||
      block.stream.sample_rate_hz <= 0.0) {
    return snapshots_;
  }

  const bool same_stream = stream_initialized_ &&
      stream_.kind == block.stream.kind &&
      stream_.sample_rate_hz == block.stream.sample_rate_hz &&
      stream_.center_frequency_hz == block.stream.center_frequency_hz;
  if (!same_stream) {
    stream_ = block.stream;
    stream_initialized_ = true;
    sample_timing_initialized_ = false;
    for (auto& track : tracks_) resetFilter(track);
  }

  if (sample_timing_initialized_) {
    const std::uint64_t difference =
        block.timestamp_ns > expected_sample_timestamp_ns_
            ? block.timestamp_ns - expected_sample_timestamp_ns_
            : expected_sample_timestamp_ns_ - block.timestamp_ns;
    const auto tolerance_ns = static_cast<std::uint64_t>(
        std::ceil(2.0 * 1'000'000'000.0 / block.stream.sample_rate_hz));
    if (difference > tolerance_ns) {
      for (auto& track : tracks_) {
        track.decoder.reset();
        track.update = {};
        resetFilter(track);
      }
    }
  }

  const double sample_rate_hz = block.stream.sample_rate_hz;
  const std::size_t evidence_samples = static_cast<std::size_t>(
      std::max(1.0, std::round(sample_rate_hz / config_.evidence_rate_hz)));
  std::array<float, kNarrowbandWidthsHz.size()> filter_alphas{};
  for (std::size_t width = 0; width < kNarrowbandWidthsHz.size(); ++width) {
    const double cutoff_hz =
        std::min(kNarrowbandWidthsHz[width] * 0.5, sample_rate_hz * 0.2);
    filter_alphas[width] = static_cast<float>(1.0 - std::exp(
        -2.0 * std::numbers::pi * cutoff_hz / sample_rate_hz));
  }
  const float reference_alpha = filter_alphas[1];

  for (auto& track : tracks_) {
    const double center_hz = block.stream.kind == StreamKind::Audio
        ? track.frequency_hz
        : track.frequency_hz - block.stream.center_frequency_hz;
    const double reference_offset = config_.noise_reference_offset_hz;
    const auto oscillator_step = [sample_rate_hz](const double frequency_hz) {
      const double angle = -2.0 * std::numbers::pi * frequency_hz /
                           sample_rate_hz;
      return std::complex<float>(static_cast<float>(std::cos(angle)),
                                 static_cast<float>(std::sin(angle)));
    };
    const auto center_step = oscillator_step(center_hz);
    const auto lower_step = oscillator_step(center_hz - reference_offset);
    const auto upper_step = oscillator_step(center_hz + reference_offset);
    if (!track.filter_initialized) {
      resetFilter(track);
      track.filter_initialized = true;
    }

    const auto filtered = [](
        const std::complex<float> input,
        const float alpha,
        std::array<std::complex<float>, 3>& stages) {
      stages[0] += alpha * (input - stages[0]);
      stages[1] += alpha * (stages[0] - stages[1]);
      stages[2] += alpha * (stages[1] - stages[2]);
      return stages[2];
    };
    for (std::size_t index = 0; index < block.sample_count; ++index) {
      const auto sample = block.stream.kind == StreamKind::Audio
          ? std::complex<float>(block.samples[index].real(), 0.0F)
          : block.samples[index];
      const auto center_mixed = sample * track.center_oscillator;
      for (std::size_t width = 0; width < kNarrowbandWidthsHz.size(); ++width) {
        const auto center = filtered(center_mixed, filter_alphas[width],
                                     track.center_filters[width]);
        track.center_power_sums[width] += std::norm(center);
      }
      const auto lower = filtered(sample * track.lower_oscillator,
                                  reference_alpha, track.lower_filter);
      const auto upper = filtered(sample * track.upper_oscillator,
                                  reference_alpha, track.upper_filter);
      track.lower_power_sum += std::norm(lower);
      track.upper_power_sum += std::norm(upper);
      ++track.accumulated_samples;

      track.center_oscillator *= center_step;
      track.lower_oscillator *= lower_step;
      track.upper_oscillator *= upper_step;
      if ((index & 1'023U) == 1'023U) {
        const auto normalize = [](std::complex<float>& oscillator) {
          const float magnitude = std::abs(oscillator);
          if (magnitude > 0.0F) oscillator /= magnitude;
        };
        normalize(track.center_oscillator);
        normalize(track.lower_oscillator);
        normalize(track.upper_oscillator);
      }

      if (track.accumulated_samples < evidence_samples) continue;
      const float scale = 1.0F /
          static_cast<float>(track.accumulated_samples);
      constexpr float kPowerFloor = 1.0e-12F;
      const float observed_lower = track.lower_power_sum * scale;
      const float observed_upper = track.upper_power_sum * scale;
      if (!track.noise_initialized) {
        track.lower_noise_power = observed_lower;
        track.upper_noise_power = observed_upper;
        track.noise_initialized = true;
      } else {
        const auto update_noise = [](float& estimate, const float observed) {
          const float smoothing = observed < estimate ? 0.20F : 0.025F;
          estimate += smoothing * (observed - estimate);
        };
        update_noise(track.lower_noise_power, observed_lower);
        update_noise(track.upper_noise_power, observed_upper);
      }
      const float reference_power = std::max(
          std::min(track.lower_noise_power, track.upper_noise_power),
          kPowerFloor);
      std::array<float, kNarrowbandWidthsHz.size()> width_snr{};
      for (std::size_t width = 0; width < kNarrowbandWidthsHz.size(); ++width) {
        const float center_power = track.center_power_sums[width] * scale;
        const float noise_scale = static_cast<float>(
            kNarrowbandWidthsHz[width] / kNarrowbandWidthsHz[1]);
        width_snr[width] = 10.0F * std::log10(
            std::max(center_power, kPowerFloor) /
            std::max(reference_power * noise_scale, kPowerFloor));
      }
      const float center_localization =
          track.center_power_sums[2] > kPowerFloor
              ? track.center_power_sums[0] / track.center_power_sums[2]
              : 1.0F;
      std::size_t preferred = 1;
      if (std::abs(track.drift_hz_per_second) >= 30.0 ||
          (track.update.wpm >= 40.0 &&
           width_snr[2] >= width_snr[1] - 1.0F)) {
        preferred = 2;
      } else if (track.update.wpm > 0.0 && track.update.wpm <= 16.0 &&
                 std::abs(track.drift_hz_per_second) < 8.0 &&
                 width_snr[0] >= width_snr[1] - 1.0F) {
        preferred = 0;
      } else if (width_snr[0] > width_snr[1] + 4.0F) {
        preferred = 0;
      } else if (width_snr[2] > width_snr[1] + 6.0F &&
                 center_localization >= 0.02F) {
        preferred = 2;
      }
      if (track.total_width_observations < 250) {
        ++track.total_width_observations;
      } else if (preferred == track.pending_width_index) {
        if (++track.pending_width_observations >= 20) {
          track.selected_width_index =
              static_cast<std::uint8_t>(preferred);
          track.pending_width_observations = 0;
        }
      } else {
        track.pending_width_index = static_cast<std::uint8_t>(preferred);
        track.pending_width_observations = 1;
      }
      track.snr_db = width_snr[track.selected_width_index];
      if (center_localization < 0.02F) track.snr_db = 0.0F;
      const auto timestamp_ns = block.timestamp_ns +
          static_cast<std::uint64_t>(
              static_cast<long double>(index) * 1'000'000'000.0L /
              sample_rate_hz);
      track.update = track.decoder.process(timestamp_ns, track.snr_db);
      track.center_power_sums = {};
      track.lower_power_sum = 0.0F;
      track.upper_power_sum = 0.0F;
      track.accumulated_samples = 0;
    }
  }

  expected_sample_timestamp_ns_ =
      block.timestamp_ns + static_cast<std::uint64_t>(
          static_cast<long double>(block.sample_count) * 1'000'000'000.0L /
          sample_rate_hz);
  sample_timing_initialized_ = true;
  rebuildSnapshots();
  return snapshots_;
}

const std::vector<CwChannelSnapshot>& CwChannelBank::channels() const noexcept {
  return snapshots_;
}

float CwChannelBank::estimateNoise(
    const std::span<const float> bins_dbfs) const {
  std::vector<float> finite;
  finite.reserve(bins_dbfs.size());
  for (const float value : bins_dbfs) {
    if (std::isfinite(value)) finite.push_back(value);
  }
  if (finite.empty()) return -200.0F;
  const std::size_t index = finite.size() / 2;
  std::nth_element(finite.begin(), finite.begin() +
                   static_cast<std::ptrdiff_t>(index), finite.end());
  return finite[index];
}

float CwChannelBank::spectralSnr(const Track& track,
                                 const double lower_frequency_hz,
                                 const double bin_width_hz,
                                 const std::span<const float> bins_dbfs,
                                 const float noise_dbfs) const {
  const double position =
      (track.frequency_hz - lower_frequency_hz) / bin_width_hz;
  const auto center = static_cast<std::ptrdiff_t>(std::llround(position));
  float peak = -200.0F;
  for (std::ptrdiff_t offset = -1; offset <= 1; ++offset) {
    const std::ptrdiff_t index = center + offset;
    if (index < 0 || index >= static_cast<std::ptrdiff_t>(bins_dbfs.size()))
      continue;
    peak = std::max(peak, bins_dbfs[static_cast<std::size_t>(index)]);
  }
  return std::isfinite(peak) ? peak - noise_dbfs : -100.0F;
}

void CwChannelBank::resetFilter(Track& track) noexcept {
  track.center_filters = {};
  track.lower_filter = {};
  track.upper_filter = {};
  track.center_oscillator = {1.0F, 0.0F};
  track.lower_oscillator = {1.0F, 0.0F};
  track.upper_oscillator = {1.0F, 0.0F};
  track.center_power_sums = {};
  track.lower_power_sum = 0.0F;
  track.upper_power_sum = 0.0F;
  track.accumulated_samples = 0;
  track.lower_noise_power = 0.0F;
  track.upper_noise_power = 0.0F;
  track.selected_width_index = 1;
  track.pending_width_index = 1;
  track.pending_width_observations = 0;
  track.total_width_observations = 0;
  track.noise_initialized = false;
  track.filter_initialized = false;
}

void CwChannelBank::rebuildSnapshots() {
  snapshots_.clear();
  snapshots_.reserve(tracks_.size());
  for (const auto& track : tracks_) {
    snapshots_.push_back({
        .id = track.id,
        .color_index = static_cast<std::uint8_t>((track.id - 1) % 24),
        .frequency_hz = track.frequency_hz,
        .drift_hz_per_second = track.drift_hz_per_second,
        .filter_width_hz =
            kNarrowbandWidthsHz[track.selected_width_index],
        .snr_db = track.snr_db,
        .wpm = track.update.wpm,
        .confidence = track.update.confidence,
        .key_down_probability = track.update.key_down_probability,
        .key_down = track.update.key_down,
        .active = track.update.key_down ||
                  track.spectral_snr_db >= config_.retention_snr_db,
        .text = track.update.text,
        .provisional_text = track.update.provisional_text,
        .pending_elements = track.update.pending_elements,
        .callsign = CallsignPolicy::latest_in_text(
                        track.update.text + " " +
                        track.update.provisional_text)
                        .value_or(std::string{}),
    });
  }
  std::sort(snapshots_.begin(), snapshots_.end(),
            [](const CwChannelSnapshot& left,
               const CwChannelSnapshot& right) {
              return left.frequency_hz < right.frequency_hz;
            });
}

}  // namespace cwassistant::core
