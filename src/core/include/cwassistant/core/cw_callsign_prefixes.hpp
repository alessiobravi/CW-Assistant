#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cwassistant::core {

struct CwCallsignPrefixImportResult {
  bool accepted{false};
  std::size_t inserted_blocks{0};
  std::size_t duplicate_blocks{0};
  std::size_t ignored_lines{0};
};

// Where the blocks a table currently holds came from.
//
// Recorded at import time for the same reason the Morse alphabet records it:
// the shipped file and the compiled-in copy are generated from one source and
// hold identical blocks, so neither the contents nor the count can tell a
// successful file load from the fallback afterwards.
enum class CwCallsignPrefixSource {
  // Nothing has been imported yet.
  kNone,
  // Imported from text the caller obtained itself, which in this application
  // means the operator's readable prefix file.
  kLoaded,
  // Imported from the copy compiled into the binary, which only happens once
  // every attempt to read a file has failed.
  kBuiltin,
};

// The ITU prefix allocations, as a question a decode can be asked.
//
// A callsign's opening characters belong to an administration. A decoded token
// whose prefix falls in no allocation names a country that does not exist,
// which is strong evidence that the decode is wrong rather than that a rare
// station was heard.
//
// This table answers only "could this prefix exist at all". It is deliberately
// asymmetric: admitting a prefix that is no longer issued costs nothing,
// because something else has to agree before a callsign is believed, while
// refusing a prefix that is issued throws away a real station every time it is
// heard. Every decision below leans that way -- the fallback chain, the
// admit-everything behaviour of an empty table, and the tolerance for a line
// that names no country.
class CwCallsignPrefixTable {
 public:
  // One block per line: START END Country, the bounds inclusive and three
  // characters each. Blank lines and lines beginning with '#' are ignored, and
  // a line that does not parse is skipped rather than taking the rest of the
  // file with it.
  //
  // The country name is optional. It is informational -- nothing decides
  // anything from it -- so a line that names no country still answers the only
  // question that matters, and dropping the block over a missing name would
  // refuse every station using it. Bounds that are not three characters, or
  // that run backwards, are what makes a line malformed.
  //
  // A second block with the same start bound is counted as a duplicate and
  // ignored, so the file's first spelling wins, as it does in the alphabet.
  //
  // The source describes where the text came from and defaults to a caller
  // that read it, because that is what every host does; only the compiled-in
  // fallback names itself. An import that inserts no block at all leaves the
  // recorded source alone, so an unreadable or empty file cannot claim to be
  // the origin of blocks it did not supply.
  CwCallsignPrefixImportResult importText(
      std::string_view text,
      CwCallsignPrefixSource source = CwCallsignPrefixSource::kLoaded);

  // Whether the callsign's prefix falls inside an allocated block.
  //
  // This is the question that matters, and false is the answer that costs
  // something, so an empty table answers true: a table nothing has been
  // imported into has no opinion, and admitting every callsign is a far
  // smaller failure than refusing every station on the band at once.
  [[nodiscard]] bool isAllocatedPrefix(std::string_view callsign) const;

  // The administration the prefix belongs to, or an empty view when no block
  // covers it. Informational only -- for a diagnostic or a label a human
  // reads. Nothing should decide anything from this: the names go out of date
  // and blocks move between administrations, which is explicitly tolerated,
  // and an empty table returns nothing here while still admitting the same
  // callsign above.
  //
  // The view refers to storage this table owns and is valid until the next
  // import or clear.
  [[nodiscard]] std::string_view countryFor(std::string_view callsign) const;

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] CwCallsignPrefixSource source() const noexcept;
  void clear() noexcept;

 private:
  // A block bound, and the padded prefix a callsign is reduced to. Three
  // characters, compared as unsigned bytes so the ordering does not depend on
  // whether char is signed on the platform.
  using Bound = std::array<char, 3>;

  struct Block {
    Bound start{};
    Bound end{};
    // The largest end bound of this block and every block before it in sorted
    // order. The lookup uses it to know when it can stop searching backwards
    // through blocks that start below the prefix; see the implementation.
    Bound reach{};
    std::string country;
  };

  // findBlock reduces a heard token to the element that carries the country,
  // stripping a cluster node suffix and a portable prefix or suffix, and hands
  // that element to findBlockForElement, which is the matching rule itself.
  [[nodiscard]] const Block* findBlock(std::string_view callsign) const;
  [[nodiscard]] const Block* findBlockForElement(
      std::string_view element) const;
  void normalize();

  std::vector<Block> blocks_;
  CwCallsignPrefixSource source_{CwCallsignPrefixSource::kNone};
};

// The prefix table the decode path consults. Filled by the host application
// during startup; if it is still empty on first use it loads itself from
// CWA_DICTIONARY_DIR when that names a readable directory, and failing that
// from the compiled-in copy.
[[nodiscard]] const CwCallsignPrefixTable& cwSharedCallsignPrefixes();

// Whether the table in use came from the compiled-in copy rather than a loaded
// file. True means every attempt to read one failed, which is worth reporting:
// the operator's own edits to the file are then being ignored. This reads the
// source recorded at import time rather than inspecting the blocks, which
// cannot tell the two origins apart.
[[nodiscard]] bool cwCallsignPrefixesLoadedFromBuiltin() noexcept;
[[nodiscard]] CwCallsignPrefixTable& cwMutableSharedCallsignPrefixes() noexcept;

}  // namespace cwassistant::core
