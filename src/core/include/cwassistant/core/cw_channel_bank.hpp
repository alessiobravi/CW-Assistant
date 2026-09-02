#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cwassistant/core/cw_decoder.hpp"
#include "cwassistant/core/sample_block.hpp"

namespace cwassistant::core {

enum class CwTrackState : std::uint8_t {
  Candidate,
  MorseLikely,
  Verified,
  Lost,
};

enum class CwVerificationReason : std::uint8_t {
  NeedsSpectralPersistence,
  NeedsKeyingEdges,
  NeedsCadenceEvidence,
  LowNarrowbandCoherence,
  LowCadenceQuality,
  NeedsDecodedSymbols,
  TooManyUnknownSymbols,
  LowTimingQuality,
  LowCharacterConfidence,
  NeedsSustainedEvidence,
  ImplausibleCharacterDistribution,
  Verified,
  SignalLost,
};

inline constexpr std::size_t kCwVerificationReasonCount =
    static_cast<std::size_t>(CwVerificationReason::SignalLost) + 1U;

[[nodiscard]] const char* cwTrackStateName(CwTrackState state) noexcept;
[[nodiscard]] const char* cwVerificationReasonName(
    CwVerificationReason reason) noexcept;

// A long run of decoded text dominated by only the two single-element
// characters (E, T) is the statistical signature of timing noise being
// classified as Morse rather than genuine text: random on/off fluctuations
// rarely sustain the longer runs needed for other characters, while real
// ham/English text sits close to natural letter frequency (E+T is typically
// ~20%). False below `minimum_characters` since the fraction is not yet
// statistically meaningful. Exposed standalone (rather than inlined into the
// verification gate) so its threshold behavior can be tested directly
// against literal decoded-text examples.
[[nodiscard]] bool isCharacterDistributionImplausible(
    const std::string& text, std::uint16_t minimum_characters,
    float maximum_simple_character_fraction) noexcept;

struct CwChannelBankConfig {
  float acquisition_snr_db{7.0F};
  float retention_snr_db{2.5F};
  float detection_dynamic_range_db{96.0F};
  float minimum_peak_prominence_db{4.5F};
  float minimum_near_peak_prominence_db{0.8F};
  double prominence_reference_offset_hz{160.0};
  double prominence_reference_width_hz{100.0};
  double minimum_separation_hz{45.0};
  double tracking_tolerance_hz{70.0};
  double empty_track_retention_seconds{2.0};
  double decoded_track_retention_seconds{30.0};
  double unverified_track_retention_seconds{0.75};
  // A verified frequency keeps its display color after the live track expires
  // so later passes from the same carrier do not look like different stations.
  double color_identity_retention_seconds{300.0};
  double color_identity_tolerance_hz{35.0};
  double narrowband_width_hz{120.0};
  double noise_reference_offset_hz{300.0};
  double evidence_rate_hz{500.0};
  std::size_t maximum_tracks{24};
  std::uint16_t minimum_spectral_observations{3};
  std::uint16_t minimum_verification_symbols{3};
  std::uint16_t minimum_key_transitions{6};
  std::uint16_t minimum_cadence_observations{3};
  float minimum_verification_timing_quality{0.45F};
  float minimum_verification_cadence_quality{0.42F};
  float minimum_character_confidence{0.40F};
  // A long run of decoded text dominated by only the two single-element
  // characters (E, T) is the statistical signature of timing noise being
  // classified as Morse rather than genuine text: random on/off
  // fluctuations rarely sustain the longer runs needed for other
  // characters, while real ham/English text sits close to natural letter
  // frequency (E+T is typically ~20%). This check only applies once enough
  // text has accumulated to be statistically meaningful, and — unlike every
  // other gate — is re-evaluated even for an already-verified track, since
  // it can only be judged from accumulated text, not a single instant.
  std::uint16_t minimum_plausibility_check_characters{40};
  float maximum_simple_character_fraction{0.35F};
  // Normalized spectral concentration: 0 is approximately wideband noise,
  // 1 is a tone concentrated in the narrowest analysis filter.
  float minimum_narrowband_coherence{0.18F};
  float maximum_verification_unknown_fraction{0.30F};
  double track_identity_tolerance_hz{35.0};
  float track_replacement_margin_db{3.0F};
  double verification_enter_seconds{0.50};
  double verification_exit_seconds{6.0};
  double decoder_recovery_seconds{3.0};
};

struct CwVerificationDiagnostics {
  std::size_t candidate_tracks{0};
  std::size_t morse_likely_tracks{0};
  std::size_t verified_tracks{0};
  std::uint64_t verified_transitions{0};
  std::uint64_t expired_unverified_tracks{0};
  std::uint64_t decoder_reacquisitions{0};
  std::uint32_t maximum_decoded_symbols{0};
  std::uint32_t maximum_key_transitions{0};
  float best_timing_quality{0.0F};
  float best_cadence_quality{0.0F};
  float best_narrowband_coherence{0.0F};
  std::array<std::size_t, kCwVerificationReasonCount> current_reason_counts{};
};

struct CwChannelSnapshot {
  std::uint64_t id{0};
  std::uint8_t color_index{0};
  double frequency_hz{0.0};
  // Stable operator-facing center. The adaptive tracker may move within its
  // bounded identity region, but presentation moves only on a known retune.
  double presentation_frequency_hz{0.0};
  double drift_hz_per_second{0.0};
  double filter_width_hz{120.0};
  float snr_db{0.0F};
  double wpm{0.0};
  double acoustic_wpm{0.0};
  float acoustic_cadence_confidence{0.0F};
  float confidence{0.0F};
  float key_down_probability{0.0F};
  bool key_down{false};
  bool active{false};
  bool verified_cw{false};
  CwTrackState verification_state{CwTrackState::Candidate};
  CwVerificationReason verification_reason{
      CwVerificationReason::NeedsSpectralPersistence};
  float verification_confidence{0.0F};
  float verification_cadence_quality{0.0F};
  float verification_timing_quality{0.0F};
  float verification_character_confidence{0.0F};
  float cadence_quality{0.0F};
  float mean_character_confidence{0.0F};
  float narrowband_coherence{0.0F};
  std::uint32_t key_transitions{0};
  std::vector<CwCharacterEvidence> characters;
  std::string text;
  std::string provisional_text;
  std::string pending_elements;
  std::string callsign;
};

// Full private per-track state, including tracks never shown to the
// operator UI. Intended only for operator-consented diagnostic capture
// (OBS-003); never used to drive the normal display/session models.
struct CwTrackDiagnostic {
  std::uint64_t id{0};
  double frequency_hz{0.0};
  double drift_hz_per_second{0.0};
  float snr_db{0.0F};
  float narrowband_coherence{0.0F};
  double filter_width_hz{120.0};
  CwTrackState verification_state{CwTrackState::Candidate};
  CwVerificationReason verification_reason{
      CwVerificationReason::NeedsSpectralPersistence};
  std::uint16_t spectral_observations{0};
  std::uint32_t key_transitions{0};
  std::uint32_t decoded_symbols{0};
  std::uint32_t unknown_symbols{0};
  float timing_quality{0.0F};
  float cadence_quality{0.0F};
  float mean_character_confidence{0.0F};
  double wpm{0.0};
  double acoustic_wpm{0.0};
  float acoustic_cadence_confidence{0.0F};
  std::string text;
  std::string provisional_text;
};

class CwChannelBank {
 public:
  explicit CwChannelBank(CwChannelBankConfig config = {});
  // Applies a new configuration to future evaluation without discarding
  // existing tracks; every field is sanitized exactly as at construction.
  void configure(CwChannelBankConfig config) noexcept;
  void reset() noexcept;
  // Re-centers every current track by a known audio-domain frequency shift
  // (for example, the shift implied by an operator retuning the linked
  // radio's RX VFO) and resynchronizes each track's narrowband mixer/filter
  // at its new position, without discarding decoded text, verification
  // state/history, or spectral-observation evidence — unlike a track that
  // drifts or jumps far enough to be lost and re-acquired from scratch, a
  // known, deliberate retune should not interrupt an already-identified
  // signal's identity.
  void shiftTrackedFrequencies(double audio_hz_delta) noexcept;
  [[nodiscard]] const std::vector<CwChannelSnapshot>& updateSpectrum(
      std::uint64_t timestamp_ns, double lower_frequency_hz,
      double upper_frequency_hz, std::span<const float> bins_dbfs);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& processSamples(
      const RealtimeSampleBlock& block);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& channels() const noexcept;
  [[nodiscard]] CwVerificationDiagnostics verificationDiagnostics() const;
  [[nodiscard]] std::vector<CwTrackDiagnostic> allTrackDiagnostics() const;

 private:
  struct Track {
    Track(std::uint64_t track_id, double frequency,
          std::uint64_t timestamp_ns);

    std::uint64_t id;
    std::uint8_t color_index{0};
    bool color_assigned{false};
    double frequency_hz;
    double identity_anchor_frequency_hz;
    double drift_hz_per_second{0.0};
    std::uint64_t last_detected_ns;
    std::uint64_t last_frequency_update_ns;
    std::uint64_t last_candidate_match_ns;
    CwMultiSpeedDecoder decoder;
    CwDecoderUpdate update;
    float snr_db{0.0F};
    float spectral_snr_db{0.0F};
    bool matched{false};
    CwTrackState verification_state{CwTrackState::Candidate};
    CwVerificationReason verification_reason{
        CwVerificationReason::NeedsSpectralPersistence};
    float verification_confidence{0.0F};
    float verification_cadence_quality{0.0F};
    float verification_timing_quality{0.0F};
    float verification_character_confidence{0.0F};
    float narrowband_coherence{0.0F};
    std::uint16_t spectral_observations{0};
    std::uint16_t consecutive_spectrum_misses{0};
    std::uint16_t verification_pass_samples{0};
    std::uint16_t verification_fail_samples{0};
    std::uint16_t decoder_rejection_samples{0};
    bool ever_verified{false};
    float keying_snr_db{0.0F};
    float keying_floor_db{0.0F};
    float keying_peak_db{0.0F};
    bool keying_envelope_initialized{false};

    std::array<std::array<std::complex<float>, 3>, 3> center_filters{};
    std::array<std::complex<float>, 3> lower_filter{};
    std::array<std::complex<float>, 3> upper_filter{};
    std::complex<float> center_oscillator{1.0F, 0.0F};
    std::complex<float> lower_oscillator{1.0F, 0.0F};
    std::complex<float> upper_oscillator{1.0F, 0.0F};
    std::array<float, 3> center_power_sums{};
    float lower_power_sum{0.0F};
    float upper_power_sum{0.0F};
    std::size_t accumulated_samples{0};
    float lower_noise_power{0.0F};
    float upper_noise_power{0.0F};
    std::uint8_t selected_width_index{1};
    std::uint8_t pending_width_index{1};
    std::uint16_t pending_width_observations{0};
    std::uint16_t total_width_observations{0};
    bool noise_initialized{false};
    bool filter_initialized{false};
  };

  struct Candidate {
    double frequency_hz{0.0};
    float snr_db{0.0F};
  };

  struct ColorLease {
    double frequency_hz{0.0};
    std::uint64_t last_seen_ns{0};
    bool occupied{false};
  };

  struct RetainedObservation {
    CwChannelSnapshot snapshot;
    std::uint64_t last_seen_ns{0};
    bool refreshed{false};
  };

  static constexpr std::size_t kColorLeaseCount = 24;

  [[nodiscard]] float estimateNoise(std::span<const float> bins_dbfs) const;
  [[nodiscard]] float spectralSnr(const Track& track,
                                  double lower_frequency_hz,
                                  double bin_width_hz,
                                  std::span<const float> bins_dbfs,
                                  float noise_dbfs) const;
  void sanitizeConfig() noexcept;
  void resetFilter(Track& track) noexcept;
  void updateVerification(Track& track, std::uint64_t timestamp_ns);
  void recoverRejectedDecoder(Track& track);
  void assignOrRefreshColor(Track& track,
                            std::uint64_t timestamp_ns) noexcept;
  [[nodiscard]] bool colorLeaseIsCurrent(
      const ColorLease& lease, std::uint64_t timestamp_ns) const noexcept;
  void rebuildSnapshots(std::uint64_t timestamp_ns);

  CwChannelBankConfig config_;
  std::vector<Track> tracks_;
  std::vector<CwChannelSnapshot> snapshots_;
  std::vector<RetainedObservation> retained_observations_;
  std::array<ColorLease, kColorLeaseCount> color_leases_{};
  std::uint64_t next_track_id_{1};
  StreamDescriptor stream_{};
  std::uint64_t expected_sample_timestamp_ns_{0};
  bool stream_initialized_{false};
  bool sample_timing_initialized_{false};
  std::uint64_t verified_transitions_{0};
  std::uint64_t expired_unverified_tracks_{0};
  std::uint64_t decoder_reacquisitions_{0};
};

}  // namespace cwassistant::core
