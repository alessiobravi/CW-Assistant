#include "cwassistant/core/cw_morse_alphabet.hpp"

#include <cctype>

#include "cwassistant/core/cw_builtin_morse_alphabet.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>

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

std::size_t builtinAlphabetSymbolCount() {
  CwMorseAlphabet builtin;
  static_cast<void>(builtin.importText(kCwBuiltinMorseAlphabet));
  return builtin.size();
}

}  // namespace

CwMorseAlphabetImportResult CwMorseAlphabet::importText(
    const std::string_view text) {
  CwMorseAlphabetImportResult result;
  result.accepted = true;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == std::string_view::npos ? text.size() : newline;
    const auto line = trim(text.substr(offset, end - offset));
    if (!line.empty() && line.front() != '#') {
      const auto split = line.find_first_of(" \t");
      const auto code = split == std::string_view::npos
          ? std::string_view{} : trim(line.substr(0, split));
      const auto symbol = split == std::string_view::npos
          ? std::string_view{} : trim(line.substr(split));
      const bool well_formed = !code.empty() && !symbol.empty() &&
          code.find_first_not_of(".-") == std::string_view::npos;
      if (!well_formed) {
        ++result.ignored_lines;
      } else if (symbols_.emplace(std::string(code),
                                  std::string(symbol)).second) {
        ++result.inserted_symbols;
      } else {
        ++result.duplicate_codes;
      }
    }
    if (newline == std::string_view::npos) break;
    offset = newline + 1U;
  }
  return result;
}

std::string_view CwMorseAlphabet::symbolFor(
    const std::string_view elements) const {
  const auto found = symbols_.find(std::string(elements));
  return found == symbols_.end() ? std::string_view{} : found->second;
}

std::size_t CwMorseAlphabet::size() const noexcept { return symbols_.size(); }
bool CwMorseAlphabet::empty() const noexcept { return symbols_.empty(); }
void CwMorseAlphabet::clear() noexcept { symbols_.clear(); }

CwMorseAlphabet& cwMutableSharedMorseAlphabet() noexcept {
  static CwMorseAlphabet alphabet;
  return alphabet;
}

const CwMorseAlphabet& cwSharedMorseAlphabet() {
  auto& alphabet = cwMutableSharedMorseAlphabet();
  if (!alphabet.empty()) return alphabet;
  // Decoding nothing at all is a far worse failure than reading an
  // environment variable, so a caller that never loaded the alphabet recovers
  // here rather than returning an unknown symbol for every character.
  const char* directory = std::getenv("CWA_DICTIONARY_DIR");
  if (directory != nullptr) {
    std::ifstream input(std::string(directory) + "/morse-alphabet.txt",
                        std::ios::binary);
    if (input) {
      std::ostringstream buffer;
      buffer << input.rdbuf();
      static_cast<void>(alphabet.importText(buffer.str()));
    }
  }
  if (!alphabet.empty()) return alphabet;
  // Last resort. An operator's file that is missing, empty, or unreadable must
  // not leave the application tracking signals and decoding nothing, which is
  // what it did: the alphabet was made data with no fallback, and a packaged
  // build that failed to load it went silently deaf with no diagnostic. The
  // text below is generated from the same shipped file at build time, so this
  // is not a second table to keep in step -- it is the same one, compiled in.
  static_cast<void>(alphabet.importText(kCwBuiltinMorseAlphabet));
  return alphabet;
}

bool cwMorseAlphabetLoadedFromBuiltin() noexcept {
  return cwMutableSharedMorseAlphabet().size() ==
      builtinAlphabetSymbolCount();
}

}  // namespace cwassistant::core
