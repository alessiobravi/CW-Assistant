#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace cwassistant::core {

// What a segment of the keyed envelope is, in Morse's own terms.
enum class CwSegmentKind : std::uint8_t {
  Dot,
  Dash,
  ElementGap,
  CharacterGap,
  WordGap,
};

[[nodiscard]] constexpr bool isMarkSegment(const CwSegmentKind kind) noexcept {
  return kind == CwSegmentKind::Dot || kind == CwSegmentKind::Dash;
}

struct CwSegment {
  CwSegmentKind kind{CwSegmentKind::ElementGap};
  std::uint64_t started_ns{0};
  std::uint64_t ended_ns{0};
  double duration_ms{0.0};
  // Mean per-frame log-likelihood in favour of this segment's key state, in
  // nats. Positive means the envelope agreed with the decision.
  double evidence_nats{0.0};
  // The same evidence accumulated over one element's worth of frames: how sure
  // the decoder is that the run is what it says, rather than how sure it is
  // about any single frame of it.
  double element_evidence_nats{0.0};
  float confidence{0.0F};
};

// Which technique decides where the key goes down and comes back up.
//
// The distinction is not accuracy in the abstract, it is which sender the
// technique suits. Measured on the keying-style bench, the threshold is far
// steadier under timing jitter -- hand and bug sending -- while the duration
// model is better when the sender is systematically off the textbook ratios,
// which is what weighted keying and Farnsworth spacing are.
enum class CwKeyingModel : std::uint8_t {
  // Per-frame hysteresis on the keying likelihood, elements classified
  // afterwards by duration ratio. The shipped path.
  AdaptiveThreshold,
  // Explicit duration model over dot, dash and the three gap lengths.
  SemiMarkov,
};

// A technique that turns a stream of per-frame keying likelihoods into runs
// with their lengths and kinds already decided.
//
// Everything downstream of a segmenter -- element assembly, the event lattice,
// the verification gate, callsign policy -- consumes only `CwSegment`, so a new
// technique is a new implementation of this interface and an entry in
// `CwKeyingModel`, not another path through the decoder. A two-state Viterbi, a
// CTC key-probability model and a matched filter all fit here unchanged.
class CwKeyingSegmenter {
 public:
  virtual ~CwKeyingSegmenter() = default;

  virtual void reset() noexcept = 0;
  // Feeds one frame of calibrated log-likelihood in nats, positive for
  // key-down, and returns whatever later evidence can no longer revise.
  [[nodiscard]] virtual std::vector<CwSegment> process(
      std::uint64_t timestamp_ns, double log_likelihood_nats) = 0;
  // Commits everything still open, including any segment in progress.
  [[nodiscard]] virtual std::vector<CwSegment> flush() = 0;

  virtual void setElementLengthMs(double dot_ms) noexcept = 0;
  // How well the runs this segmenter reported matched the element length it
  // was told to expect. Speed hypotheses are compared on this, so a technique
  // that cannot report it should return zero and rely on the other evidence.
  [[nodiscard]] virtual double segmentationScore() const noexcept = 0;
  [[nodiscard]] virtual std::size_t stateBytes() const noexcept = 0;
};

// Stable identifiers, so a stored preference survives a model being added or
// the list being reordered. A new technique adds a name here and nowhere else.
[[nodiscard]] constexpr std::string_view cwKeyingModelName(
    const CwKeyingModel model) noexcept {
  switch (model) {
    case CwKeyingModel::SemiMarkov: return "semi-markov";
    case CwKeyingModel::AdaptiveThreshold: break;
  }
  return "adaptive-threshold";
}

[[nodiscard]] constexpr CwKeyingModel cwKeyingModelFromName(
    const std::string_view name,
    const CwKeyingModel fallback =
        CwKeyingModel::AdaptiveThreshold) noexcept {
  if (name == cwKeyingModelName(CwKeyingModel::SemiMarkov))
    return CwKeyingModel::SemiMarkov;
  if (name == cwKeyingModelName(CwKeyingModel::AdaptiveThreshold))
    return CwKeyingModel::AdaptiveThreshold;
  return fallback;
}

// How much per-frame log-likelihood a model can use. A per-frame threshold
// gains nothing past the point where the posterior is already nearly certain,
// and is only destabilised by letting one confident sample run further; a model
// that integrates evidence across a whole run gains a great deal, because the
// difference between a frame that is probably keyed and one that certainly is
// becomes most of the signal once thirty of them are added together. Measured,
// widening this for the duration model moved capture recovery from 7/9 to 8/9.
[[nodiscard]] constexpr float cwEvidenceBoundNats(
    const CwKeyingModel model) noexcept {
  return model == CwKeyingModel::AdaptiveThreshold ? 3.0F : 8.0F;
}

}  // namespace cwassistant::core
