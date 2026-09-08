#include "cwassistant/core/cw_semi_markov_segmenter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cwassistant::core {
namespace {

constexpr double kNegativeInfinity = -std::numeric_limits<double>::infinity();
constexpr double kLogTwoPi = 1.837'877'066'409'345;

struct StateSpec {
  double units;
  double minimum_units;
  double maximum_units;
  bool mark;
};

// Nominal lengths are Morse's own: one element for a dot and an intra-element
// gap, three for a dash and a character gap, seven between words. The search
// ranges around them overlap deliberately -- a gap of three and a half
// elements really is ambiguous between character and word spacing, and the
// decoder should resolve that from the sender's other spacing rather than
// from a boundary drawn through the middle of it.
constexpr std::array<StateSpec, 5> kStates{{
    {1.0, 0.35, 2.20, true},    // Dot
    {3.0, 1.70, 5.50, true},    // Dash
    {1.0, 0.35, 2.20, false},   // ElementGap
    {3.0, 1.70, 5.50, false},   // CharacterGap
    {7.0, 3.80, 14.00, false},  // WordGap
}};

constexpr std::size_t kDot = 0;
constexpr std::size_t kDash = 1;
constexpr std::size_t kElementGap = 2;
constexpr std::size_t kCharacterGap = 3;
constexpr std::size_t kWordGap = 4;

// A mark is always followed by a gap and a gap by a mark: two adjacent marks
// are one longer mark, and the decoder must not be able to describe the same
// envelope two ways. Within that, the priors are weak -- an element gap is the
// commonest thing to follow a mark, but the duration term decides.
constexpr double logPrior(const double probability) {
  // std::log is not constexpr everywhere; these are the values needed.
  return probability == 0.60   ? -0.510'825'623'765'991
         : probability == 0.28 ? -1.272'965'676'349'160
         : probability == 0.12 ? -2.120'263'536'200'091
         : probability == 0.55 ? -0.597'837'000'755'620
         : probability == 0.45 ? -0.798'507'696'217'772
         : probability == 0.40 ? -0.916'290'731'874'155
         : probability == 0.32 ? -1.139'434'283'188'365
                               : -1.272'965'676'349'160;  // 0.28
}

constexpr std::array<std::array<double, 5>, 5> kTransition{{
    // from Dot
    {{kNegativeInfinity, kNegativeInfinity, logPrior(0.60), logPrior(0.28),
      logPrior(0.12)}},
    // from Dash
    {{kNegativeInfinity, kNegativeInfinity, logPrior(0.60), logPrior(0.28),
      logPrior(0.12)}},
    // from ElementGap
    {{logPrior(0.55), logPrior(0.45), kNegativeInfinity, kNegativeInfinity,
      kNegativeInfinity}},
    // from CharacterGap
    {{logPrior(0.55), logPrior(0.45), kNegativeInfinity, kNegativeInfinity,
      kNegativeInfinity}},
    // from WordGap. Silence between transmissions is unbounded and has no
    // characteristic length, so it is described as a run of word gaps rather
    // than by stretching one. Without this a silence longer than the widest
    // single gap has no legal parse at all, and the search is forced to invent
    // marks inside it purely to make the interval tile -- on real air, where
    // gaps between transmissions are seconds long, that is the difference
    // between a quiet channel and a channel full of phantom text.
    {{logPrior(0.40), logPrior(0.32), kNegativeInfinity, kNegativeInfinity,
      logPrior(0.28)}},
}};

double sigmaFor(const CwSemiMarkovConfig& config, const std::size_t state) {
  switch (state) {
    case kDot:
    case kDash: return config.mark_sigma;
    case kElementGap: return config.element_gap_sigma;
    case kCharacterGap: return config.character_gap_sigma;
    default: return config.word_gap_sigma;
  }
}

// Log density of a duration under its class. Log-normal rather than Gaussian
// because keying error is proportional: a 10% long dash at 40 WPM and at 10
// WPM are the same mistake, and a duration can never be negative.
double logDurationDensity(const double duration_ms, const double nominal_ms,
                          const double sigma) {
  if (!(duration_ms > 0.0) || !(nominal_ms > 0.0)) return kNegativeInfinity;
  const double z = std::log(duration_ms / nominal_ms) / sigma;
  return -0.5 * z * z - std::log(duration_ms) - std::log(sigma) -
         0.5 * kLogTwoPi;
}

}  // namespace

CwSemiMarkovSegmenter::CwSemiMarkovSegmenter(CwSemiMarkovConfig config)
    : config_(config) {
  dot_ms_ = 1'200.0 / std::max(1.0, config_.initial_wpm);
  if (config_.maximum_frames == 0) {
    // The widest word gap, the commit lag behind it, and a margin, at the
    // channel bank's evidence rate.
    const double needed =
        (kStates[kWordGap].maximum_units + config_.commit_lag_dots + 8.0) *
        dot_ms_ / 2.0;
    config_.maximum_frames = static_cast<std::size_t>(
        std::clamp(needed, 512.0, 2'048.0));
  }
  config_.maximum_frames = std::max<std::size_t>(config_.maximum_frames, 512);
  const std::size_t capacity = config_.maximum_frames;
  prefix_.assign(capacity, 0.0);
  boundary_ns_.assign(capacity, 0);
  cells_.assign(capacity * kStateCount, Cell{});
  entry_.assign(capacity * kStateCount, kNegativeInfinity);
  entry_state_.assign(capacity * kStateCount, 0);
  reset();
}

void CwSemiMarkovSegmenter::reset() noexcept {
  base_boundary_ = 0;
  head_boundary_ = 0;
  reported_boundary_ = 0;
  initialized_ = false;
  frames_since_harvest_ = 0;
  segmentation_score_ = 0.0;
  scored_frames_ = 0;

  std::ranges::fill(prefix_, 0.0);
  std::ranges::fill(boundary_ns_, std::uint64_t{0});
}

void CwSemiMarkovSegmenter::setElementLengthMs(const double dot_ms) noexcept {
  if (dot_ms >= 8.0 && dot_ms <= 400.0) dot_ms_ = dot_ms;
}

std::size_t CwSemiMarkovSegmenter::stateBytes() const noexcept {
  return sizeof(*this) + prefix_.size() * sizeof(double) +
         boundary_ns_.size() * sizeof(std::uint64_t) +
         cells_.size() * sizeof(Cell) + entry_.size() * sizeof(double) +
         entry_state_.size();
}

std::size_t CwSemiMarkovSegmenter::slot(const std::uint64_t boundary) const noexcept {
  return static_cast<std::size_t>(boundary % prefix_.size());
}

void CwSemiMarkovSegmenter::appendFrame(const std::uint64_t timestamp_ns,
                                   const double log_likelihood_nats) {
  const double bounded = std::clamp(log_likelihood_nats, -20.0, 20.0);
  const std::size_t previous = slot(head_boundary_);
  ++head_boundary_;
  const std::size_t current = slot(head_boundary_);
  prefix_[current] = prefix_[previous] + bounded;
  boundary_ns_[current] = timestamp_ns;
}

void CwSemiMarkovSegmenter::computeBoundary(const std::uint64_t b) {
  const std::size_t span = static_cast<std::size_t>(b - base_boundary_);
  if (span == 0) return;
  // Frames arrive on a fixed hop; measuring it rather than assuming it keeps
  // the duration search correct if the hop ever changes.
  const double window_ms =
      static_cast<double>(boundary_ns_[slot(b)] -
                          boundary_ns_[slot(base_boundary_)]) /
      1.0e6;
  const double frame_ms = window_ms / static_cast<double>(span);
  if (!(frame_ms > 0.0)) return;

  const std::size_t at = slot(b) * kStateCount;
  for (std::size_t s = 0; s < kStateCount; ++s) {
    const StateSpec& spec = kStates[s];
    const double nominal_ms = spec.units * dot_ms_;
    const double sigma = sigmaFor(config_, s);
    auto frames_for = [&](const double units) {
      return static_cast<std::size_t>(
          std::lround(units * dot_ms_ / frame_ms));
    };
    const std::size_t shortest =
        std::max<std::size_t>(1, frames_for(spec.minimum_units));
    const std::size_t longest =
        std::min<std::size_t>(frames_for(spec.maximum_units), span);
    Cell best{kNegativeInfinity, 0, 0};
    if (shortest <= longest) {
      // Every length in the range is tried. Sampling the range more coarsely
      // looks affordable -- the spacing stays well inside the duration spread
      // -- but it quantises where a segment may *end*, and the segments have
      // to tile the interval exactly. When no combination of the permitted
      // lengths reaches a true edge, the cheapest repair the search can find
      // is a minimum-length element inserted into silence. Measured, coarse
      // sampling cost 0.098 mean character error against 0.060 here, all of it
      // from invented elements at edges.
      for (std::size_t d = shortest; d <= longest; ++d) {
        const std::uint64_t a = b - d;
        const std::size_t from = slot(a);
        const double reached = entry_[from * kStateCount + s];
        if (reached == kNegativeInfinity) continue;
        const double accumulated = prefix_[slot(b)] - prefix_[from];
        const double evidence = spec.mark ? accumulated : -accumulated;
        const double duration_ms =
            static_cast<double>(boundary_ns_[slot(b)] - boundary_ns_[from]) /
            1.0e6;
        const double score = reached + evidence + config_.segment_log_prior +
                             logDurationDensity(duration_ms, nominal_ms, sigma);
        if (score > best.score) {
          best.score = score;
          best.previous = entry_state_[from * kStateCount + s];
          best.length = static_cast<std::uint32_t>(d);
        }
      }
    }
    cells_[at + s] = best;
  }

  for (std::size_t s = 0; s < kStateCount; ++s) {
    double best = kNegativeInfinity;
    std::uint8_t best_previous = 0;
    for (std::size_t p = 0; p < kStateCount; ++p) {
      const double transition = kTransition[p][s];
      if (transition == kNegativeInfinity) continue;
      const double candidate = cells_[at + p].score + transition;
      if (candidate > best) {
        best = candidate;
        best_previous = static_cast<std::uint8_t>(p);
      }
    }
    entry_[at + s] = best;
    entry_state_[at + s] = best_previous;
  }
}

void CwSemiMarkovSegmenter::extendLattice() { computeBoundary(head_boundary_); }

void CwSemiMarkovSegmenter::pin(const std::uint64_t boundary,
                           const std::size_t state) {
  // A merge point is a node every surviving path already crosses, so fixing
  // the path there removes nothing still in contention -- but it has to be
  // fixed in two senses, and the second is easy to miss. Ruling out the other
  // states at this boundary is not enough on its own: a segment may still be
  // proposed that starts before the boundary and ends after it, stepping over
  // the decision entirely and leaving a hole where the reported stream should
  // have continued. Making the pin the new window origin is what forbids that,
  // because no segment can begin before the origin.
  const std::size_t at = slot(boundary) * kStateCount;
  for (std::size_t s = 0; s < kStateCount; ++s) {
    if (s == state) continue;
    cells_[at + s] = Cell{kNegativeInfinity, 0, 0};
  }
  const double survivor = cells_[at + state].score;
  for (std::size_t s = 0; s < kStateCount; ++s) {
    const double transition = kTransition[state][s];
    entry_[at + s] =
        std::isfinite(transition) ? survivor + transition : kNegativeInfinity;
    entry_state_[at + s] = static_cast<std::uint8_t>(state);
  }
  if (boundary <= base_boundary_) return;
  base_boundary_ = boundary;
  // Scores ahead of the pin were reached before it was known, and some of
  // them came through routes it has now closed. They are re-derived rather
  // than re-seeded: every score in the window stays measured from the same
  // origin, so nothing freshly zeroed can sit beside an older accumulation
  // and win on scale alone.
  for (std::uint64_t b = boundary + 1; b <= head_boundary_; ++b) {
    computeBoundary(b);
  }
}

std::vector<CwSegment> CwSemiMarkovSegmenter::harvest(const bool flushing) {
  std::vector<CwSegment> reported;
  const std::uint64_t head = head_boundary_;
  if (head <= base_boundary_) return reported;

  // Where the survivors agree, the past is decided. Tracing back from only the
  // currently best state would report a path that the next frame can still
  // re-route, which is what makes a segment appear to move after it has been
  // described. Tracing back from every surviving state and reporting only
  // their common prefix reports a boundary exactly once, when no future
  // evidence can move it.
  struct Walk {
    std::uint64_t cursor;
    std::size_t state;
    bool active;
  };
  std::array<Walk, kStateCount> walks{};
  std::size_t active = 0;
  for (std::size_t s = 0; s < kStateCount; ++s) {
    const Cell& cell = cells_[slot(head) * kStateCount + s];
    const bool usable = std::isfinite(cell.score) && cell.length != 0;
    walks[s] = {head, s, usable};
    if (usable) ++active;
  }
  if (active == 0) return reported;

  std::uint64_t merge_boundary = 0;
  std::size_t merge_state = 0;
  bool merged = false;
  // A flush has no future to wait for, so the best surviving path stands.
  if (flushing) {
    double best = kNegativeInfinity;
    for (std::size_t s = 0; s < kStateCount; ++s) {
      if (!walks[s].active) continue;
      const double score = cells_[slot(head) * kStateCount + s].score;
      if (score > best) { best = score; merge_state = s; }
    }
    merge_boundary = head;
    merged = true;
  }
  while (!merged) {
    std::uint64_t furthest = base_boundary_;
    for (const Walk& walk : walks) {
      if (walk.active && walk.cursor > furthest) furthest = walk.cursor;
    }
    if (furthest <= base_boundary_) break;
    bool aligned = true;
    std::size_t common = kStateCount;
    for (const Walk& walk : walks) {
      if (!walk.active) continue;
      if (walk.cursor != furthest) { aligned = false; break; }
      if (common == kStateCount) common = walk.state;
      else if (common != walk.state) { aligned = false; break; }
    }
    if (aligned && common != kStateCount && furthest < head) {
      merge_boundary = furthest;
      merge_state = common;
      merged = true;
      break;
    }
    for (Walk& walk : walks) {
      if (!walk.active || walk.cursor != furthest) continue;
      const Cell& cell = cells_[slot(walk.cursor) * kStateCount + walk.state];
      if (cell.length == 0 || walk.cursor - cell.length < base_boundary_) {
        walk.active = false;
        continue;
      }
      walk.cursor -= cell.length;
      walk.state = cell.previous;
    }
    bool any = false;
    for (const Walk& walk : walks) any = any || walk.active;
    if (!any) break;
  }

  std::uint64_t truncated_tail_start = 0;
  std::uint64_t truncated_tail_end = 0;
  if (!merged) {
    // Agreement is the right reason to commit, but it cannot be the only one.
    // A long silence can be tiled by several runs of word gaps that are all
    // equally good, so the survivors genuinely never agree on where one ends
    // -- and while that stands, the character before the silence is never
    // closed either. Past the commit lag the difference no longer matters:
    // the best path is taken, and the transmission that just ended is
    // reported instead of being held for an agreement that will not come.
    const double window_ms =
        static_cast<double>(boundary_ns_[slot(head)] -
                            boundary_ns_[slot(base_boundary_)]) /
        1.0e6;
    const double frame_ms =
        window_ms / static_cast<double>(head - base_boundary_);
    const auto lag_frames =
        frame_ms > 0.0 ? static_cast<std::uint64_t>(std::lround(
                             config_.commit_lag_dots * dot_ms_ / frame_ms))
                       : 0;
    if (head > lag_frames && head - lag_frames > reported_boundary_) {
      const std::uint64_t forced = head - lag_frames;
      double best_tail = kNegativeInfinity;
      std::size_t tail = kStateCount;
      for (std::size_t s = 0; s < kStateCount; ++s) {
        const double score = cells_[slot(head) * kStateCount + s].score;
        if (score > best_tail) { best_tail = score; tail = s; }
      }
      std::uint64_t cursor = head;
      std::size_t state = tail;
      while (tail != kStateCount && cursor > base_boundary_) {
        const Cell& cell = cells_[slot(cursor) * kStateCount + state];
        if (cell.length == 0) break;
        const std::uint64_t start = cursor - cell.length;
        if (cursor <= forced) {
          merge_boundary = cursor;
          merge_state = state;
          merged = true;
          break;
        }
        // A transmission ends in silence that runs to the edge of what has
        // been heard, so waiting for that silence to finish before describing
        // it means the last character of the last call is never closed -- and
        // that is the character an operator most needs. Once the silence is
        // already longer than any character gap could be, how much longer it
        // runs cannot change what has elapsed, and the part that has elapsed
        // is enough to close the character. Only the elapsed part is reported;
        // the rest is left to the search, and the gap in the reported stream
        // that leaves costs nothing, because the key does not change across it.
        if (state == kWordGap && start <= forced &&
            forced - start >= static_cast<std::uint64_t>(std::lround(
                                  kStates[kCharacterGap].maximum_units *
                                  dot_ms_ / frame_ms))) {
          merge_boundary = start;
          merge_state = cell.previous;
          merged = true;
          truncated_tail_start = start;
          truncated_tail_end = forced;
          break;
        }
        state = cell.previous;
        cursor = start;
      }
    }
  }
  if (!merged) return reported;

  struct Traced {
    std::size_t state;
    std::uint64_t start;
    std::uint64_t end;
  };
  std::vector<Traced> path;
  std::uint64_t cursor = merge_boundary;
  std::size_t state = merge_state;
  while (cursor > reported_boundary_) {
    const Cell& cell = cells_[slot(cursor) * kStateCount + state];
    if (cell.length == 0) break;
    const std::uint64_t start = cursor - cell.length;
    if (start < reported_boundary_) break;
    path.push_back({state, start, cursor});
    state = cell.previous;
    cursor = start;
  }
  std::ranges::reverse(path);

  const double window_ms =
      static_cast<double>(boundary_ns_[slot(head)] -
                          boundary_ns_[slot(base_boundary_)]) /
      1.0e6;
  const double frame_ms =
      head > base_boundary_
          ? window_ms / static_cast<double>(head - base_boundary_)
          : 0.0;

  for (const Traced& traced : path) {
    const std::size_t from = slot(traced.start);
    const std::size_t to = slot(traced.end);
    const double accumulated = prefix_[to] - prefix_[from];
    const bool mark = kStates[traced.state].mark;
    const double evidence = mark ? accumulated : -accumulated;
    const double frames = static_cast<double>(traced.end - traced.start);
    const double mean = frames > 0.0 ? evidence / frames : 0.0;
    const double frames_per_element =
        frame_ms > 0.0 ? dot_ms_ / frame_ms : 0.0;
    CwSegment segment;
    segment.kind = static_cast<CwSegmentKind>(traced.state);
    segment.started_ns = boundary_ns_[from];
    segment.ended_ns = boundary_ns_[to];
    segment.duration_ms =
        static_cast<double>(segment.ended_ns - segment.started_ns) / 1.0e6;
    // Only the shape term, without the density's normaliser: the normaliser
    // rewards a model that predicts shorter segments, which would quietly
    // favour the fastest anchor rather than the best-fitting one.
    const StateSpec& spec = kStates[traced.state];
    const double z = std::log(std::max(segment.duration_ms, 1.0e-3) /
                              (spec.units * dot_ms_)) /
                     sigmaFor(config_, traced.state);
    ++scored_frames_;
    segmentation_score_ += (-0.5 * z * z - segmentation_score_) /
                           static_cast<double>(std::min<std::uint64_t>(
                               scored_frames_, 24));
    segment.evidence_nats = mean;
    segment.element_evidence_nats = mean * frames_per_element;
    segment.confidence = static_cast<float>(
        1.0 / (1.0 + std::exp(-std::clamp(segment.element_evidence_nats,
                                          -20.0, 20.0))));
    reported.push_back(segment);
    reported_boundary_ = traced.end;
  }
  if (truncated_tail_end > truncated_tail_start &&
      truncated_tail_start >= reported_boundary_) {
    const std::size_t from = slot(truncated_tail_start);
    const std::size_t to = slot(truncated_tail_end);
    const double frames =
        static_cast<double>(truncated_tail_end - truncated_tail_start);
    const double mean =
        frames > 0.0 ? -(prefix_[to] - prefix_[from]) / frames : 0.0;
    CwSegment segment;
    segment.kind = CwSegmentKind::WordGap;
    segment.started_ns = boundary_ns_[from];
    segment.ended_ns = boundary_ns_[to];
    segment.duration_ms =
        static_cast<double>(segment.ended_ns - segment.started_ns) / 1.0e6;
    segment.evidence_nats = mean;
    segment.element_evidence_nats =
        mean * (frame_ms > 0.0 ? dot_ms_ / frame_ms : 0.0);
    segment.confidence = static_cast<float>(
        1.0 / (1.0 + std::exp(-std::clamp(segment.element_evidence_nats,
                                          -20.0, 20.0))));
    reported.push_back(segment);
    reported_boundary_ = truncated_tail_end;
    // Deliberately not pinned. The silence is reported as far as it has run,
    // but its class is not fixed here: pinning a word gap part way through
    // writes a word boundary the evidence has not yet asked for, and measured
    // that cost 0.499 mean character error against 0.351.
    return reported;
  }
  if (!flushing && !path.empty()) pin(merge_boundary, merge_state);
  return reported;
}

void CwSemiMarkovSegmenter::retire() {
  // Scores across the window are all measured from the same origin, so the
  // window may be trimmed but never re-seeded: re-seeding would put freshly
  // zeroed cells alongside cells still carrying the older accumulation, and
  // the stale ones would win and re-describe ground already reported.
  const std::uint64_t capacity =
      static_cast<std::uint64_t>(prefix_.size()) - 8;
  if (head_boundary_ - base_boundary_ <= capacity) return;
  const std::uint64_t trimmed = head_boundary_ - capacity;
  base_boundary_ = std::max(trimmed, base_boundary_);
  // Nothing before the window can still be described, so reporting cannot be
  // left behind it -- on a channel carrying only noise the survivors may never
  // agree and this is the only thing that keeps the two in step.
  if (reported_boundary_ < base_boundary_) reported_boundary_ = base_boundary_;
}

void CwSemiMarkovSegmenter::rebase() {
  // Both the evidence prefix and the path scores accumulate for as long as a
  // stream lives. Only differences are ever used, so the whole live window can
  // be shifted back toward zero before precision becomes a question.
  if (std::abs(prefix_[slot(head_boundary_)]) < 1.0e9) return;
  const double evidence_offset = prefix_[slot(base_boundary_)];
  double score_offset = kNegativeInfinity;
  for (std::size_t s = 0; s < kStateCount; ++s) {
    score_offset = std::max(
        score_offset, cells_[slot(head_boundary_) * kStateCount + s].score);
  }
  if (!std::isfinite(score_offset)) score_offset = 0.0;
  for (std::uint64_t b = base_boundary_; b <= head_boundary_; ++b) {
    const std::size_t index = slot(b);
    prefix_[index] -= evidence_offset;
    for (std::size_t s = 0; s < kStateCount; ++s) {
      Cell& cell = cells_[index * kStateCount + s];
      if (std::isfinite(cell.score)) cell.score -= score_offset;
      double& entry = entry_[index * kStateCount + s];
      if (std::isfinite(entry)) entry -= score_offset;
    }
  }
}

std::vector<CwSegment> CwSemiMarkovSegmenter::process(
    const std::uint64_t timestamp_ns, const double log_likelihood_nats) {
  if (!initialized_) {
    initialized_ = true;
    base_boundary_ = 0;
    head_boundary_ = 0;
    reported_boundary_ = 0;
    boundary_ns_[0] = timestamp_ns;
    prefix_[0] = 0.0;
    const std::size_t at = 0;
    for (std::size_t s = 0; s < kStateCount; ++s) {
      cells_[at + s] = Cell{kNegativeInfinity, 0, 0};
      // A stream may open part way through a transmission, so no starting
      // state is excluded; the first committed segment is whichever the
      // evidence supports.
      entry_[at + s] = 0.0;
      entry_state_[at + s] = static_cast<std::uint8_t>(kWordGap);
    }
    return {};
  }
  if (timestamp_ns <= boundary_ns_[slot(head_boundary_)]) return {};

  appendFrame(timestamp_ns, log_likelihood_nats);
  extendLattice();
  retire();
  rebase();
  if (++frames_since_harvest_ < 8) return {};
  frames_since_harvest_ = 0;
  return harvest(false);
}

std::vector<CwSegment> CwSemiMarkovSegmenter::flush() {
  if (!initialized_) return {};
  std::vector<CwSegment> committed = harvest(true);
  reset();
  return committed;
}

}  // namespace cwassistant::core
