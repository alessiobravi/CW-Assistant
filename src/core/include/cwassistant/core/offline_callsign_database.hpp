#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cwassistant::core {

struct OfflineCallsignDatabaseLimits {
  std::size_t maximum_input_bytes{32U * 1'024U * 1'024U};
  std::size_t maximum_records{1'000'000U};
  std::size_t maximum_line_bytes{1'024U};
  std::size_t maximum_query_results{64U};
  std::size_t maximum_edit_distance{4U};
};

struct OfflineCallsignImportResult {
  bool accepted{false};
  bool capacity_reached{false};
  std::size_t inserted_records{0};
  std::size_t duplicate_records{0};
  std::size_t ignored_lines{0};
  std::size_t overlong_lines{0};
};

enum class CallsignDifferenceKind {
  wildcard_match,
  substitution,
  insertion,
  deletion,
};

struct CallsignCharacterDifference {
  CallsignDifferenceKind kind{CallsignDifferenceKind::substitution};
  std::size_t query_index{0};
  std::size_t callsign_index{0};
  char query_character{'\0'};
  char callsign_character{'\0'};
};

struct OfflineCallsignMatch {
  std::string callsign;
  std::size_t edit_distance{0};
  std::vector<CallsignCharacterDifference> differences;
};

// A bounded, dependency-free index for operator-supplied master.scp and
// call-history text. Database misses are advisory and never determine whether
// a syntactically valid callsign is accepted elsewhere in the application.
class OfflineCallsignDatabase {
 public:
  explicit OfflineCallsignDatabase(
      OfflineCallsignDatabaseLimits limits = {});

  void clear() noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] OfflineCallsignImportResult importText(std::string_view text);
  [[nodiscard]] bool contains(std::string_view callsign) const;
  [[nodiscard]] std::vector<OfflineCallsignMatch> query(
      std::string_view pattern, std::size_t maximum_edit_distance,
      std::size_t limit) const;

 private:
  OfflineCallsignDatabaseLimits limits_{};
  std::unordered_set<std::string> callsigns_;
};

}  // namespace cwassistant::core
