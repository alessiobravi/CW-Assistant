#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/cw_event_lattice.hpp"
#include "cwassistant/core/cw_semi_markov_segmenter.hpp"

namespace cwassistant::core {

struct CwCharacterEvidence {
  std::string symbol;
  float confidence{0.0F};
  float timing_quality{0.0F};
  bool known{false};
};

// A segment-scoped acoustic alternative from the mark/gap timing lattice.
// Unlike `text`, alternatives may change as more envelope evidence arrives.
// Observation IDs let a consumer relate every suggestion back to immutable
// physical runs without treating a plausible Morse string as ground truth.
struct CwAcousticAlternative {
  std::string text;
  std::string provisional_elements;
  double wpm{0.0};
  double acoustic_cost{0.0};
  float evidence_confidence{0.0F};
  std::uint64_t first_observation_id{0};
  std::uint64_t last_observation_id{0};
};

struct CwTransmissionTurn {
  std::uint64_t sequence{0};
  std::string text;
  // Empty unless explicit handover words identify the sender. A repeated or
  // merely call-shaped token is intentionally insufficient.
  std::string sender_callsign;
  double wpm{0.0};
  float cadence_confidence{0.0F};
};

struct CwSenderCadence {
  std::string callsign;
  double wpm{0.0};
  float confidence{0.0F};
  std::uint32_t observed_turns{0};
};

struct CwDecoderConfig {
  // Which technique decides where the key goes down and comes back up.
  // Everything after that decision -- element assembly, the event lattice, the
  // verification gate, callsign policy -- is shared, so this selects a
  // segmenter and changes nothing else.
  CwKeyingModel keying_model{CwKeyingModel::AdaptiveThreshold};
  float key_on_snr_db{6.0F};
  float key_off_snr_db{3.0F};
  double initial_wpm{20.0};
  float key_on_probability{0.68F};
  float key_off_probability{0.32F};
  double evidence_time_constant_ms{12.0};
  // Threshold separating an element gap from a character gap. The midpoint
  // between the two nominal lengths would be 2.0, but a gap is not measured
  // in clean conditions: a noise excursion inside it registers as a mark and
  // eats it from both ends, so measured gaps run short and a midpoint
  // threshold breaks apart characters that were never spaced. Sitting nearer
  // the nominal character gap costs nothing on unambiguous spacing and
  // recovers those characters. Measured paired against the same noise draws
  // across three independent seed sets, this lowers mean character error by
  // 0.018, 0.039 and 0.013 -- the absolute figure swings by about 0.03 with
  // the draw, so only the paired difference means anything here.
  double character_gap_dots{2.8};
  double stable_gap_dots{3.1};
  double word_gap_dots{6.0};
};

struct CwDecoderUpdate {
  bool changed{false};
  bool key_down{false};
  float key_down_probability{0.0F};
  double wpm{0.0};
  float confidence{0.0F};
  std::string text;
  std::string provisional_text;
  std::string pending_elements;
  float timing_quality{0.0F};
  float cadence_quality{0.0F};
  float mean_character_confidence{0.0F};
  std::uint32_t decoded_symbols{0};
  std::uint32_t unknown_symbols{0};
  std::uint32_t key_transitions{0};
  std::uint32_t cadence_observations{0};
  std::vector<CwCharacterEvidence> characters;
  // Append-only text agreed by every acoustically competitive lattice path at
  // completed symbol boundaries. It is deliberately separate from `text`
  // until capture-calibrated promotion into the primary decoder is proven.
  std::string refined_text;
  std::vector<CwAcousticAlternative> acoustic_alternatives;
  // Verification and hypothesis selection use this bounded recent window so
  // a track can recover from an earlier bad acquisition instead of carrying
  // lifetime-average evidence forever. The lifetime counters above remain
  // available for diagnostics.
  std::uint32_t recent_decoded_symbols{0};
  std::uint32_t recent_unknown_symbols{0};
  std::uint32_t recent_cadence_observations{0};
  // Independent run-length fit of recent marks/gaps to Morse's 1:3 and
  // 1:3:7 timing ratios. This is acoustic cadence evidence, not a language
  // prediction and not necessarily the currently selected decoder speed.
  double acoustic_wpm{0.0};
  float acoustic_cadence_confidence{0.0F};
  // Bounded semantic records made only at sustained-silence or explicit
  // flush boundaries. They keep alternating operators on one RF carrier
  // separate without inventing extra frequency tracks.
  std::vector<CwTransmissionTurn> transmissions;
  std::vector<CwSenderCadence> sender_cadences;
  std::uint64_t active_transmission_sequence{0};
  std::string current_sender_callsign;
  double current_sender_wpm{0.0};
  // Presentation transcript with only conservative word-boundary repair.
  // `text` and `refined_text` above remain the immutable acoustic records.
  std::string contextual_text;
};

class CwTimingDecoder {
 public:
  explicit CwTimingDecoder(CwDecoderConfig config = {});
  void reset() noexcept;
  [[nodiscard]] const CwDecoderUpdate& process(std::uint64_t timestamp_ns,
                                               float snr_db);
  [[nodiscard]] const CwDecoderUpdate& flush(std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t stateBytes() const noexcept;
  // Exact inverse of the internal evidence-to-probability logistic. A detector
  // that can state its keying decision as a log-likelihood ratio encodes it
  // through this, and the probability this decoder then works with is a
  // calibrated posterior instead of a second squash of an already shaped
  // ramp. Kept beside its inverse so the two cannot drift apart.
  [[nodiscard]] float evidenceForLogLikelihoodRatio(
      float log_likelihood_ratio) const noexcept;
  // Runs committed by the most recent process() call, with the timestamps the
  // segmenter decided rather than the frame clock they were reported on.
  // Anything measuring run length -- cadence, the event lattice -- has to read
  // them from here: sampling the key state once per incoming frame both dates
  // a run to when it was noticed instead of when it happened, and loses every
  // transition but the last when several are committed together.
  [[nodiscard]] const std::vector<CwSegment>& committedSegments()
      const noexcept {
    return committed_segments_;
  }
  [[nodiscard]] double segmentationScore() const noexcept {
    return segmenter_ != nullptr ? segmenter_->segmentationScore() : 0.0;
  }
  // Whether a segmenter decides this decoder's runs. Cadence and the event
  // lattice read run boundaries from a different place in each case, so the
  // caller has to know which.
  [[nodiscard]] bool usesSegmenter() const noexcept {
    return segmenter_ != nullptr;
  }
  [[nodiscard]] const CwDecoderUpdate& currentUpdate() const noexcept {
    return cached_update_;
  }

 private:
  // The shipped per-frame path. It is no longer driven by the live frame
  // clock: the segmenter decides where runs begin and end, and this replays
  // its decisions at their own timestamps, so element classification, speed
  // adaptation, the event lattice and everything downstream of them run
  // exactly as before -- on runs that are no longer a threshold's opinion.
  const CwDecoderUpdate& processFrame(std::uint64_t timestamp_ns,
                                      float snr_db);
  bool replayCommittedSegments();
  [[nodiscard]] float snrForSegment(const CwSegment& segment) const noexcept;
  void finishElement(double duration_ms);
  void finishCharacter();
  void promoteProvisional();
  [[nodiscard]] float probabilityForSnr(float snr_db) const noexcept;
  [[nodiscard]] const CwDecoderUpdate& snapshot(bool changed);

  CwDecoderConfig config_;
  // Null for AdaptiveThreshold, which keeps its own per-frame path. Held by
  // pointer so a model that is not selected costs nothing: the duration
  // model's search windows come to roughly a megabyte per track across the
  // nine speed anchors.
  std::unique_ptr<CwKeyingSegmenter> segmenter_;
  // Scalar evidence changes at the 500 Hz decoder cadence, while transcript
  // strings and character vectors change only at Morse boundaries. Retaining
  // one update object lets the multi-speed bank observe every scalar change
  // without rebuilding all dynamic fields for every hypothesis and frame.
  CwDecoderUpdate cached_update_{};
  std::vector<CwSegment> committed_segments_;
  // Measured rather than assumed, so a committed run is replayed on the same
  // grid it was observed on whatever rate the front end delivers.
  double anchor_dot_ms_{60.0};
  double input_frame_ms_{2.0};
  std::uint64_t last_input_timestamp_ns_{0};
  std::string stable_text_;
  std::string provisional_text_;
  std::string elements_;
  std::uint64_t state_started_ns_{0};
  // Start of the run preceding the current one. A key excursion shorter than a
  // plausible fraction of an element is impulsive noise, not keying; absorbing
  // it back into the preceding run lets the amplitude decision stay tight
  // enough to time real edges accurately without letting impulses generate
  // characters.
  std::uint64_t previous_state_started_ns_{0};
  bool has_previous_state_{false};
  std::uint64_t last_timestamp_ns_{0};
  // A mark alone is biased by the operator's keying weight: at weight w a dit
  // lasts w*dot while the element gap after it lasts (2-w)*dot. The pair
  // therefore sums to a weight-independent (n+1)*dot, so the element-length
  // estimate is deferred until the following gap closes.
  double pending_pair_mark_dots_{0.0};
  double pending_pair_mark_duration_ms_{0.0};
  float pending_pair_confidence_{0.0F};
  bool pending_pair_valid_{false};
  double dot_ms_{60.0};
  float last_snr_db_{0.0F};
  float key_down_probability_{0.0F};
  float confidence_{0.0F};
  float element_confidence_sum_{0.0F};
  // Pure element-duration-ratio precision, deliberately excluding the
  // amplitude/keying-probability (mark_confidence) component folded into
  // element_confidence_sum_/confidence_ above, so it measures cadence
  // precision independently of SNR-driven
  // character confidence, rather than duplicating it.
  float timing_confidence_sum_{0.0F};
  float mark_probability_sum_{0.0F};
  double mark_probability_duration_ms_{0.0};
  std::uint32_t decoded_symbol_count_{0};
  std::uint32_t unknown_symbol_count_{0};
  std::uint32_t key_transition_count_{0};
  std::uint32_t cadence_observation_count_{0};
  std::uint8_t element_count_{0};
  std::vector<CwCharacterEvidence> characters_;
  std::vector<float> recent_cadence_quality_;
  CwCharacterEvidence provisional_character_;
  bool initialized_{false};
  bool key_down_{false};
  bool character_finished_{false};
  bool word_space_emitted_{false};
};

struct CwMultiSpeedConfig {
  double preferred_wpm{20.0};
  double minimum_acquisition_ms{2'500.0};
  double reacquire_after_silence_ms{2'500.0};
  std::uint8_t lock_after_symbols{2};
  float lock_score_margin{0.10F};
  double lattice_checkpoint_ms{500.0};
  double lattice_competitive_cost_margin{1.0};
  float minimum_lattice_evidence_confidence{0.40F};
  // Pair each mark with the gap immediately following it when fitting the
  // independent acoustic cadence. Keying weight moves one edge between those
  // two runs, so their sum retains the operator's underlying element length.
  // The unpaired fit remains available for paired benchmark comparisons.
  bool paired_cadence_fit{true};
};

class CwMultiSpeedDecoder {
 public:
  explicit CwMultiSpeedDecoder(CwDecoderConfig decoder_config = {},
                               CwMultiSpeedConfig config = {});
  void reset();
  [[nodiscard]] CwDecoderUpdate process(std::uint64_t timestamp_ns,
                                        float snr_db);
  // Drains a disappearing channel once without asserting that an operator
  // turn ended. A spectrum association can vanish during an ordinary slow
  // word gap, so semantic completion is deferred until resume proves that
  // the configured sustained-silence interval elapsed.
  [[nodiscard]] CwDecoderUpdate suspendInput(std::uint64_t timestamp_ns);
  [[nodiscard]] CwDecoderUpdate resumeInput(std::uint64_t timestamp_ns);
  [[nodiscard]] CwDecoderUpdate flush(std::uint64_t timestamp_ns);
  [[nodiscard]] std::size_t hypothesisCount() const noexcept;
  [[nodiscard]] std::size_t stateBytes() const noexcept;
  // Rebuilds every speed hypothesis around a different keying technique. The
  // partial state of one technique means nothing to another, so this restarts
  // decoding; it is a no-op when the model is already the one in use.
  void setKeyingModel(CwKeyingModel model);
  // Every hypothesis shares one evidence configuration, so the calibration is
  // common to all of them and a detector need encode its ratio only once.
  [[nodiscard]] float evidenceForLogLikelihoodRatio(
      float log_likelihood_ratio) const noexcept;
  // Cadence used by filter/recovery control. Kept on the original independent
  // mark/gap fit so the paired manual-keying measurement cannot silently
  // perturb established decoding or publication behavior.
  [[nodiscard]] double timingControlWpm() const noexcept {
    return lattice_cadence_dot_ms_ > 0.0
        ? 1'200.0 / lattice_cadence_dot_ms_ : 0.0;
  }
  [[nodiscard]] float timingControlCadenceConfidence() const noexcept {
    return lattice_cadence_confidence_;
  }

 private:
  struct Hypothesis {
    Hypothesis(double speed_wpm, CwDecoderConfig config);

    double seed_wpm;
    CwTimingDecoder decoder;
  };

  [[nodiscard]] float score(const Hypothesis& hypothesis) const noexcept;
  [[nodiscard]] std::size_t selectLeader(float* margin = nullptr) const;
  [[nodiscard]] CwDecoderUpdate snapshot(bool changed) const;
  void considerLock(float margin);
  void resetHypotheses();
  void observeLeaderEvidence(std::uint64_t timestamp_ns);
  void observeCadence(bool key_down, std::uint64_t timestamp_ns);
  void recomputeCadenceEstimate();
  void observeLattice(bool key_down, float key_down_probability,
                      std::uint64_t timestamp_ns);
  void refreshLattice(CwLatticeDecodeMode mode);
  void resetLatticeSegment() noexcept;
  void updateCurrentSender();
  void commitCompletedTransmission(std::size_t final_leader);
  void beginNextTransmissionWithoutAcousticReset();
  void completeTransmission(std::size_t final_leader);
  void rememberSenderCadence(std::string_view sender, double wpm,
                             float confidence);
  [[nodiscard]] const CwSenderCadence* senderCadence(
      std::string_view sender) const noexcept;

  CwDecoderConfig decoder_config_;
  CwMultiSpeedConfig config_;
  std::vector<Hypothesis> hypotheses_;
  std::size_t leader_index_{0};
  std::size_t locked_index_{0};
  std::uint64_t first_timestamp_ns_{0};
  std::uint64_t last_signal_timestamp_ns_{0};
  std::string committed_prefix_;
  CwEventLattice event_lattice_;
  std::string refined_text_;
  std::vector<CwAcousticAlternative> acoustic_alternatives_;
  std::string contextual_lattice_text_;
  static constexpr std::size_t kMaximumTransmissionTurns = 16;
  static constexpr std::size_t kMaximumSenderCadences = 8;
  std::vector<CwTransmissionTurn> transmissions_;
  std::vector<CwSenderCadence> sender_cadences_;
  std::uint64_t next_transmission_sequence_{1};
  std::uint64_t active_transmission_sequence_{1};
  std::array<std::size_t, 9> transmission_primary_starts_{};
  std::size_t transmission_refined_start_{0};
  std::string current_sender_callsign_;
  double current_sender_wpm_{0.0};
  bool active_transmission_completed_{false};
  std::uint64_t lattice_state_started_ns_{0};
  std::uint64_t lattice_last_timestamp_ns_{0};
  std::uint64_t lattice_last_decode_ns_{0};
  std::uint64_t lattice_committed_observation_id_{0};
  double lattice_confidence_sum_{0.0};
  double lattice_confidence_duration_ms_{0.0};
  bool lattice_initialized_{false};
  bool lattice_key_down_{false};
  static constexpr std::size_t kCadenceDurationWindow = 64;
  std::array<double, kCadenceDurationWindow> recent_mark_ms_{};
  std::array<double, kCadenceDurationWindow> recent_gap_ms_{};
  std::array<double, kCadenceDurationWindow> recent_mark_gap_ms_{};
  std::size_t recent_mark_count_{0};
  std::size_t recent_gap_count_{0};
  std::size_t recent_mark_index_{0};
  std::size_t recent_gap_index_{0};
  std::size_t recent_mark_gap_count_{0};
  std::size_t recent_mark_gap_index_{0};
  double pending_cadence_mark_ms_{0.0};
  bool pending_cadence_mark_{false};
  std::uint64_t cadence_state_started_ns_{0};
  double cadence_dot_ms_{0.0};
  float cadence_confidence_{0.0F};
  // The paired estimate above is used for operator/sender WPM. Decoder
  // lattice selection retains the independently scored mark/gap estimate so
  // adding manual-weight compensation cannot alter established text or
  // callsign publication behavior without a separate measured change.
  double lattice_cadence_dot_ms_{0.0};
  float lattice_cadence_confidence_{0.0F};
  bool cadence_initialized_{false};
  // The nine speed anchors segment independently, so they do not all reach the
  // same instant at the same time and the presentation leader can change to one
  // that is further behind. Cadence and the lattice would then be handed a run
  // that starts before the one they last saw, which reads as time running
  // backwards and throws away the estimate. Observations are taken strictly
  // forwards instead: a new leader resumes where the stream had reached.
  std::uint64_t last_observed_segment_ns_{0};
  bool cadence_key_down_{false};
  bool locked_{false};
  bool initialized_{false};
  bool signal_seen_{false};
};

}  // namespace cwassistant::core
