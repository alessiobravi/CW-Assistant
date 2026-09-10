#include "cwassistant/core/cw_vocabulary.hpp"

#include <algorithm>
#include <cctype>

namespace cwassistant::core {
namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

// A token is the vocabulary's whole unit of meaning, so a line carrying
// anything else is rejected rather than trimmed down to its valid prefix: a
// silently truncated entry would score matches its author never wrote.
bool normalizeToken(const std::string_view line, std::string& token) {
  token.clear();
  if (line.empty() || line.size() > 16U) return false;
  for (const unsigned char character : line) {
    if (std::isalnum(character) == 0 && character != '/') return false;
    token.push_back(static_cast<char>(std::toupper(character)));
  }
  return !token.empty();
}

template <typename Accept>
CwVocabularyImportResult importLines(const std::string_view text,
                                     Accept&& accept) {
  CwVocabularyImportResult result;
  result.accepted = true;
  std::size_t offset = 0;
  std::string token;
  while (offset <= text.size()) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == std::string_view::npos ? text.size() : newline;
    const auto line = trim(text.substr(offset, end - offset));
    if (!line.empty() && line.front() != '#') {
      if (!normalizeToken(line, token)) {
        ++result.ignored_lines;
      } else if (accept(token)) {
        ++result.inserted_tokens;
      } else {
        ++result.duplicate_tokens;
      }
    }
    if (newline == std::string_view::npos) break;
    offset = newline + 1U;
  }
  return result;
}

}  // namespace

CwVocabularyImportResult CwVocabulary::importExchangeWords(
    const std::string_view text) {
  return importLines(text, [this](const std::string& token) {
    return exchange_words_.insert(token).second;
  });
}

CwVocabularyImportResult CwVocabulary::importWordGapPrefixes(
    const std::string_view text) {
  return importLines(text, [this](const std::string& token) {
    // A prefix that is not itself an exchange word would let word-gap repair
    // split on a string the scorer does not recognise, so it is refused.
    if (!exchange_words_.contains(token)) return false;
    if (std::ranges::find(word_gap_prefixes_, token) !=
        word_gap_prefixes_.end()) {
      return false;
    }
    word_gap_prefixes_.push_back(token);
    return true;
  });
}

bool CwVocabulary::containsExchangeWord(const std::string_view token) const {
  return exchange_words_.contains(std::string(token));
}

const std::vector<std::string>& CwVocabulary::wordGapPrefixes() const noexcept {
  return word_gap_prefixes_;
}

std::size_t CwVocabulary::exchangeWordCount() const noexcept {
  return exchange_words_.size();
}

void CwVocabulary::clear() noexcept {
  exchange_words_.clear();
  word_gap_prefixes_.clear();
}

CwVocabulary& cwSharedVocabulary() noexcept {
  static CwVocabulary vocabulary;
  return vocabulary;
}

}  // namespace cwassistant::core
