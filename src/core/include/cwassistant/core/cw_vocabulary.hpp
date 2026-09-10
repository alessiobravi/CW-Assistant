#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cwassistant::core {

struct CwVocabularyImportResult {
  bool accepted{false};
  std::size_t inserted_tokens{0};
  std::size_t duplicate_tokens{0};
  std::size_t ignored_lines{0};
};

// The abbreviations, Q-codes and prosigns an operator sends verbatim, plus the
// subset of them that may legitimately run straight into a callsign.
//
// This is operator-editable domain data rather than algorithm, so it is loaded
// from text and never compiled in: a station that works a mode or a contest
// with its own vocabulary can extend it without a rebuild. An empty vocabulary
// is valid and simply contributes no spacing evidence, which is the correct
// behaviour when the files are absent -- the decoder falls back to acoustic
// evidence alone rather than to a stale built-in list.
//
// Membership is only ever consulted to choose between candidates that carry
// identical characters, so no entry here can change a decoded letter.
class CwVocabulary {
 public:
  // One token per line. Blank lines and lines beginning with '#' are ignored.
  // Tokens are upper-cased; anything that is not alphanumeric or '/' is
  // rejected rather than silently truncated.
  CwVocabularyImportResult importExchangeWords(std::string_view text);

  // Tokens that may precede a callsign with no gap, such as the CQ in
  // "CQDESV7BIO". Every entry must also be an exchange word.
  CwVocabularyImportResult importWordGapPrefixes(std::string_view text);

  [[nodiscard]] bool containsExchangeWord(std::string_view token) const;
  [[nodiscard]] const std::vector<std::string>& wordGapPrefixes() const noexcept;
  [[nodiscard]] std::size_t exchangeWordCount() const noexcept;
  void clear() noexcept;

 private:
  std::unordered_set<std::string> exchange_words_;
  std::vector<std::string> word_gap_prefixes_;
};

// The vocabulary the decoder consults when no other is supplied. The host
// application fills this once during startup, before any decoding thread is
// running; it is read-only thereafter.
[[nodiscard]] CwVocabulary& cwSharedVocabulary() noexcept;

}  // namespace cwassistant::core
