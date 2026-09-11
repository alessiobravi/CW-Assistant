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

// Where the symbols an alphabet currently holds came from.
//
// This has to be recorded when the symbols are imported, because it cannot be
// recovered afterwards: the shipped file and the compiled-in copy are
// generated from the same source and therefore hold exactly the same entries,
// so neither their contents nor their count distinguishes a successful file
// load from the fallback. A diagnostic that tried to tell them apart that way
// reported the fallback on every healthy installation, which is precisely
// backwards for the fault it exists to reveal.
enum class CwMorseAlphabetSource {
  // Nothing has been imported yet.
  kNone,
  // Imported from text the caller obtained itself, which in this application
  // means the operator's readable alphabet file.
  kLoaded,
  // Imported from the copy compiled into the binary, which only happens once
  // every attempt to read a file has failed.
  kBuiltin,
};

// The element pattern to symbol mapping the decoder reads.
//
// Unlike the exchange vocabulary, an empty alphabet is not a graceful
// degradation: the decoder would return an unknown symbol for every character.
// The host application therefore loads this before decoding, and the shared
// instance below falls back first to reading the directory named by the
// CWA_DICTIONARY_DIR environment variable and finally to the compiled-in copy,
// so that a test or tool which forgets cannot silently decode nothing.
class CwMorseAlphabet {
 public:
  // One entry per line: elements, whitespace, symbol. A symbol may be several
  // characters so that prosigns can be represented. Blank lines and lines
  // beginning with '#' are ignored.
  //
  // The source describes where the text came from and defaults to a caller
  // that read it, because that is what every host does; only the compiled-in
  // fallback names itself. An import that inserts no symbol at all leaves the
  // recorded source alone, so an unreadable or empty file cannot claim to be
  // the origin of symbols it did not supply.
  CwMorseAlphabetImportResult importText(
      std::string_view text,
      CwMorseAlphabetSource source = CwMorseAlphabetSource::kLoaded);

  // The symbol for an element pattern, or an empty view when it is unknown.
  [[nodiscard]] std::string_view symbolFor(std::string_view elements) const;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] CwMorseAlphabetSource source() const noexcept;
  void clear() noexcept;

 private:
  std::unordered_map<std::string, std::string> symbols_;
  CwMorseAlphabetSource source_{CwMorseAlphabetSource::kNone};
};

// The alphabet the decoder consults. Filled by the host application during
// startup; if it is still empty on first use it loads itself from
// CWA_DICTIONARY_DIR when that names a readable directory, and failing that
// from the compiled-in copy.
[[nodiscard]] const CwMorseAlphabet& cwSharedMorseAlphabet();

// Whether the alphabet in use came from the compiled-in copy rather than a
// loaded file. True means every attempt to read one failed, which is worth
// reporting: the operator's copy is then being ignored. This reads the source
// recorded at import time rather than inspecting the symbols, which cannot
// tell the two origins apart.
[[nodiscard]] bool cwMorseAlphabetLoadedFromBuiltin() noexcept;
[[nodiscard]] CwMorseAlphabet& cwMutableSharedMorseAlphabet() noexcept;

}  // namespace cwassistant::core
