#include "cwassistant/core/cw_channel_bank.hpp"

#include <algorithm>
#include <cmath>

namespace cwassistant::core {

CwChannelBank::Track::Track(const std::uint64_t track_id,
                            const double frequency,
                            const std::uint64_t timestamp_ns)
    : id(track_id),
      frequency_hz(frequency),
      last_detected_ns(timestamp_ns) {}

CwChannelBank::CwChannelBank(CwChannelBankConfig config) : config_(config) {
  config_.acquisition_snr_db =
      std::clamp(config_.acquisition_snr_db, 3.0F, 30.0F);
  config_.retention_snr_db = std::clamp(
      config_.retention_snr_db, 0.0F, config_.acquisition_snr_db);
  config_.minimum_separation_hz =
      std::clamp(config_.minimum_separation_hz, 5.0, 500.0);
  config_.tracking_tolerance_hz = std::clamp(
      config_.tracking_tolerance_hz, config_.minimum_separation_hz, 1'000.0);
  config_.empty_track_retention_seconds =
      std::clamp(config_.empty_track_retention_seconds, 0.5, 30.0);
  config_.decoded_track_retention_seconds = std::clamp(
      config_.decoded_track_retention_seconds,
      config_.empty_track_retention_seconds, 120.0);
  config_.maximum_tracks =
      std::clamp<std::size_t>(config_.maximum_tracks, 1, 64);
}

void CwChannelBank::reset() noexcept {
  tracks_.clear();
  snapshots_.clear();
  next_track_id_ = 1;
}

const std::vector<CwChannelSnapshot>& CwChannelBank::process(
    const std::uint64_t timestamp_ns, const double lower_frequency_hz,
    const double upper_frequency_hz, const std::span<const float> bins_dbfs) {
  if (bins_dbfs.size() < 3 ||
      !std::isfinite(lower_frequency_hz) ||
      !std::isfinite(upper_frequency_hz) ||
      upper_frequency_hz <= lower_frequency_hz) {
    snapshots_.clear();
    return snapshots_;
  }

  const double bin_width_hz =
      (upper_frequency_hz - lower_frequency_hz) /
      static_cast<double>(bins_dbfs.size() - 1);
  const float noise_dbfs = estimateNoise(bins_dbfs);
  std::vector<Candidate> candidates;
  candidates.reserve(std::min<std::size_t>(bins_dbfs.size(), 64));
  for (std::size_t bin = 1; bin + 1 < bins_dbfs.size(); ++bin) {
    const float level = bins_dbfs[bin];
    if (!std::isfinite(level) || level < bins_dbfs[bin - 1] ||
        level < bins_dbfs[bin + 1]) {
      continue;
    }
    const float snr_db = level - noise_dbfs;
    if (snr_db < config_.acquisition_snr_db) continue;
    candidates.push_back({
        .frequency_hz = lower_frequency_hz +
                        static_cast<double>(bin) * bin_width_hz,
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
      const double distance =
          std::abs(track->frequency_hz - candidate.frequency_hz);
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
    nearest->frequency_hz +=
        0.25 * (candidate.frequency_hz - nearest->frequency_hz);
    nearest->last_detected_ns = timestamp_ns;
  }

  for (auto& track : tracks_) {
    track.snr_db = trackSnr(track, lower_frequency_hz, bin_width_hz,
                            bins_dbfs, noise_dbfs);
    if (track.snr_db >= config_.retention_snr_db)
      track.last_detected_ns = timestamp_ns;
    track.update = track.decoder.process(timestamp_ns, track.snr_db);
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

float CwChannelBank::trackSnr(const Track& track,
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

void CwChannelBank::rebuildSnapshots() {
  snapshots_.clear();
  snapshots_.reserve(tracks_.size());
  for (const auto& track : tracks_) {
    snapshots_.push_back({
        .id = track.id,
        .color_index = static_cast<std::uint8_t>((track.id - 1) % 24),
        .frequency_hz = track.frequency_hz,
        .snr_db = track.snr_db,
        .wpm = track.update.wpm,
        .confidence = track.update.confidence,
        .key_down_probability = track.update.key_down_probability,
        .key_down = track.update.key_down,
        .active = track.update.key_down ||
                  track.snr_db >= config_.retention_snr_db,
        .text = track.update.text,
        .provisional_text = track.update.provisional_text,
        .pending_elements = track.update.pending_elements,
    });
  }
  std::sort(snapshots_.begin(), snapshots_.end(),
            [](const CwChannelSnapshot& left,
               const CwChannelSnapshot& right) {
              return left.frequency_hz < right.frequency_hz;
            });
}

}  // namespace cwassistant::core
