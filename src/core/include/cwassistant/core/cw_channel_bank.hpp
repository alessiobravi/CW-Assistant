#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/callsign_policy.hpp"
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
// Whether decoded text contains a token distinctive enough that its presence is
// itself strong evidence the channel is carrying real Morse, independent of any
// timing or confidence measure.
//
// Motivated by a real capture: a contest station whose decoded text plainly
// read TEST never verified, because its timing quality sat under the threshold
// for the track's entire life. Nothing else about the track was in doubt -- it
// had a carrier, keyed edges, cadence and coherence -- so a legible contest
// token was better evidence than the proxy that rejected it.
//
// The list is deliberately short and skewed to tokens noise is unlikely to
// spell by chance. Short, common ones a random keying pattern lands on
// regularly -- K, DE, R, single letters -- are excluded however useful they
// are to a human reader, because the point is evidence, not readability.
[[nodiscard]] bool cwTextContainsDistinctiveToken(
    std::string_view text) noexcept;

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
  // The operator's own callsign, normalized, or empty when unset. A stream is
  // labelled with the station transmitting on it, and the operator's own call
  // is by definition not that station: hearing it means somebody is calling
  // the operator, and the station to name is the one doing the calling. It
  // reaches the decoded text often -- a caller sends it before its own -- and
  // without this a pileup answering the operator would label every stream with
  // the operator's own call.
  std::string own_callsign{};
  float acquisition_snr_db{7.0F};
  // A track weaker than this is acquired, followed and drawn, but not decoded.
  // Below it what reaches the decoder is fragments rather than copy: on the
  // capture corpus such tracks emit streams of one- and two-element characters
  // that fill a transcript with nothing and spend a decoder's worth of
  // processor time each. The default was first set at twelve decibels from the
  // capture corpus, where the weakest track carrying a correctly recovered
  // callsign sits at 19.5 dB. On the air that proved far too aggressive and
  // suppressed signals an operator could work, so the repository owner set it
  // to four. The corpus was never the whole population: it is twenty-two
  // recordings made on one receiver, and a threshold fitted to it does not
  // transfer to a different front end or a quieter band.
  float minimum_decode_snr_db{4.0F};
  // Decode every tracked signal regardless of level. Off by default because
  // the cost is paid in both transcript quality and processor time.
  bool decode_weak_signals{false};
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
  // How long a track carried out of the processed passband by a receiver
  // retune is held before it is given up.
  //
  // A signal that leaves the passband because the operator turned the dial has
  // not been lost: its position is known exactly, and turning the dial back
  // brings it to a computable place. Expiring it on the ordinary retention
  // timeout destroyed the identity, the transcript and the audio monitor of a
  // station the operator was deliberately tuning around, and it came back as a
  // new, unrecognised track. Generous on purpose -- tuning away and back is
  // measured in tens of seconds -- and bounded so a band-edge sweep cannot
  // accumulate parked tracks without limit.
  double parked_track_retention_seconds{180.0};
  // A verified frequency keeps its display color after the live track expires
  // so later passes from the same carrier do not look like different stations.
  double color_identity_retention_seconds{300.0};
  double color_identity_tolerance_hz{35.0};
  double narrowband_width_hz{120.0};
  double noise_reference_offset_hz{300.0};
  double evidence_rate_hz{500.0};
  std::size_t maximum_tracks{24};
  // Keep the fast per-frame level tracker anchored to two robust modes from a
  // short history. Exposed to the core benchmark so the production path can
  // be compared with its exact pre-anchor baseline on identical audio; normal
  // application configurations leave this enabled.
  bool robust_keying_level_history{true};
  // The detector averages the supplied spectrum itself, over a fixed time
  // constant, so that display-side averaging and the display frame rate can
  // never change candidate discovery. Callers should supply unaveraged bins.
  double detector_averaging_seconds{0.05};
  // Detection runs on its own fixed cadence. Spectrum frames arriving faster
  // than this are folded into the detector's average but do not run an extra
  // detection pass, so raising the display line rate cannot change decoding.
  // Zero processes every supplied frame.
  double detector_frame_interval_seconds{1.0 / 60.0};
  // Spectral persistence is expressed in 60 Hz-equivalent observations and is
  // accumulated from elapsed time, so a track needs the same wall-clock
  // evidence regardless of the configured display frame rate.
  std::uint16_t minimum_spectral_observations{3};
  std::uint16_t minimum_verification_symbols{3};
  std::uint16_t minimum_key_transitions{6};
  std::uint16_t minimum_cadence_observations{3};
  // Raised from 0.45 once element timing was measured without the systematic
  // mark/gap bias. Real CW now sits at 0.85-0.98 and irregularly keyed noise
  // at about 0.44, so the threshold moves into the gap between them instead
  // of sitting on top of the negative.
  float minimum_verification_timing_quality{0.55F};
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
  // Presentation may correct a biased first acquisition and cautiously follow
  // qualified carrier motion, but can never leave this radius around the
  // immutable identity origin. It never participates in DSP association.
  double presentation_reanchor_limit_hz{65.0};
  double presentation_follow_deadband_hz{4.0};
  double presentation_follow_slew_hz_per_second{2.0};
  double presentation_follow_stable_seconds{1.0};
  double presentation_follow_maximum_drift_hz_per_second{5.0};
  double presentation_follow_maximum_mad_hz{6.0};
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
  // Tracks currently verified whose character-quality gates were satisfied by a
  // recognised token rather than by the timing measures. Zero means the path
  // has never been needed; a non-zero count on air is the evidence that it is
  // worth keeping.
  std::size_t pattern_verified_tracks{0};
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
  // True while this channel is visible because the operator explicitly
  // selected its frequency. Selection creates a bounded analysis probe; it
  // never bypasses the ordinary acoustic verification gates.
  bool operator_selected{false};
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
  // Append-only text on which the competitive acoustic timing paths agree.
  // It remains separate from the literal decoder output above.
  std::string refined_text;
  std::vector<CwAcousticAlternative> acoustic_alternatives;
  std::string provisional_text;
  std::string pending_elements;
  std::vector<CwTransmissionTurn> transmissions;
  std::vector<CwSenderCadence> sender_cadences;
  std::uint64_t active_transmission_sequence{0};
  std::string current_sender_callsign;
  double current_sender_wpm{0.0};
  std::string contextual_text;
  std::string callsign;
  // High-confidence CALL1 DE CALL2 participants heard on one carrier. A
  // simplex QSO is one frequency observation containing alternating senders,
  // not two artificial frequency tracks.
  std::vector<std::string> qso_participants;
};

// Full private per-track state, including tracks never shown to the
// operator UI. Intended only for operator-consented diagnostic capture
// (OBS-003); never used to drive the normal display/session models.
struct CwTrackDiagnostic {
  std::uint64_t id{0};
  double frequency_hz{0.0};
  double identity_origin_frequency_hz{0.0};
  double presentation_frequency_hz{0.0};
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
  float keying_level_separation_db{0.0F};
  float keying_level_explained_variation{0.0F};
  bool robust_keying_level_anchor_active{false};
  std::string text;
  std::string refined_text;
  std::vector<CwAcousticAlternative> acoustic_alternatives;
  std::string provisional_text;
  double match_age_seconds{0.0};
  std::uint8_t color_index{0};
  bool matched{false};
  bool active{false};
  bool key_down{false};
  bool operator_selected{false};
};

// Allocation-light view used by the optional character frontend on every
// audio block. Keeping transcript strings, character vectors, and acoustic
// alternatives out of this hot path avoids deep diagnostic snapshots at the
// capture cadence; the complete structure above remains available for the
// explicitly rate-limited debug capture.
struct CwCharacterTrackSnapshot {
  std::uint64_t id{0};
  double frequency_hz{0.0};
  double presentation_frequency_hz{0.0};
  float snr_db{0.0F};
  CwTrackState verification_state{CwTrackState::Candidate};
  bool active{false};
  bool operator_selected{false};
};

enum class CwMonitorMode : std::uint8_t {
  Off,
  FullReceiver,
  SelectedTrack,
};

class CwChannelBank {
 public:
  explicit CwChannelBank(CwChannelBankConfig config = {});
  // Applies a new configuration to future evaluation without discarding
  // existing tracks; every field is sanitized exactly as at construction.
  void configure(CwChannelBankConfig config) noexcept;
  // Separate from configure() deliberately: configure() replaces the whole
  // configuration, so a caller that later adjusts one unrelated field would
  // otherwise silently clear this.
  void setOwnCallsign(std::string callsign);
  // Switches the keying model on every live track without disturbing anything
  // else about them. Their decoders restart -- a different technique cannot
  // inherit another's partial state -- but track identity, frequency and the
  // callsign evidence gathered so far all survive, so an operator can compare
  // two models on the same station without losing it.
  // Changing these affects only which tracks are decoded, never which are
  // detected, so it does not disturb tracking or discard decoder state.
  void setWeakSignalDecoding(bool enabled,
                             float minimum_decode_snr_db) noexcept;

  void setKeyingModel(CwKeyingModel model) noexcept;
  // What the operator is doing. It decides whose callsign a monitored stream is
  // expected to carry, which exchange context alone cannot always settle.
  void setOperatorRole(CwOperatorRole role) noexcept { operator_role_ = role; }
  [[nodiscard]] CwOperatorRole operatorRole() const noexcept {
    return operator_role_;
  }
  [[nodiscard]] CwKeyingModel keyingModel() const noexcept {
    return keying_model_;
  }
  void reset() noexcept;

  // The sample stream jumped, but the stations did not.
  //
  // Re-centring the SDR decoder window, which happens on every receiver
  // retune, changes the slice of spectrum being decoded. That is a real
  // discontinuity in the audio -- filters and timing have to start again --
  // but it says nothing about the signals themselves: a track's frequency is
  // absolute RF, and a station on 14.025 MHz is still on 14.025 MHz after the
  // receiver moves. reset() was called here and destroyed every track,
  // transcript and identity on each retune, which is exactly what an operator
  // sees as "I move the RF spectrum and I lose the tracks".
  //
  // This resets the signal path and leaves the tracks standing. Those the new
  // window no longer covers are parked by the ordinary out-of-band rule and
  // return when the receiver does.
  void noteInputDiscontinuity() noexcept;
  // Re-centers every current track by a known audio-domain frequency shift
  // (for example, the shift implied by an operator retuning the linked
  // radio's RX VFO) and resynchronizes each track's narrowband mixer/filter
  // at its new position, without discarding decoded text, verification
  // state/history, or spectral-observation evidence — unlike a track that
  // drifts or jumps far enough to be lost and re-acquired from scratch, a
  // known, deliberate retune should not interrupt an already-identified
  // signal's identity.
  void shiftTrackedFrequencies(double audio_hz_delta) noexcept;
  // Callers that immediately follow with processSamples() may defer snapshot
  // rebuilding to that call, avoiding a duplicate deep presentation copy for
  // the same audio block. The returned reference then remains the prior view.
  [[nodiscard]] const std::vector<CwChannelSnapshot>& updateSpectrum(
      std::uint64_t timestamp_ns, double lower_frequency_hz,
      double upper_frequency_hz, std::span<const float> bins_dbfs,
      bool rebuild_snapshot = true);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& processSamples(
      const RealtimeSampleBlock& block);
  // Selects the provider-neutral receive monitor. Selected-track audio is
  // taken from the same tracking mixers and adaptive narrow filters used by
  // the decoder, mixed with bounded gain, then translated to the requested
  // sidetone. It never affects decoding, radio state, PTT, or KEY.
  void setMonitor(CwMonitorMode mode, std::uint64_t track_id = 0,
                  double reference_tone_hz = 700.0) noexcept;
  void setMonitorTracks(CwMonitorMode mode,
                        std::span<const std::uint64_t> track_ids,
                        double reference_tone_hz = 700.0) noexcept;
  [[nodiscard]] CwMonitorMode monitorMode() const noexcept {
    return monitor_mode_;
  }
  [[nodiscard]] std::uint64_t monitoredTrackId() const noexcept {
    return monitored_track_count_ == 0 ? 0U : monitored_track_ids_[0];
  }
  [[nodiscard]] std::span<const std::uint64_t> monitoredTrackIds()
      const noexcept {
    return {monitored_track_ids_.data(), monitored_track_count_};
  }
  [[nodiscard]] const std::vector<float>& monitorAudio() const noexcept {
    return monitor_audio_;
  }
  // Creates or refreshes a bounded analysis probe at an operator-selected
  // frequency. Returns its track ID, or zero when no valid spectrum range is
  // available or the frequency is outside that range.
  [[nodiscard]] std::uint64_t selectFrequency(double frequency_hz) noexcept;
  // Supplies append-only, overlap-confirmed output from the optional local
  // character model. It may confirm an already Morse-likely acoustic track,
  // but cannot create a track or bypass carrier/keying/cadence qualification.
  [[nodiscard]] bool acceptCharacterRefinement(
      std::uint64_t track_id, const std::string& stable_text,
      std::uint64_t evidence_timestamp_ns);
  [[nodiscard]] const std::vector<CwChannelSnapshot>& channels() const noexcept;
  [[nodiscard]] CwVerificationDiagnostics verificationDiagnostics() const;
  [[nodiscard]] const std::vector<CwCharacterTrackSnapshot>&
  characterRefinementTracks() const noexcept;
  [[nodiscard]] std::vector<CwTrackDiagnostic> allTrackDiagnostics() const;

 private:
  struct Track {
    Track(std::uint64_t track_id, double frequency, std::uint64_t timestamp_ns);

    std::uint64_t id;
    std::uint8_t color_index{0};
    bool color_assigned{false};
    double frequency_hz;
    // Immutable except for a known receiver retune.
    double identity_origin_frequency_hz;
    // Operator-facing center; independent from DSP and identity association.
    double presentation_frequency_hz;
    double drift_hz_per_second{0.0};
    // Carried outside the processed passband by a receiver retune, and waiting
    // for the dial to bring it back rather than being treated as a lost
    // signal. A parked track is still published, so its marker keeps its place
    // on the frequency it belongs to even while that place is off-screen.
    bool parked{false};
    std::uint64_t parked_since_ns{0};
    std::uint64_t last_detected_ns;
    std::uint64_t last_frequency_update_ns;
    std::uint64_t last_candidate_match_ns;
    CwMultiSpeedDecoder decoder;
    CwDecoderUpdate update;
    float snr_db{0.0F};
    float spectral_snr_db{0.0F};
    bool matched{false};
    // A missing spectral association may be an ordinary Morse gap. Once that
    // bounded hold expires, close the current timing segment exactly once and
    // stop feeding residual/adjacent audio until a real candidate matches.
    bool decoder_input_suspended{false};
    bool operator_selected{false};
    std::uint64_t operator_selected_ns{0};
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
    // Elapsed matched/unmatched spectrum time not yet converted into a
    // 60 Hz-equivalent observation step. Keeping the credit in milliseconds
    // makes persistence independent of the spectrum frame rate.
    double matched_evidence_credit_ms{0.0};
    double unmatched_evidence_credit_ms{0.0};
    std::uint16_t verification_pass_samples{0};
    std::uint16_t verification_fail_samples{0};
    std::uint16_t decoder_rejection_samples{0};
    bool ever_verified{false};
    // The strongest level this track has reached. The decode gate reads this
    // rather than the level of the moment, because a signal is weak while it
    // is still being acquired and gating on that suppresses it before it can
    // establish itself -- measured, that lost a callsign whose settled level
    // was thirty-five decibels.
    float peak_decode_level_db{0.0F};
    // How many sample blocks this track has been filtered for. A track is
    // only judged too weak to be worth filtering once it has had long enough
    // to show what its level actually is.
    std::uint32_t decode_level_observations{0};
    bool ever_morse_likely{false};
    // Sticky for the life of the track: having once said CQ, a station does not
    // stop being a station when the word scrolls out of the recent window.
    bool distinctive_token_seen{false};
    std::size_t distinctive_token_scanned_length{0};
    std::uint64_t character_refinement_timestamp_ns{0};
    float keying_snr_db{0.0F};
    // Two-component keying level model held in LINEAR power relative to the
    // side noise reference. Thresholding a dB-domain span at a fixed fraction
    // put the decision far below half amplitude, which lengthened every mark
    // and shortened every gap; the bias grew with signal strength because a
    // stronger carrier widens the dB span.
    // Signal level presented to the operator: the estimated MARK level, not
    // the instantaneous reading. A keyed carrier is present only half the
    // time, so an instantaneous figure swings between roughly +28 dB inside a
    // mark and below zero inside a gap, and whichever value the display
    // sampled tells the operator nothing about the signal. Observed on a
    // receiver capture: a perfectly readable station reported -6.3 dB because
    // the sample landed in a gap.
    float keying_mark_snr_db{0.0F};
    float keying_space_power{0.0F};
    float keying_mark_power{0.0F};
    // Scatter of the observed amplitude about the level it was assigned to.
    // This is the noise on the keying decision itself, and dividing the level
    // separation by it is what converts a bare amplitude distance into
    // evidence: the same gap is decisive on a quiet channel and meaningless
    // on a noisy one.
    float keying_space_variance{0.0F};
    float keying_mark_variance{0.0F};
    bool keying_envelope_initialized{false};
    // A short, allocation-free amplitude history periodically anchors the
    // online two-level tracker to two robust modes.  The per-frame tracker is
    // still needed for attack, fading, and weighting; the history prevents a
    // run of ambiguous edge samples from walking both levels together.
    static constexpr std::size_t kKeyingLevelHistorySize = 256;
    std::array<float, kKeyingLevelHistorySize> keying_level_history{};
    std::size_t keying_level_history_count{0};
    std::size_t keying_level_history_index{0};
    std::uint8_t keying_level_fit_countdown{0};
    float keying_level_explained_variation{0.0F};
    bool robust_keying_level_anchor_active{false};

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
    float monitor_peak_envelope{0.0F};
    std::uint8_t selected_width_index{1};
    std::uint8_t pending_width_index{1};
    std::uint16_t pending_width_observations{0};
    std::uint16_t total_width_observations{0};
    bool noise_initialized{false};
    bool filter_initialized{false};
    static constexpr std::size_t kPresentationEvidenceWindow = 15;
    std::array<double, kPresentationEvidenceWindow>
        presentation_frequency_evidence{};
    std::size_t presentation_frequency_evidence_count{0};
    std::size_t presentation_frequency_evidence_index{0};
    double presentation_follow_median_hz{0.0};
    std::uint64_t presentation_follow_stable_since_ns{0};
    std::uint64_t last_presentation_follow_update_ns{0};
    std::uint64_t last_presentation_evidence_ns{0};
  };

  struct Candidate {
    double frequency_hz{0.0};
    float snr_db{0.0F};
    // An established track may reserve its nearest raw ridge before global
    // peak-separation ranking. This prevents a stronger adjacent skirt/noise
    // peak from suppressing the real ridge and spawning a duplicate identity.
    std::uint64_t preferred_track_id{0};
  };

  struct ColorLease {
    double frequency_hz{0.0};
    std::uint64_t last_seen_ns{0};
    bool occupied{false};
  };

  struct RetainedObservation {
    CwChannelSnapshot snapshot;
    // Presentation continuity has explicit provenance. `source_track_id`
    // owns the live suffix; `inherited_text_prefix` is frozen only when a
    // genuine predecessor is replaced, so composed UI text is never fed back
    // through callsign scoring or appended again on the next refresh.
    std::uint64_t source_track_id{0};
    std::string inherited_text_prefix;
    std::string confirmed_callsign;
    std::vector<std::string> confirmed_qso_participants;
    std::uint64_t last_seen_ns{0};
    bool refreshed{false};
  };

  static constexpr std::size_t kColorLeaseCount = 24;

  [[nodiscard]] float estimateNoise(std::span<const float> bins_dbfs) const;
  [[nodiscard]] float spectralSnr(const Track& track, double lower_frequency_hz,
                                  double bin_width_hz,
                                  std::span<const float> bins_dbfs,
                                  float noise_dbfs) const;
  void sanitizeConfig() noexcept;
  void applyKeyingModel() noexcept;
  // Deliberately not part of CwChannelBankConfig. configure() replaces the
  // whole config, and callers legitimately build one with a single designated
  // initialiser to change one unrelated setting -- the decoded-track retention
  // does exactly that. A keying model living in there would revert to the
  // default every time an unrelated slider moved, silently and only sometimes.
  CwKeyingModel keying_model_{CwKeyingModel::AdaptiveThreshold};
  // Kept out of the config for the same reason as the keying model: configure()
  // replaces the whole config, and a caller changing one unrelated setting with
  // a designated initialiser would silently reset this.
  CwOperatorRole operator_role_{CwOperatorRole::Monitor};
  void resetFilter(Track& track) noexcept;
  // Parks every track the analysed band no longer covers and revives every
  // one it has come back to. One rule, used by both a VFO move and a retune of
  // the IQ decoder window, so the two cannot disagree about what "out of band"
  // means.
  void parkTracksOutsideBand(std::uint64_t timestamp_ns) noexcept;
  void updateVerification(Track& track, std::uint64_t timestamp_ns);
  void recoverRejectedDecoder(Track& track);
  void assignOrRefreshColor(Track& track, std::uint64_t timestamp_ns) noexcept;
  void observePresentationFrequency(Track& track, double candidate_frequency_hz,
                                    std::uint64_t timestamp_ns) noexcept;
  void reanchorPresentationOnFirstVerification(
      Track& track, std::uint64_t timestamp_ns) noexcept;
  void followVerifiedPresentation(Track& track,
                                  std::uint64_t timestamp_ns) noexcept;
  [[nodiscard]] bool colorLeaseIsCurrent(
      const ColorLease& lease, std::uint64_t timestamp_ns) const noexcept;
  void rebuildSnapshots(std::uint64_t timestamp_ns);

  CwChannelBankConfig config_;
  std::vector<Track> tracks_;
  std::vector<CwChannelSnapshot> snapshots_;
  std::vector<CwCharacterTrackSnapshot> character_refinement_tracks_;
  std::vector<float> monitor_audio_;
  std::vector<RetainedObservation> retained_observations_;
  std::array<ColorLease, kColorLeaseCount> color_leases_{};
  std::uint64_t next_track_id_{1};
  CwMonitorMode monitor_mode_{CwMonitorMode::Off};
  std::array<std::uint64_t, kColorLeaseCount> monitored_track_ids_{};
  std::size_t monitored_track_count_{0};
  double monitor_reference_tone_hz_{700.0};
  std::complex<float> monitor_oscillator_{1.0F, 0.0F};
  StreamDescriptor stream_{};
  std::uint64_t expected_sample_timestamp_ns_{0};
  std::uint64_t last_spectrum_timestamp_ns_{0};
  // Detector-owned averaged spectrum. Display averaging is deliberately not an
  // input to detection; this buffer is smoothed over a fixed time constant so
  // the same signal produces the same candidates at any display frame rate.
  std::vector<float> detector_bins_dbfs_;
  std::vector<float> detector_bins_power_;
  bool detector_average_initialized_{false};
  std::uint64_t last_detector_pass_ns_{0};
  bool detector_pass_initialized_{false};
  double last_spectrum_lower_frequency_hz_{0.0};
  double last_spectrum_upper_frequency_hz_{0.0};
  bool spectrum_range_initialized_{false};
  bool stream_initialized_{false};
  bool sample_timing_initialized_{false};
  std::uint64_t verified_transitions_{0};
  std::uint64_t expired_unverified_tracks_{0};
  std::uint64_t decoder_reacquisitions_{0};
};

}  // namespace cwassistant::core
