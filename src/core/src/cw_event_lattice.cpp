#include "cwassistant/core/cw_event_lattice.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace cwassistant::core {
namespace {

struct MorseEntry {
  std::string_view elements;
  std::string_view symbol;
};

constexpr MorseEntry kMorseTable[]{
    {".-", "A"},       {"-...", "B"},     {"-.-.", "C"},
    {"-..", "D"},      {".", "E"},        {"..-.", "F"},
    {"--.", "G"},      {"....", "H"},     {"..", "I"},
    {".---", "J"},     {"-.-", "K"},      {".-..", "L"},
    {"--", "M"},       {"-.", "N"},       {"---", "O"},
    {".--.", "P"},     {"--.-", "Q"},     {".-.", "R"},
    {"...", "S"},      {"-", "T"},        {"..-", "U"},
    {"...-", "V"},     {".--", "W"},      {"-..-", "X"},
    {"-.--", "Y"},     {"--..", "Z"},     {"-----", "0"},
    {".----", "1"},    {"..---", "2"},    {"...--", "3"},
    {"....-", "4"},    {".....", "5"},    {"-....", "6"},
    {"--...", "7"},    {"---..", "8"},    {"----.", "9"},
    {".-.-.-", "."},   {"--..--", ","},   {"..--..", "?"},
    {".----.", "'"},   {"-.-.--", "!"},   {"-..-.", "/"},
    {"-.--.", "("},    {"-.--.-", ")"},  {".-...", "&"},
    {"---...", ":"},   {"-.-.-.", ";"},  {"-...-", "="},
    {".-.-.", "+"},    {"-....-", "-"},   {"..--.-", "_"},
    {".-..-.", "\""}, {"...-..-", "$"}, {".--.-.", "@"},
    {"...-.-", "<SK>"}, {"...---...", "<SOS>"},
};

std::optional<std::string_view> decodeElements(const std::string_view value) {
  for (const auto& entry : kMorseTable) {
    if (entry.elements == value) return entry.symbol;
  }
  return std::nullopt;
}

double squaredResidual(const double value, const double center,
                       const double scale) noexcept {
  const double normalized = (value - center) / scale;
  return std::min(normalized * normalized, 25.0);
}

double finiteOr(const double value, const double fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

double confidenceScale(const float confidence) noexcept {
  // Poor envelope confidence flattens acoustic distinctions but cannot make
  // any path cheaper than well-observed evidence.
  return 0.10 + 0.90 * static_cast<double>(
      std::clamp(confidence, 0.0F, 1.0F));
}

double uncertaintyCost(const float confidence) noexcept {
  return 0.15 * -std::log(std::max(
      static_cast<double>(std::clamp(confidence, 0.0F, 1.0F)), 0.05));
}

enum class GapKind : std::uint8_t { Element, Character, Word };

double gapCost(const double ratio, const float confidence,
               const GapKind kind, const CwEventLatticeConfig& config,
               const double tolerance_scale) {
  double residual = 0.0;
  switch (kind) {
    case GapKind::Element:
      residual = squaredResidual(ratio, 1.0,
                                 0.45 * tolerance_scale);
      break;
    case GapKind::Character:
      residual = std::min(
          squaredResidual(ratio, config.character_gap_dots,
                          0.90 * tolerance_scale),
          0.12 + squaredResidual(
                     ratio, config.compressed_character_gap_dots,
                     0.45 * tolerance_scale));
      break;
    case GapKind::Word:
      residual = std::min(
          squaredResidual(ratio, config.word_gap_dots,
                          1.50 * tolerance_scale),
          0.35 + squaredResidual(
                     ratio, config.compressed_word_gap_dots,
                     tolerance_scale));
      break;
  }
  return confidenceScale(confidence) * residual;
}

double estimateTimingTolerance(
    const std::vector<CwRunObservation>& observations,
    const std::size_t first_observation, const double dot_ms,
    const CwEventLatticeConfig& config) {
  std::vector<double> residuals;
  residuals.reserve(observations.size() - first_observation);
  std::size_t mark_count = 0;
  std::size_t gap_count = 0;
  for (std::size_t index = first_observation; index < observations.size();
       ++index) {
    const auto& observation = observations[index];
    if (observation.confidence <
        config.minimum_tolerance_observation_confidence) {
      continue;
    }
    const double ratio = observation.duration_ms / dot_ms;
    double residual = 0.0;
    if (observation.keyed) {
      residual = std::min(std::abs(ratio - 1.0) / 0.45,
                          std::abs(ratio - 3.0) / 0.85);
      ++mark_count;
    } else {
      residual = std::min({
          std::abs(ratio - 1.0) / 0.45,
          std::abs(ratio - config.compressed_character_gap_dots) / 0.45,
          std::abs(ratio - config.character_gap_dots) / 0.90,
          std::abs(ratio - config.compressed_word_gap_dots) / 1.00,
          std::abs(ratio - config.word_gap_dots) / 1.50,
      });
      ++gap_count;
    }
    if (std::isfinite(residual)) residuals.push_back(residual);
  }

  if (residuals.size() < config.minimum_tolerance_observations ||
      mark_count < 4U || gap_count < 4U) {
    return config.timing_tolerance_scale;
  }
  std::ranges::sort(residuals);
  const auto quantile = [&residuals](const double fraction) {
    const auto last = residuals.size() - 1U;
    const auto index = static_cast<std::size_t>(
        std::floor(fraction * static_cast<double>(last)));
    return residuals[index];
  };
  const double median = quantile(0.50);
  const double upper_quartile = quantile(0.75);
  // A segment whose typical run does not resemble any Morse timing center is
  // not hand-sent variance. Refuse to widen around noise or a wrong dot fit.
  if (median > 1.25) return config.timing_tolerance_scale;
  const double estimated = 1.0 +
      1.25 * std::max(0.0, upper_quartile - 0.20);
  return std::clamp(std::max(config.timing_tolerance_scale, estimated),
                    config.timing_tolerance_scale,
                    config.maximum_timing_tolerance_scale);
}

struct Path {
  std::vector<CwLatticeSymbol> symbols;
  std::string pending_elements;
  std::uint64_t pending_first_observation_id{0};
  std::uint64_t pending_last_observation_id{0};
  double cost{0.0};
};

void emitSymbol(Path& path, const bool word_boundary,
                const std::uint64_t last_observation_id,
                const bool force_unknown = false) {
  if (path.pending_elements.empty()) return;
  const auto decoded = decodeElements(path.pending_elements);
  path.symbols.push_back({
      .symbol = decoded && !force_unknown ? std::string(*decoded)
                                           : std::string{},
      .elements = path.pending_elements,
      .first_observation_id = path.pending_first_observation_id,
      .last_observation_id = last_observation_id,
      .known = decoded.has_value() && !force_unknown,
      .word_boundary_after = word_boundary,
  });
  path.pending_elements.clear();
}

std::string pathKey(const Path& path) {
  std::string key;
  for (const auto& symbol : path.symbols) {
    if (symbol.known) {
      key += symbol.symbol;
    } else {
      key += "<UNKNOWN:";
      key += symbol.elements;
      key += '>';
    }
    key += '@';
    key += std::to_string(symbol.first_observation_id);
    key += ':';
    key += std::to_string(symbol.last_observation_id);
    key += symbol.word_boundary_after ? ' ' : '|';
  }
  key += '/';
  key += path.pending_elements;
  return key;
}

void prune(std::vector<Path>& paths, const std::size_t beam_width) {
  std::stable_sort(paths.begin(), paths.end(),
                   [](const Path& left, const Path& right) {
                     return left.cost < right.cost;
                   });
  std::vector<Path> unique;
  unique.reserve(std::min(paths.size(), beam_width));
  std::vector<std::string> keys;
  keys.reserve(std::min(paths.size(), beam_width));
  for (auto& path : paths) {
    const std::string key = pathKey(path);
    if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
    keys.push_back(key);
    unique.push_back(std::move(path));
    if (unique.size() >= beam_width) break;
  }
  paths = std::move(unique);
}

}  // namespace

std::string CwLatticeAlternative::text(const char unknown_marker) const {
  std::string result;
  for (const auto& item : symbols) {
    if (item.known) {
      result += item.symbol;
    } else {
      result.push_back(unknown_marker);
    }
    if (item.word_boundary_after &&
        (result.empty() || result.back() != ' ')) {
      result.push_back(' ');
    }
  }
  while (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}

CwEventLattice::CwEventLattice(CwEventLatticeConfig config)
    : config_(config) {
  config_.maximum_observations =
      std::clamp<std::size_t>(config_.maximum_observations, 16U, 512U);
  config_.beam_width = std::clamp<std::size_t>(config_.beam_width, 2U, 64U);
  config_.maximum_alternatives = std::clamp<std::size_t>(
      config_.maximum_alternatives, 1U, config_.beam_width);
  config_.maximum_elements_per_symbol = std::clamp<std::size_t>(
      config_.maximum_elements_per_symbol, 9U, 32U);
  config_.compressed_character_gap_dots = std::clamp(finiteOr(
      config_.compressed_character_gap_dots, 2.0), 1.4, 2.6);
  config_.character_gap_dots = std::clamp(finiteOr(
      config_.character_gap_dots, 3.0),
      config_.compressed_character_gap_dots + 0.2, 4.0);
  config_.compressed_word_gap_dots = std::clamp(finiteOr(
      config_.compressed_word_gap_dots, 4.5),
      config_.character_gap_dots + 0.3, 6.5);
  config_.word_gap_dots = std::clamp(finiteOr(
      config_.word_gap_dots, 7.0),
      config_.compressed_word_gap_dots + 0.4, 9.0);
  config_.unknown_symbol_cost = std::clamp(finiteOr(
      config_.unknown_symbol_cost, 2.5), 0.5, 12.0);
  config_.timing_tolerance_scale = std::clamp(finiteOr(
      config_.timing_tolerance_scale, 1.0), 0.75, 2.5);
  config_.maximum_timing_tolerance_scale = std::clamp(finiteOr(
      config_.maximum_timing_tolerance_scale, 2.5),
      config_.timing_tolerance_scale, 3.0);
  config_.minimum_tolerance_observations = std::clamp<std::size_t>(
      config_.minimum_tolerance_observations, 8U,
      config_.maximum_observations);
  if (!std::isfinite(config_.minimum_tolerance_observation_confidence)) {
    config_.minimum_tolerance_observation_confidence = 0.55F;
  }
  config_.minimum_tolerance_observation_confidence = std::clamp(
      config_.minimum_tolerance_observation_confidence, 0.25F, 0.95F);
  observations_.reserve(config_.maximum_observations);
}

void CwEventLattice::reset() noexcept {
  observations_.clear();
  input_truncated_ = false;
  rejected_observations_ = 0;
  coalesced_observations_ = 0;
}

std::optional<std::uint64_t> CwEventLattice::append(
    CwRunObservation observation) {
  if (!std::isfinite(observation.duration_ms) ||
      observation.duration_ms <= 0.0) {
    ++rejected_observations_;
    return std::nullopt;
  }
  if (!std::isfinite(observation.confidence)) observation.confidence = 0.0F;
  observation.confidence = std::clamp(observation.confidence, 0.0F, 1.0F);
  const bool has_timestamps = observation.started_ns != 0U ||
                              observation.ended_ns != 0U;
  if (has_timestamps && observation.ended_ns <= observation.started_ns) {
    ++rejected_observations_;
    return std::nullopt;
  }
  if (!observations_.empty()) {
    auto& previous = observations_.back();
    const bool previous_has_timestamps = previous.started_ns != 0U ||
                                         previous.ended_ns != 0U;
    if (has_timestamps != previous_has_timestamps ||
        (has_timestamps && observation.started_ns < previous.ended_ns)) {
      ++rejected_observations_;
      return std::nullopt;
    }
    if (previous.keyed == observation.keyed) {
      if (has_timestamps && observation.started_ns != previous.ended_ns) {
        ++rejected_observations_;
        return std::nullopt;
      }
      const double combined_duration = previous.duration_ms +
                                       observation.duration_ms;
      if (!std::isfinite(combined_duration)) {
        ++rejected_observations_;
        return std::nullopt;
      }
      previous.confidence = static_cast<float>(
          (static_cast<double>(previous.confidence) * previous.duration_ms +
           static_cast<double>(observation.confidence) *
               observation.duration_ms) /
          combined_duration);
      previous.duration_ms = combined_duration;
      if (has_timestamps) previous.ended_ns = observation.ended_ns;
      ++coalesced_observations_;
      return previous.observation_id;
    }
  }
  if (next_observation_id_ == std::numeric_limits<std::uint64_t>::max()) {
    ++rejected_observations_;
    return std::nullopt;
  }
  observation.observation_id = next_observation_id_++;
  if (observations_.size() >= config_.maximum_observations) {
    observations_.erase(observations_.begin());
    input_truncated_ = true;
  }
  observations_.push_back(observation);
  return observation.observation_id;
}

CwEventLatticeResult CwEventLattice::decode(
    const double dot_ms, const CwLatticeDecodeMode mode) const {
  CwEventLatticeResult result{
      .observations = observations_,
      .alternatives = {},
      .input_truncated = input_truncated_,
      .left_prefix_discarded = false,
      .rejected_observations = rejected_observations_,
      .coalesced_observations = coalesced_observations_,
      .effective_timing_tolerance_scale = config_.timing_tolerance_scale,
  };
  if (!std::isfinite(dot_ms) || dot_ms <= 0.0 || observations_.empty()) {
    return result;
  }

  std::size_t first_observation = 0;
  if (input_truncated_) {
    result.left_prefix_discarded = true;
    const double safe_boundary_ratio =
        1.0 + 0.55 * config_.maximum_timing_tolerance_scale;
    bool boundary_found = false;
    for (std::size_t index = 0; index + 1U < observations_.size(); ++index) {
      if (!observations_[index].keyed &&
          observations_[index].duration_ms / dot_ms >= safe_boundary_ratio &&
          observations_[index].confidence >= 0.35F &&
          observations_[index + 1U].keyed) {
        first_observation = index + 1U;
        boundary_found = true;
        break;
      }
    }
    if (!boundary_found) return result;
  }

  const double effective_tolerance = estimateTimingTolerance(
      observations_, first_observation, dot_ms, config_);
  result.effective_timing_tolerance_scale = effective_tolerance;
  float confidence_sum = 0.0F;
  std::size_t confidence_count = 0;
  for (std::size_t index = first_observation;
       index < observations_.size(); ++index) {
    confidence_sum += observations_[index].confidence;
    ++confidence_count;
  }
  const float evidence_confidence = confidence_count == 0U
      ? 0.0F
      : std::clamp(
            confidence_sum / static_cast<float>(confidence_count) /
                static_cast<float>(effective_tolerance),
            0.0F, 1.0F);

  std::vector<Path> paths(1);
  for (std::size_t observation_index = first_observation;
       observation_index < observations_.size(); ++observation_index) {
    const auto& observation = observations_[observation_index];
    if (!observation.keyed) continue;

    const double ratio = observation.duration_ms / dot_ms;
    const double uncertainty_cost = uncertaintyCost(observation.confidence);
    const double variance_cost =
        0.10 * std::max(0.0, std::log(effective_tolerance));
    const double dot_cost = uncertainty_cost +
                            variance_cost +
                            confidenceScale(observation.confidence) *
                                squaredResidual(
                                    ratio, 1.0,
                                    0.45 * effective_tolerance);
    const double dash_cost = uncertainty_cost +
                             variance_cost +
                             confidenceScale(observation.confidence) *
                                 squaredResidual(
                                     ratio, 3.0,
                                     0.85 * effective_tolerance);
    std::vector<Path> marked;
    marked.reserve(paths.size() * 2U);
    for (const auto& path : paths) {
      if (path.pending_elements.size() >=
          config_.maximum_elements_per_symbol) {
        continue;
      }
      Path dot_path = path;
      if (dot_path.pending_elements.empty()) {
        dot_path.pending_first_observation_id = observation.observation_id;
      }
      dot_path.pending_elements.push_back('.');
      dot_path.pending_last_observation_id = observation.observation_id;
      dot_path.cost += dot_cost;
      marked.push_back(std::move(dot_path));

      Path dash_path = path;
      if (dash_path.pending_elements.empty()) {
        dash_path.pending_first_observation_id = observation.observation_id;
      }
      dash_path.pending_elements.push_back('-');
      dash_path.pending_last_observation_id = observation.observation_id;
      dash_path.cost += dash_cost;
      marked.push_back(std::move(dash_path));
    }
    prune(marked, config_.beam_width);

    std::size_t gap_index = observation_index + 1U;
    while (gap_index < observations_.size() &&
           observations_[gap_index].keyed) {
      ++gap_index;
    }
    if (gap_index >= observations_.size()) {
      paths = std::move(marked);
      continue;
    }
    const auto& gap = observations_[gap_index];
    const double gap_ratio = gap.duration_ms / dot_ms;
    const double gap_uncertainty_cost = uncertaintyCost(gap.confidence) +
        0.10 * std::max(0.0, std::log(effective_tolerance));
    std::vector<Path> expanded;
    expanded.reserve(marked.size() * 3U);
    for (const auto& path : marked) {
      if (path.pending_elements.size() <
          config_.maximum_elements_per_symbol) {
        Path element_path = path;
        element_path.cost += gap_uncertainty_cost + gapCost(
            gap_ratio, gap.confidence, GapKind::Element, config_,
            effective_tolerance);
        expanded.push_back(std::move(element_path));
      }

      Path character_path = path;
      character_path.cost += gap_uncertainty_cost + gapCost(
          gap_ratio, gap.confidence, GapKind::Character, config_,
          effective_tolerance);
      const bool character_is_known =
          decodeElements(character_path.pending_elements).has_value();
      if (character_is_known) {
        Path unknown_character_path = character_path;
        unknown_character_path.cost += config_.unknown_symbol_cost;
        emitSymbol(unknown_character_path, false, observation.observation_id,
                   true);
        expanded.push_back(std::move(unknown_character_path));
      }
      emitSymbol(character_path, false, observation.observation_id);
      expanded.push_back(std::move(character_path));

      Path word_path = path;
      word_path.cost += gap_uncertainty_cost + gapCost(
          gap_ratio, gap.confidence, GapKind::Word, config_,
          effective_tolerance);
      const bool word_is_known =
          decodeElements(word_path.pending_elements).has_value();
      if (word_is_known) {
        Path unknown_word_path = word_path;
        unknown_word_path.cost += config_.unknown_symbol_cost;
        emitSymbol(unknown_word_path, true, observation.observation_id, true);
        expanded.push_back(std::move(unknown_word_path));
      }
      emitSymbol(word_path, true, observation.observation_id);
      expanded.push_back(std::move(word_path));
    }
    prune(expanded, config_.beam_width);
    paths = std::move(expanded);
  }

  std::vector<Path> finalized;
  finalized.reserve(paths.size() * 2U);
  for (auto& path : paths) {
    if (mode == CwLatticeDecodeMode::Flush &&
        !path.pending_elements.empty() &&
        decodeElements(path.pending_elements).has_value()) {
      Path unknown_path = path;
      unknown_path.cost += config_.unknown_symbol_cost;
      emitSymbol(unknown_path, false,
                 unknown_path.pending_last_observation_id, true);
      finalized.push_back(std::move(unknown_path));
    }
    if (mode == CwLatticeDecodeMode::Flush &&
        !path.pending_elements.empty()) {
      emitSymbol(path, false, path.pending_last_observation_id);
    }
    finalized.push_back(std::move(path));
  }
  prune(finalized, config_.beam_width);
  paths = std::move(finalized);
  const std::size_t count = std::min(paths.size(),
                                     config_.maximum_alternatives);
  result.alternatives.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.alternatives.push_back({
        .symbols = std::move(paths[index].symbols),
        .provisional_elements = std::move(paths[index].pending_elements),
        .provisional_first_observation_id =
            paths[index].pending_first_observation_id,
        .provisional_last_observation_id =
            paths[index].pending_last_observation_id,
        .acoustic_cost = paths[index].cost,
        .evidence_confidence = evidence_confidence,
    });
  }
  return result;
}

std::size_t CwEventLattice::observationCount() const noexcept {
  return observations_.size();
}

std::size_t CwEventLattice::stateBytes() const noexcept {
  return sizeof(*this) +
         observations_.capacity() * sizeof(CwRunObservation);
}

}  // namespace cwassistant::core
