#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cwassistant/core/cw_keying_segmenter.hpp"

namespace cwassistant::core {

struct CwSemiMarkovConfig {
  double initial_wpm{20.0};
  // Log-normal spread of each duration class, in natural log units. Marks and
  // element gaps are machine-timed by the sender's keyer and stay tight;
  // character and word gaps are where a human hesitates, and Farnsworth
  // spacing stretches them without touching element length, so they are given
  // room to move independently.
  double mark_sigma{0.20};
  double element_gap_sigma{0.25};
  double character_gap_sigma{0.35};
  double word_gap_sigma{0.60};
  // Added once per segment. Negative discourages fragmenting a run into
  // several segments; positive discourages merging distinct ones.
  double segment_log_prior{0.0};
  // A segment may not be committed until this much later evidence exists, so
  // that a decision is never made on a partially observed run.
  double commit_lag_dots{5.0};
  // Sized from the speed being tracked when left at zero: the window only has
  // to hold the longest describable gap plus the depth the search looks back,
  // and a 60 WPM anchor needs a small fraction of what an 8 WPM anchor does.
  // Nine anchors on every track make this the decoder's whole memory cost.
  std::size_t maximum_frames{0};
};

// Duration-explicit (semi-Markov) segmentation of a keyed envelope.
//
// Frames arrive as calibrated log-likelihood ratios in nats -- positive means
// the envelope favours key-down. The decoder maintains a Viterbi lattice over
// (boundary, segment kind) in which a transition *is* a whole segment of
// explicit length, scored as the accumulated evidence across it plus the log
// density of its duration under that kind. Because every competing
// segmentation covers the same frames, their scores are directly comparable.
//
// This is what a two-state model cannot do. Giving key-down its own state and
// letting it self-loop makes its length geometric -- mode at zero, no
// characteristic scale -- so such a model can only be more or less reluctant
// to switch. Here a dash is a state whose length distribution is centred on
// three elements, and evidence that would leave a threshold decoder undecided
// is resolved by the length the run would have to have.
class CwSemiMarkovSegmenter final : public CwKeyingSegmenter {
 public:
  explicit CwSemiMarkovSegmenter(CwSemiMarkovConfig config = {});

  void reset() noexcept override;

  // Timestamps must not go backwards.
  [[nodiscard]] std::vector<CwSegment> process(
      std::uint64_t timestamp_ns, double log_likelihood_nats) override;
  [[nodiscard]] std::vector<CwSegment> flush() override;

  void setElementLengthMs(double dot_ms) noexcept override;
  // Mean log-likelihood per frame of the segmentation this decoder committed:
  // the evidence it accumulated, plus how well each run's length matched the
  // class it was given. This is how one speed hypothesis is told from another.
  // Neither the text nor the duration fit alone can do it -- Morse timing is
  // self-similar at a factor of three, so a decoder set three times too fast
  // reads every dash as a dot and every character gap as an element gap, and
  // the result is legal, entirely known symbols that fit their own model
  // perfectly. What such a hypothesis cannot do is stay cheap: a real dash is
  // longer than the longest mark it can describe, so it has to break solid
  // marks apart to make the interval tile at all, and it pays for every
  // invented gap in evidence that contradicts it.
  [[nodiscard]] double segmentationScore() const noexcept override {
    return segmentation_score_;
  }
  [[nodiscard]] double elementLengthMs() const noexcept { return dot_ms_; }
  [[nodiscard]] std::size_t stateBytes() const noexcept override;

 private:
  static constexpr std::size_t kStateCount = 5;

  struct Cell {
    double score{0.0};
    std::uint8_t previous{0};
    std::uint32_t length{0};
  };

  void appendFrame(std::uint64_t timestamp_ns, double log_likelihood_nats);
  void computeBoundary(std::uint64_t boundary);
  void extendLattice();
  void pin(std::uint64_t boundary, std::size_t state);
  [[nodiscard]] std::vector<CwSegment> harvest(bool flushing);
  void retire();
  void rebase();
  [[nodiscard]] std::size_t slot(std::uint64_t boundary) const noexcept;

  CwSemiMarkovConfig config_;
  double dot_ms_{60.0};

  // Ring-buffered frame evidence. `prefix_` holds the running sum of the
  // per-frame log-likelihood so any segment's accumulated evidence is one
  // subtraction, whatever its length.
  std::vector<double> prefix_;
  std::vector<std::uint64_t> boundary_ns_;
  std::vector<Cell> cells_;
  // Best score reaching a boundary given the segment that starts there, with
  // the transition prior already applied. Folding the predecessor search into
  // the boundary keeps the duration loop free of an inner state loop.
  std::vector<double> entry_;
  std::vector<std::uint8_t> entry_state_;

  std::uint64_t base_boundary_{0};
  std::uint64_t head_boundary_{0};
  // Everything up to here has been handed to the caller. The lattice is free
  // to keep revising behind it -- reporting is gated on the commit lag, not on
  // freezing the path -- but a segment is never described twice.
  std::uint64_t reported_boundary_{0};
  bool initialized_{false};
  std::uint64_t frames_since_harvest_{0};
  double segmentation_score_{0.0};
  std::uint64_t scored_frames_{0};
};

}  // namespace cwassistant::core
