#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cwassistant::core {

struct CwMorseAlphabetImportResult {
  bool accepted{false};
  std::size_t inserted_symbols{0};
  std::size_t duplicate_codes{0};
  std::size_t ignored_lines{0};
};

// The element pattern to symbol mapping the decoder reads.
//
// Unlike the exchange vocabulary, an empty alphabet is not a graceful
// degradation: the decoder would return an unknown symbol for every character.
// The host application therefore loads this before decoding, and the shared
// instance below falls back to reading the directory named by the
// CWA_DICTIONARY_DIR environment variable so that a test or tool which forgets
// cannot silently decode nothing. Nothing is compiled in.
class CwMorseAlphabet {
 public:
  // One entry per line: elements, whitespace, symbol. A symbol may be several
  // characters so that prosigns can be represented. Blank lines and lines
  // beginning with '#' are ignored.
  CwMorseAlphabetImportResult importText(std::string_view text);

  // The symbol for an element pattern, or an empty view when it is unknown.
  [[nodiscard]] std::string_view symbolFor(std::string_view elements) const;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  void clear() noexcept;

 private:
  std::unordered_map<std::string, std::string> symbols_;
};

// The alphabet the decoder consults. Filled by the host application during
// startup; if it is still empty on first use it loads itself from
// CWA_DICTIONARY_DIR when that names a readable directory.
[[nodiscard]] const CwMorseAlphabet& cwSharedMorseAlphabet();

// Whether the alphabet in use came from the compiled-in copy rather than a
// loaded file. True means every attempt to read one failed, which is worth
// reporting: the operator's copy is then being ignored.
[[nodiscard]] bool cwMorseAlphabetLoadedFromBuiltin() noexcept;
[[nodiscard]] CwMorseAlphabet& cwMutableSharedMorseAlphabet() noexcept;

}  // namespace cwassistant::core
