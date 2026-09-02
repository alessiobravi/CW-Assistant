#include "cwassistant/core/cw_channel_bank.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

constexpr std::array<double, 3> kNarrowbandWidthsHz{60.0, 120.0, 240.0};

}  // namespace

const char* cwTrackStateName(const CwTrackState state) noexcept {
  switch (state) {
    case CwTrackState::Candidate: return "candidate";
    case CwTrackState::MorseLikely: return "morse-likely";
    case CwTrackState::Verified: return "verified";
    case CwTrackState::Lost: return "lost";
  }
  return "candidate";
}

const char* cwVerificationReasonName(
    const CwVerificationReason reason) noexcept {
  switch (reason) {
    case CwVerificationReason::NeedsSpectralPersistence:
      return "needs-spectral-persistence";
    case CwVerificationReason::NeedsKeyingEdges: return "needs-keying-edges";
    case CwVerificationReason::NeedsCadenceEvidence:
      return "needs-cadence-evidence";
    case CwVerificationReason::LowNarrowbandCoherence:
      return "low-narrowband-coherence";
    case CwVerificationReason::LowCadenceQuality:
      return "low-cadence-quality";
    case CwVerificationReason::NeedsDecodedSymbols:
      return "needs-decoded-symbols";
    case CwVerificationReason::TooManyUnknownSymbols:
      return "too-many-unknown-symbols";
    case CwVerificationReason::LowTimingQuality:
      return "low-timing-quality";
    case CwVerificationReason::LowCharacterConfidence:
      return "low-character-confidence";
    case CwVerificationReason::NeedsSustainedEvidence:
      return "needs-sustained-evidence";
    case CwVerificationReason::ImplausibleCharacterDistribution:
      return "implausible-character-distribution";
    case CwVerificationReason::Verified: return "verified";
    case CwVerificationReason::SignalLost: return "signal-lost";
  }
  return "needs-spectral-persistence";
}

bool isCharacterDistributionImplausible(
    const std::string& text, const std::uint16_t minimum_characters,
    const float maximum_simple_character_fraction) noexcept {
  if (text.size() < minimum_characters) return false;
  std::size_t letters = 0;
  std::size_t simple = 0;
  for (const char character : text) {
    if (character == ' ') continue;
    ++letters;
    if (character == 'E' || character == 'T') ++simple;
  }
  return letters > 0 &&
         static_cast<float>(simple) / static_cast<float>(letters) >
             maximum_simple_character_fraction;
}

CwChannelBank::Track::Track(const std::uint64_t track_id,
                            const double frequency,
                            const std::uint64_t timestamp_ns)
    : id(track_id),
      frequency_hz(frequency),
      last_detected_ns(timestamp_ns),
      last_frequency_update_ns(timestamp_ns),
      last_candidate_match_ns(timestamp_ns) {}

CwChannelBank::CwChannelBank(CwChannelBankConfig config) : config_(config) {
  sanitizeConfig();
}

void CwChannelBank::configure(CwChannelBankConfig config) noexcept {
  config_ = config;
  sanitizeConfig();
}

void CwChannelBank::sanitizeConfig() noexcept {
  config_.acquisition_snr_db =
      std::clamp(config_.acquisition_snr_db, 3.0F, 30.0F);
  config_.retention_snr_db = std::clamp(
      config_.retention_snr_db, 0.0F, config_.acquisition_snr_db);
  config_.detection_dynamic_range_db =
      std::clamp(config_.detection_dynamic_range_db, 40.0F, 140.0F);
  config_.minimum_peak_prominence_db =
      std::clamp(config_.minimum_peak_prominence_db, 0.0F, 30.0F);
  config_.minimum_near_peak_prominence_db = std::clamp(
      config_.minimum_near_peak_prominence_db, 0.0F,
      config_.minimum_peak_prominence_db);
  config_.prominence_reference_offset_hz = std::clamp(
      config_.prominence_reference_offset_hz, 40.0, 1'000.0);
  config_.prominence_reference_width_hz = std::clamp(
      config_.prominence_reference_width_hz, 20.0,
      config_.prominence_reference_offset_hz);
  config_.minimum_separation_hz =
      std::clamp(config_.minimum_separation_hz, 5.0, 500.0);
  config_.tracking_tolerance_hz = std::clamp(
      config_.tracking_tolerance_hz, config_.minimum_separation_hz, 1'000.0);
  config_.empty_track_retention_seconds =
      std::clamp(config_.empty_track_retention_seconds, 0.5, 30.0);
  config_.decoded_track_retention_seconds = std::clamp(
      config_.decoded_track_retention_seconds,
      config_.empty_track_retention_seconds, 300.0);
  config_.unverified_track_retention_seconds = std::clamp(
      config_.unverified_track_retention_seconds, 0.2, 5.0);
  config_.color_identity_retention_seconds = std::clamp(
      config_.color_identity_retention_seconds, 300.0, 3'600.0);
  config_.color_identity_tolerance_hz = std::clamp(
      config_.color_identity_tolerance_hz, 5.0,
      config_.tracking_tolerance_hz);
  config_.narrowband_width_hz =
      std::clamp(config_.narrowband_width_hz, 40.0, 500.0);
  config_.noise_reference_offset_hz = std::clamp(
      config_.noise_reference_offset_hz, kNarrowbandWidthsHz.back(), 2'000.0);
  config_.evidence_rate_hz =
      std::clamp(config_.evidence_rate_hz, 100.0, 2'000.0);
  config_.maximum_tracks =
      std::clamp<std::size_t>(config_.maximum_tracks, 1, 64);
  config_.minimum_spectral_observations =
      std::clamp<std::uint16_t>(config_.minimum_spectral_observations, 1, 50);
  config_.minimum_verification_symbols =
      std::clamp<std::uint16_t>(config_.minimum_verification_symbols, 0, 20);
  config_.minimum_key_transitions =
      std::clamp<std::uint16_t>(config_.minimum_key_transitions, 2, 100);
  config_.minimum_cadence_observations =
      std::clamp<std::uint16_t>(config_.minimum_cadence_observations, 1, 50);
  config_.minimum_verification_timing_quality = std::clamp(
      config_.minimum_verification_timing_quality, 0.0F, 1.0F);
  config_.minimum_verification_cadence_quality = std::clamp(
      config_.minimum_verification_cadence_quality, 0.0F, 1.0F);
  config_.minimum_character_confidence = std::clamp(
      config_.minimum_character_confidence, 0.0F, 1.0F);
  config_.minimum_narrowband_coherence = std::clamp(
      config_.minimum_narrowband_coherence, 0.0F, 1.0F);
  config_.maximum_verification_unknown_fraction = std::clamp(
      config_.maximum_verification_unknown_fraction, 0.0F, 1.0F);
  config_.minimum_plausibility_check_characters = std::clamp<std::uint16_t>(
      config_.minimum_plausibility_check_characters, 10, 500);
  config_.maximum_simple_character_fraction = std::clamp(
      config_.maximum_simple_character_fraction, 0.0F, 1.0F);
  config_.track_identity_tolerance_hz = std::clamp(
      config_.track_identity_tolerance_hz, 5.0,
      config_.tracking_tolerance_hz);
  config_.track_replacement_margin_db = std::clamp(
      config_.track_replacement_margin_db, 0.0F, 20.0F);
  config_.verification_enter_seconds = std::clamp(
      config_.verification_enter_seconds, 0.0, 5.0);
  config_.verification_exit_seconds = std::clamp(
      config_.verification_exit_seconds,
      config_.verification_enter_seconds, 15.0);
  config_.decoder_recovery_seconds = std::clamp(
      config_.decoder_recovery_seconds, 0.5, 15.0);
}

void CwChannelBank::reset() noexcept {
  tracks_.clear();
  snapshots_.clear();
  retained_observations_.clear();
  color_leases_ = {};
  next_track_id_ = 1;
  expected_sample_timestamp_ns_ = 0;
  stream_initialized_ = false;
  sample_timing_initialized_ = false;
  verified_transitions_ = 0;
  expired_unverified_tracks_ = 0;
  decoder_reacquisitions_ = 0;
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
    float near_left_sum = 0.0F;
    float near_right_sum = 0.0F;
    std::size_t near_count = 0;
    for (std::size_t offset = 2; offset <= 4; ++offset) {
      if (bin < offset || bin + offset >= bins_dbfs.size()) continue;
      near_left_sum += bins_dbfs[bin - offset];
      near_right_sum += bins_dbfs[bin + offset];
      ++near_count;
    }
    if (near_count == 0) continue;
    const float near_reference = std::max(
        near_left_sum / static_cast<float>(near_count),
        near_right_sum / static_cast<float>(near_count));
    if (interpolated_level - near_reference <
        config_.minimum_near_peak_prominence_db) {
      continue;
    }
    const auto reference_offset_bins = static_cast<std::ptrdiff_t>(
        std::max(2.0, std::round(
            config_.prominence_reference_offset_hz / bin_width_hz)));
    const auto reference_half_width_bins = static_cast<std::ptrdiff_t>(
        std::max(1.0, std::round(
            0.5 * config_.prominence_reference_width_hz / bin_width_hz)));
    const auto reference_average = [&](const std::ptrdiff_t center) {
      float sum = 0.0F;
      std::size_t count = 0;
      for (std::ptrdiff_t offset = -reference_half_width_bins;
           offset <= reference_half_width_bins; ++offset) {
        const auto index = center + offset;
        if (index < 0 || index >=
            static_cast<std::ptrdiff_t>(bins_dbfs.size())) continue;
        const float value = bins_dbfs[static_cast<std::size_t>(index)];
        if (!std::isfinite(value)) continue;
        sum += value;
        ++count;
      }
      return std::pair{sum, count};
    };
    const auto [left_sum, left_count] = reference_average(
        static_cast<std::ptrdiff_t>(bin) - reference_offset_bins);
    const auto [right_sum, right_count] = reference_average(
        static_cast<std::ptrdiff_t>(bin) + reference_offset_bins);
    if (left_count == 0 && right_count == 0) continue;
    float local_reference = -std::numeric_limits<float>::infinity();
    if (left_count > 0) local_reference = std::max(
        local_reference, left_sum / static_cast<float>(left_count));
    if (right_count > 0) local_reference = std::max(
        local_reference, right_sum / static_cast<float>(right_count));
    if (interpolated_level - local_reference <
        config_.minimum_peak_prominence_db) {
      continue;
    }
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
      // Once a track has accumulated enough evidence, a large innovation is
      // a different signal, not ordinary drift. Refusing that association is
      // what prevents an old decoder/text history from walking across nearby
      // peaks and being attached to a new station.
      if (track->spectral_observations >=
              config_.minimum_spectral_observations &&
          distance > config_.track_identity_tolerance_hz) {
        continue;
      }
      if (distance <= nearest_distance) {
        nearest = track;
        nearest_distance = distance;
      }
    }
    if (nearest == tracks_.end()) {
      if (tracks_.size() >= config_.maximum_tracks) {
        auto victim = tracks_.end();
        float victim_score = std::numeric_limits<float>::infinity();
        for (auto track = tracks_.begin(); track != tracks_.end(); ++track) {
          if (track->matched ||
              track->verification_state == CwTrackState::Verified) {
            continue;
          }
          const float score = track->spectral_snr_db +
              0.10F * static_cast<float>(std::min<std::uint16_t>(
                  track->spectral_observations, 20)) +
              (track->verification_state == CwTrackState::MorseLikely
                   ? 8.0F : 0.0F);
          if (score < victim_score) {
            victim_score = score;
            victim = track;
          }
        }
        if (victim == tracks_.end()) continue;
        const double unmatched_seconds = timestamp_ns >
                victim->last_candidate_match_ns
            ? static_cast<double>(timestamp_ns -
                                  victim->last_candidate_match_ns) /
                  1'000'000'000.0
            : 0.0;
        const bool replace =
            candidate.snr_db >= victim->spectral_snr_db +
                                    config_.track_replacement_margin_db ||
            unmatched_seconds >= 0.25 ||
            victim->spectral_observations <
                config_.minimum_spectral_observations;
        if (!replace) continue;
        ++expired_unverified_tracks_;
        tracks_.erase(victim);
      }
      tracks_.emplace_back(next_track_id_++, candidate.frequency_hz,
                           timestamp_ns);
      nearest = std::prev(tracks_.end());
    }
    nearest->matched = true;
    if (nearest->spectral_observations <
        std::numeric_limits<std::uint16_t>::max()) {
      ++nearest->spectral_observations;
    }
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
    nearest->last_candidate_match_ns = timestamp_ns;
    nearest->consecutive_spectrum_misses = 0;
    if (nearest->color_assigned && nearest->ever_verified) {
      assignOrRefreshColor(*nearest, timestamp_ns);
    }
  }

  for (auto& track : tracks_) {
    track.spectral_snr_db = spectralSnr(
        track, lower_frequency_hz, bin_width_hz, bins_dbfs, noise_dbfs);
    if (!track.matched) {
      if (track.consecutive_spectrum_misses <
          std::numeric_limits<std::uint16_t>::max()) {
        ++track.consecutive_spectrum_misses;
      }
      // Persistence is recent evidence, not a lifetime counter. A slow decay
      // tolerates keyed gaps while ensuring abandoned candidates eventually
      // lose their admission advantage.
      if (track.consecutive_spectrum_misses % 30U == 0U &&
          track.spectral_observations > 0) {
        --track.spectral_observations;
      }
    }
    if (track.verification_state == CwTrackState::Verified &&
        track.spectral_snr_db >= config_.retention_snr_db) {
      track.last_detected_ns = timestamp_ns;
    }
  }

  const auto expired = [&](const Track& track) {
    if (timestamp_ns < track.last_detected_ns) return false;
    const double age_seconds =
        static_cast<double>(timestamp_ns - track.last_detected_ns) /
        1'000'000'000.0;
    const double retention = track.verification_state != CwTrackState::Verified
        ? (!track.update.text.empty() ||
                   !track.update.provisional_text.empty() ||
                   track.verification_state == CwTrackState::MorseLikely
               ? std::max(config_.unverified_track_retention_seconds,
                          config_.empty_track_retention_seconds)
               : config_.unverified_track_retention_seconds)
        : config_.decoded_track_retention_seconds;
    return age_seconds > retention;
  };
  for (auto& track : tracks_) {
    if (!expired(track)) continue;
    if (track.verification_state != CwTrackState::Verified)
      ++expired_unverified_tracks_;
    track.verification_state = CwTrackState::Lost;
    track.verification_reason = CwVerificationReason::SignalLost;
  }
  std::erase_if(tracks_, [](const Track& track) {
    return track.verification_state == CwTrackState::Lost;
  });
  rebuildSnapshots(timestamp_ns);
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
        track.decoder_rejection_samples = 0;
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
      // A single quiet sideband must not make every center-bin fluctuation
      // look like a keyed carrier. The geometric mean remains tolerant of a
      // nearby interferer on one side without inheriting the old min-side
      // floor underestimate.
      const float reference_power = std::max(
          std::sqrt(std::max(track.lower_noise_power, kPowerFloor) *
                    std::max(track.upper_noise_power, kPowerFloor)),
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
      const float center_localization_ratio =
          track.center_power_sums[2] > kPowerFloor
              ? track.center_power_sums[0] / track.center_power_sums[2]
              : 1.0F;
      // For ideal wideband noise, 60 Hz contains roughly one quarter of the
      // energy in 240 Hz (-6 dB); a centered tone approaches equal energy in
      // both filters (0 dB). Normalize and bound that physical range so the
      // verification threshold has stable meaning and cannot explode when
      // the wide filter happens to be near its numerical floor.
      const float localization_db = 10.0F * std::log10(std::max(
          center_localization_ratio, kPowerFloor));
      const float center_localization = std::clamp(
          (localization_db + 6.0F) / 6.0F, 0.0F, 1.0F);
      track.narrowband_coherence = track.total_width_observations == 0
          ? center_localization
          : 0.92F * track.narrowband_coherence +
                0.08F * center_localization;
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
      if (center_localization_ratio < 0.02F) track.snr_db = 0.0F;
      if (!track.keying_envelope_initialized) {
        track.keying_floor_db = track.snr_db - 6.0F;
        track.keying_peak_db = track.snr_db;
        track.keying_envelope_initialized = true;
      } else {
        const float floor_smoothing = track.snr_db < track.keying_floor_db
            ? 0.20F : (track.update.key_down ? 0.0005F : 0.01F);
        track.keying_floor_db += floor_smoothing *
            (track.snr_db - track.keying_floor_db);
        const float peak_smoothing = track.snr_db > track.keying_peak_db
            ? 0.15F : 0.002F;
        track.keying_peak_db += peak_smoothing *
            (track.snr_db - track.keying_peak_db);
      }
      track.keying_peak_db = std::max(
          track.keying_peak_db, track.keying_floor_db + 3.0F);
      const float keying_span = std::max(
          track.keying_peak_db - track.keying_floor_db, 6.0F);
      track.keying_snr_db = 10.0F * std::clamp(
          (track.snr_db - track.keying_floor_db) / keying_span,
          0.0F, 1.0F);
      const auto timestamp_ns = block.timestamp_ns +
          static_cast<std::uint64_t>(
              static_cast<long double>(index) * 1'000'000'000.0L /
              sample_rate_hz);
      track.update = track.decoder.process(timestamp_ns,
                                            track.keying_snr_db);
      updateVerification(track, timestamp_ns);
      recoverRejectedDecoder(track);
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
  rebuildSnapshots(expected_sample_timestamp_ns_);
  return snapshots_;
}

const std::vector<CwChannelSnapshot>& CwChannelBank::channels() const noexcept {
  return snapshots_;
}

CwVerificationDiagnostics CwChannelBank::verificationDiagnostics() const {
  CwVerificationDiagnostics result{
      .verified_transitions = verified_transitions_,
      .expired_unverified_tracks = expired_unverified_tracks_,
      .decoder_reacquisitions = decoder_reacquisitions_,
  };
  for (const auto& track : tracks_) {
    switch (track.verification_state) {
      case CwTrackState::Candidate: ++result.candidate_tracks; break;
      case CwTrackState::MorseLikely: ++result.morse_likely_tracks; break;
      case CwTrackState::Verified: ++result.verified_tracks; break;
      case CwTrackState::Lost: break;
    }
    const auto reason = static_cast<std::size_t>(track.verification_reason);
    if (reason < result.current_reason_counts.size())
      ++result.current_reason_counts[reason];
    result.maximum_decoded_symbols = std::max(
        result.maximum_decoded_symbols, track.update.decoded_symbols);
    result.maximum_key_transitions = std::max(
        result.maximum_key_transitions, track.update.key_transitions);
    result.best_timing_quality = std::max(
        result.best_timing_quality, track.update.timing_quality);
    result.best_cadence_quality = std::max(
        result.best_cadence_quality, track.update.cadence_quality);
    result.best_narrowband_coherence = std::max(
        result.best_narrowband_coherence, track.narrowband_coherence);
  }
  return result;
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
  track.keying_snr_db = 0.0F;
  track.keying_floor_db = 0.0F;
  track.keying_peak_db = 0.0F;
  track.keying_envelope_initialized = false;
  track.filter_initialized = false;
}

void CwChannelBank::shiftTrackedFrequencies(
    const double audio_hz_delta) noexcept {
  if (audio_hz_delta == 0.0 || !std::isfinite(audio_hz_delta)) return;
  for (Track& track : tracks_) {
    track.frequency_hz += audio_hz_delta;
    resetFilter(track);
  }
  for (ColorLease& lease : color_leases_) {
    if (!lease.occupied) continue;
    lease.frequency_hz += audio_hz_delta;
    if (lease.frequency_hz <= 0.0) lease = {};
  }
  for (RetainedObservation& observation : retained_observations_)
    observation.snapshot.frequency_hz += audio_hz_delta;
  std::erase_if(
      retained_observations_, [](const RetainedObservation& observation) {
        return observation.snapshot.frequency_hz <= 0.0;
      });
  // A shift is meant to follow a signal that stays within the processed
  // audio band as the VFO moves; repeated or large shifts (an operator
  // tuning across the band, not centering on one station) can carry a
  // track past 0 Hz, where it no longer corresponds to anything real and
  // must not linger as a nonsensical negative-frequency candidate. A track
  // shifted too far *positive* still needs no special handling here: it
  // simply stops matching spectral peaks and expires through the existing
  // retention timeout, exactly as an ordinary lost signal would.
  std::erase_if(tracks_,
                [](const Track& track) { return track.frequency_hz <= 0.0; });
  rebuildSnapshots(expected_sample_timestamp_ns_);
}

void CwChannelBank::updateVerification(Track& track,
                                       const std::uint64_t timestamp_ns) {
  const bool was_verified =
      track.verification_state == CwTrackState::Verified;
  const std::size_t plausibility_window = std::max<std::size_t>(
      64, static_cast<std::size_t>(
              config_.minimum_plausibility_check_characters) * 2U);
  const std::string recent_text = track.update.text.size() > plausibility_window
      ? track.update.text.substr(track.update.text.size() -
                                 plausibility_window)
      : track.update.text;
  if (isCharacterDistributionImplausible(
          recent_text, config_.minimum_plausibility_check_characters,
          config_.maximum_simple_character_fraction)) {
    track.verification_state = CwTrackState::Candidate;
    track.verification_reason =
        CwVerificationReason::ImplausibleCharacterDistribution;
    track.verification_pass_samples = 0;
    track.verification_fail_samples = 0;
    return;
  }

  // Absence is not contradictory evidence. Keep the verified observation and
  // its marker inactive until the configured decoded-track timeout expires;
  // only an actively matched carrier can accumulate evidence that demotes it.
  // Previously, ordinary key-up/silence decayed the evidence counters and
  // demoted a verified track after about two seconds, bypassing the advertised
  // 30-second retention and causing every later pass to receive a new ID/color.
  if (was_verified && !track.matched &&
      track.spectral_snr_db < config_.retention_snr_db) {
    track.verification_fail_samples = 0;
    track.verification_reason = CwVerificationReason::SignalLost;
    return;
  }

  const auto observations = track.spectral_observations;
  const auto symbols = track.update.recent_decoded_symbols > 0
      ? track.update.recent_decoded_symbols : track.update.decoded_symbols;
  const auto unknown = std::min(
      track.update.recent_decoded_symbols > 0
          ? track.update.recent_unknown_symbols
          : track.update.unknown_symbols,
      symbols);
  const auto cadence_observations =
      track.update.recent_cadence_observations > 0
          ? track.update.recent_cadence_observations
          : track.update.cadence_observations;
  const auto known = symbols - unknown;
  const float unknown_fraction = symbols == 0
      ? 1.0F
      : static_cast<float>(unknown) / static_cast<float>(symbols);
  const float persistence = std::min(
      1.0F, static_cast<float>(observations) /
                static_cast<float>(config_.minimum_spectral_observations));
  const float edge_evidence = std::min(
      1.0F, static_cast<float>(track.update.key_transitions) /
                static_cast<float>(config_.minimum_key_transitions));
  const float symbol_evidence = config_.minimum_verification_symbols == 0
      ? 1.0F
      : std::min(1.0F, static_cast<float>(known) /
                           static_cast<float>(
                               config_.minimum_verification_symbols));
  track.verification_confidence = std::clamp(
      0.12F * persistence + 0.12F * edge_evidence +
      0.16F * track.narrowband_coherence +
      0.18F * track.update.cadence_quality +
      0.18F * track.update.timing_quality +
      0.16F * track.update.mean_character_confidence +
      0.08F * symbol_evidence, 0.0F, 1.0F);

  CwTrackState eligible_state = CwTrackState::Candidate;
  CwVerificationReason failure_reason =
      CwVerificationReason::NeedsSpectralPersistence;
  bool passes = false;
  if (observations < config_.minimum_spectral_observations) {
    failure_reason = CwVerificationReason::NeedsSpectralPersistence;
  } else if (config_.minimum_verification_symbols == 0) {
    passes = true;
  } else if (track.update.key_transitions <
             config_.minimum_key_transitions) {
    failure_reason = CwVerificationReason::NeedsKeyingEdges;
  } else if (cadence_observations <
             config_.minimum_cadence_observations) {
    failure_reason = CwVerificationReason::NeedsCadenceEvidence;
  } else if (track.narrowband_coherence <
             config_.minimum_narrowband_coherence) {
    failure_reason = CwVerificationReason::LowNarrowbandCoherence;
  } else if (track.update.cadence_quality <
             config_.minimum_verification_cadence_quality) {
    failure_reason = CwVerificationReason::LowCadenceQuality;
  } else {
    eligible_state = CwTrackState::MorseLikely;
    if (known < config_.minimum_verification_symbols) {
      failure_reason = CwVerificationReason::NeedsDecodedSymbols;
    } else if (unknown_fraction >
               config_.maximum_verification_unknown_fraction) {
      failure_reason = CwVerificationReason::TooManyUnknownSymbols;
    } else if (track.update.timing_quality <
               config_.minimum_verification_timing_quality) {
      failure_reason = CwVerificationReason::LowTimingQuality;
    } else if (track.update.mean_character_confidence <
               config_.minimum_character_confidence) {
      failure_reason = CwVerificationReason::LowCharacterConfidence;
    } else {
      passes = true;
    }
  }

  const auto enter_samples = static_cast<std::uint16_t>(std::clamp(
      std::lround(config_.verification_enter_seconds *
                  config_.evidence_rate_hz),
      1L, static_cast<long>(
              std::numeric_limits<std::uint16_t>::max())));
  const auto exit_samples = static_cast<std::uint16_t>(std::clamp(
      std::lround(config_.verification_exit_seconds *
                  config_.evidence_rate_hz),
      1L, static_cast<long>(
              std::numeric_limits<std::uint16_t>::max())));

  if (!passes) {
    track.verification_pass_samples = 0;
    if (was_verified) {
      if (track.verification_fail_samples < exit_samples)
        ++track.verification_fail_samples;
      if (track.verification_fail_samples < exit_samples) {
        track.verification_reason = CwVerificationReason::Verified;
        return;
      }
    }
    track.verification_fail_samples = 0;
    track.verification_state = eligible_state;
    track.verification_reason = failure_reason;
    return;
  }

  track.verification_fail_samples = 0;
  if (config_.minimum_verification_symbols == 0) {
    track.verification_state = CwTrackState::Verified;
    track.verification_reason = CwVerificationReason::Verified;
    track.ever_verified = true;
    track.verification_confidence = 1.0F;
    assignOrRefreshColor(track, timestamp_ns);
    if (!was_verified) ++verified_transitions_;
    return;
  }
  if (!was_verified) {
    if (track.verification_pass_samples < enter_samples)
      ++track.verification_pass_samples;
    if (track.verification_pass_samples < enter_samples) {
      track.verification_state = CwTrackState::MorseLikely;
      track.verification_reason =
          CwVerificationReason::NeedsSustainedEvidence;
      return;
    }
  }

  track.verification_pass_samples = enter_samples;
  track.verification_state = CwTrackState::Verified;
  track.verification_reason = CwVerificationReason::Verified;
  track.ever_verified = true;
  assignOrRefreshColor(track, timestamp_ns);
  track.verification_cadence_quality = track.update.cadence_quality;
  track.verification_timing_quality = track.update.timing_quality;
  track.verification_character_confidence =
      track.update.mean_character_confidence;
  if (!was_verified) {
    ++verified_transitions_;
  }
}

bool CwChannelBank::colorLeaseIsCurrent(
    const ColorLease& lease, const std::uint64_t timestamp_ns) const noexcept {
  if (!lease.occupied) return false;
  if (timestamp_ns < lease.last_seen_ns) return true;
  const long double age_seconds =
      static_cast<long double>(timestamp_ns - lease.last_seen_ns) /
      1'000'000'000.0L;
  return age_seconds <= config_.color_identity_retention_seconds;
}

void CwChannelBank::assignOrRefreshColor(
    Track& track, const std::uint64_t timestamp_ns) noexcept {
  if (track.color_assigned) {
    ColorLease& lease = color_leases_[track.color_index % kColorLeaseCount];
    // Keep the lease anchored to the frequency that established the visual
    // identity. A verified track can otherwise walk across nearby noise while
    // silent and move the lease away from the carrier it is meant to remember.
    // Known operator retunes move every lease explicitly.
    lease.last_seen_ns = std::max(lease.last_seen_ns, timestamp_ns);
    lease.occupied = true;
    return;
  }

  std::array<bool, kColorLeaseCount> colors_in_use{};
  for (const Track& other : tracks_) {
    if (&other == &track || !other.color_assigned ||
        other.verification_state != CwTrackState::Verified)
      continue;
    colors_in_use[other.color_index % kColorLeaseCount] = true;
  }

  std::size_t selected = kColorLeaseCount;
  double nearest_distance = config_.color_identity_tolerance_hz;
  for (std::size_t index = 0; index < color_leases_.size(); ++index) {
    const ColorLease& lease = color_leases_[index];
    if (!colorLeaseIsCurrent(lease, timestamp_ns)) continue;
    const double distance = std::abs(lease.frequency_hz - track.frequency_hz);
    if (distance <= nearest_distance) {
      nearest_distance = distance;
      selected = index;
    }
  }

  if (selected == kColorLeaseCount) {
    std::uint64_t oldest_seen = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t index = 0; index < color_leases_.size(); ++index) {
      if (colors_in_use[index]) continue;
      const ColorLease& lease = color_leases_[index];
      if (!colorLeaseIsCurrent(lease, timestamp_ns)) {
        selected = index;
        break;
      }
      if (lease.last_seen_ns < oldest_seen) {
        oldest_seen = lease.last_seen_ns;
        selected = index;
      }
    }
  }

  // With at most 24 tracked carriers and 24 palette entries, a free color is
  // always expected. Keep a deterministic fallback for defensive robustness.
  if (selected == kColorLeaseCount) {
    selected = static_cast<std::size_t>((track.id - 1U) % kColorLeaseCount);
  }
  track.color_index = static_cast<std::uint8_t>(selected);
  track.color_assigned = true;
  ColorLease& lease = color_leases_[selected];
  if (!colorLeaseIsCurrent(lease, timestamp_ns)) {
    lease.frequency_hz = track.frequency_hz;
  }
  lease.last_seen_ns = timestamp_ns;
  lease.occupied = true;
}

void CwChannelBank::recoverRejectedDecoder(Track& track) {
  const bool quality_rejection =
      !track.ever_verified &&
      track.verification_state != CwTrackState::Verified &&
      track.verification_reason ==
          CwVerificationReason::ImplausibleCharacterDistribution &&
      track.update.recent_decoded_symbols >= 8U &&
      track.update.acoustic_cadence_confidence >= 0.55F;
  if (!quality_rejection) {
    track.decoder_rejection_samples = 0;
    return;
  }

  const auto recovery_samples = static_cast<std::uint16_t>(std::clamp(
      std::lround(config_.decoder_recovery_seconds *
                  config_.evidence_rate_hz),
      1L, static_cast<long>(std::numeric_limits<std::uint16_t>::max())));
  if (track.decoder_rejection_samples < recovery_samples)
    ++track.decoder_rejection_samples;
  if (track.decoder_rejection_samples < recovery_samples) return;

  // The carrier/noise filters remain valid; only timing/text state is
  // reacquired. This lets a real transmission take over a frequency that was
  // previously occupied by a persistent noise-derived timing hypothesis.
  // No verified text is rewritten because recovery is restricted to tracks
  // that have never reached verification.
  track.decoder.reset();
  track.update = {};
  track.decoder_rejection_samples = 0;
  track.verification_pass_samples = 0;
  track.verification_fail_samples = 0;
  track.verification_state = CwTrackState::Candidate;
  track.verification_reason = CwVerificationReason::NeedsKeyingEdges;
  ++decoder_reacquisitions_;
}

std::vector<CwTrackDiagnostic> CwChannelBank::allTrackDiagnostics() const {
  std::vector<CwTrackDiagnostic> result;
  result.reserve(tracks_.size());
  for (const auto& track : tracks_) {
    result.push_back({
        .id = track.id,
        .frequency_hz = track.frequency_hz,
        .drift_hz_per_second = track.drift_hz_per_second,
        .snr_db = track.snr_db,
        .narrowband_coherence = track.narrowband_coherence,
        .filter_width_hz = kNarrowbandWidthsHz[track.selected_width_index],
        .verification_state = track.verification_state,
        .verification_reason = track.verification_reason,
        .spectral_observations = track.spectral_observations,
        .key_transitions = track.update.key_transitions,
        .decoded_symbols = track.update.decoded_symbols,
        .unknown_symbols = track.update.unknown_symbols,
        .timing_quality = track.update.timing_quality,
        .cadence_quality = track.update.cadence_quality,
        .mean_character_confidence = track.update.mean_character_confidence,
        .wpm = track.update.wpm,
        .acoustic_wpm = track.update.acoustic_wpm,
        .acoustic_cadence_confidence =
            track.update.acoustic_cadence_confidence,
        .text = track.update.text,
        .provisional_text = track.update.provisional_text,
    });
  }
  return result;
}

void CwChannelBank::rebuildSnapshots(const std::uint64_t timestamp_ns) {
  for (RetainedObservation& observation : retained_observations_)
    observation.refreshed = false;

  for (const auto& track : tracks_) {
    if (track.verification_state != CwTrackState::Verified) continue;
    const std::string callsign =
        track.update.timing_quality >=
                config_.minimum_verification_timing_quality
            ? CallsignPolicy::latest_complete_in_text(track.update.text)
                  .value_or(std::string{})
            : std::string{};
    CwChannelSnapshot snapshot{
        .id = track.id,
        .color_index = track.color_index,
        .frequency_hz = track.frequency_hz,
        .drift_hz_per_second = track.drift_hz_per_second,
        .filter_width_hz =
            kNarrowbandWidthsHz[track.selected_width_index],
        .snr_db = track.snr_db,
        .wpm = track.update.wpm,
        .acoustic_wpm = track.update.acoustic_wpm,
        .acoustic_cadence_confidence =
            track.update.acoustic_cadence_confidence,
        .confidence = track.update.confidence,
        .key_down_probability = track.update.key_down_probability,
        .key_down = track.update.key_down,
        .active = track.verification_reason !=
                          CwVerificationReason::SignalLost &&
                  (track.update.key_down ||
                   track.spectral_snr_db >= config_.retention_snr_db),
        .verified_cw = true,
        .verification_state = track.verification_state,
        .verification_reason = track.verification_reason,
        .verification_confidence = track.verification_confidence,
        .verification_cadence_quality =
            track.verification_cadence_quality,
        .verification_timing_quality = track.verification_timing_quality,
        .verification_character_confidence =
            track.verification_character_confidence,
        .cadence_quality = track.update.cadence_quality,
        .mean_character_confidence =
            track.update.mean_character_confidence,
        .narrowband_coherence = track.narrowband_coherence,
        .key_transitions = track.update.key_transitions,
        .characters = track.update.characters,
        .text = track.update.text,
        .provisional_text = track.update.provisional_text,
        .pending_elements = track.update.pending_elements,
        .callsign = callsign,
    };

    auto retained = std::find_if(
        retained_observations_.begin(), retained_observations_.end(),
        [&](const RetainedObservation& observation) {
          if (observation.snapshot.id == track.id) return true;
          return observation.snapshot.color_index == track.color_index &&
                 std::abs(observation.snapshot.frequency_hz -
                          track.frequency_hz) <=
                     config_.color_identity_tolerance_hz;
        });
    if (retained == retained_observations_.end()) {
      if (retained_observations_.size() >= kColorLeaseCount) {
        retained = std::min_element(
            retained_observations_.begin(), retained_observations_.end(),
            [](const RetainedObservation& left,
               const RetainedObservation& right) {
              if (left.refreshed != right.refreshed)
                return !left.refreshed;
              return left.last_seen_ns < right.last_seen_ns;
            });
      } else {
        retained_observations_.push_back({});
        retained = std::prev(retained_observations_.end());
      }
    }
    retained->snapshot = std::move(snapshot);
    retained->last_seen_ns = track.last_detected_ns;
    retained->refreshed = true;
  }

  const auto observation_expired = [&](const RetainedObservation& observation) {
    if (timestamp_ns < observation.last_seen_ns) return false;
    const long double age_seconds =
        static_cast<long double>(timestamp_ns - observation.last_seen_ns) /
        1'000'000'000.0L;
    return age_seconds > config_.decoded_track_retention_seconds;
  };
  std::erase_if(retained_observations_, observation_expired);

  snapshots_.clear();
  snapshots_.reserve(retained_observations_.size());
  for (RetainedObservation& observation : retained_observations_) {
    if (!observation.refreshed) {
      observation.snapshot.active = false;
      observation.snapshot.key_down = false;
      observation.snapshot.key_down_probability = 0.0F;
      observation.snapshot.verification_state = CwTrackState::Lost;
      observation.snapshot.verification_reason =
          CwVerificationReason::SignalLost;
    }
    snapshots_.push_back(observation.snapshot);
  }
  std::sort(snapshots_.begin(), snapshots_.end(),
            [](const CwChannelSnapshot& left,
               const CwChannelSnapshot& right) {
              return left.frequency_hz < right.frequency_hz;
            });
}

}  // namespace cwassistant::core
