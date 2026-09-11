#include "cwassistant/core/conversation_profile_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwassistant::core {
namespace {

// The operator's own copy of a contest file is editable on disk, so a
// truncated, binary, or generated file has to cost a bounded amount of work
// and produce a bounded number of messages rather than a wall of text.
constexpr std::size_t kMaximumTextBytes = 128U * 1024U;
constexpr std::size_t kMaximumLineLength = 1024U;
constexpr std::size_t kMaximumReportedProblems = 64U;
constexpr std::size_t kMaximumQuotedLength = 48U;
constexpr std::size_t kMaximumIdentifierLength = 64U;
constexpr std::size_t kMaximumLengthDigits = 6U;
constexpr std::size_t kMaximumRevisionDigits = 9U;
constexpr std::size_t kDefaultMaximumFieldLength = 64U;

constexpr std::string_view kLiteralPrefix = "literal:";
constexpr std::string_view kReceivedPrefix = "received:";
constexpr std::string_view kStationPrefix = "station:";

enum class Section { Header, Received, Station, Runner, Caller };

std::string_view trim(const std::string_view value) noexcept {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
    --last;
  }
  return value.substr(first, last - first);
}

bool equalsIgnoringCase(const std::string_view left,
                        const std::string_view right) noexcept {
  return left.size() == right.size() &&
      std::equal(left.begin(), left.end(), right.begin(),
                 [](const unsigned char first, const unsigned char second) {
                   return std::tolower(first) == std::tolower(second);
                 });
}

std::string uppercase(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<char>(std::toupper(character)));
  }
  return result;
}

// Mirrors the identifier rule validate_conversation_profile() applies to a
// built profile. Checking it here too costs one function and lets a bad
// identifier be reported against the line that wrote it instead of against
// the finished profile, where no line number survives.
bool identifierValid(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumIdentifierLength) return false;
  return std::all_of(value.begin(), value.end(),
                     [](const unsigned char character) {
                       return std::isalnum(character) != 0 ||
                           character == '-' || character == '_';
                     });
}

// Quotes a fragment for an error message. The text may come from a binary or
// truncated file, so it is reduced to printable ASCII and shortened: an error
// report must stay readable whatever was fed to the parser.
std::string describe(const std::string_view value) {
  std::string result;
  result.reserve(std::min(value.size(), kMaximumQuotedLength) + 5U);
  result.push_back('\'');
  for (const unsigned char character : value.substr(
           0, std::min(value.size(), kMaximumQuotedLength))) {
    result.push_back(std::isprint(character) != 0
                         ? static_cast<char>(character)
                         : '?');
  }
  result.push_back('\'');
  if (value.size() > kMaximumQuotedLength) result += "...";
  return result;
}

void addProblem(std::vector<std::string>& errors, const std::size_t line_number,
                const std::string& message) {
  if (errors.size() >= kMaximumReportedProblems) {
    if (errors.size() == kMaximumReportedProblems) {
      errors.emplace_back("further problems in this profile were not reported");
    }
    return;
  }
  errors.push_back("line " + std::to_string(line_number) + ": " + message);
}

// Must stay identical to the zone list the CQ WW exchange is built against:
// forty zones, two digits, zero padded.
std::vector<std::string> cqZoneValues() {
  std::vector<std::string> values;
  values.reserve(40);
  for (int zone = 1; zone <= 40; ++zone) {
    values.push_back(zone < 10 ? "0" + std::to_string(zone)
                               : std::to_string(zone));
  }
  return values;
}

std::optional<ConversationFieldKind> fieldKind(const std::string_view name) {
  if (equalsIgnoringCase(name, "rst")) return ConversationFieldKind::Rst;
  if (equalsIgnoringCase(name, "serial")) return ConversationFieldKind::Serial;
  if (equalsIgnoringCase(name, "enumeration")) {
    return ConversationFieldKind::Enumeration;
  }
  if (equalsIgnoringCase(name, "callsign")) {
    return ConversationFieldKind::Callsign;
  }
  if (equalsIgnoringCase(name, "text")) return ConversationFieldKind::FreeText;
  return std::nullopt;
}

std::optional<std::size_t> parseUnsigned(const std::string_view text,
                                         const std::size_t maximum_digits) {
  if (text.empty() || text.size() > maximum_digits) return std::nullopt;
  std::size_t value = 0;
  for (const unsigned char character : text) {
    if (std::isdigit(character) == 0) return std::nullopt;
    value = value * 10U + static_cast<std::size_t>(character - '0');
  }
  return value;
}

std::vector<std::string_view> splitOnWhitespace(const std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t index = 0;
  while (index < line.size()) {
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index])) != 0) {
      ++index;
    }
    const std::size_t start = index;
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index])) == 0) {
      ++index;
    }
    if (index > start) tokens.push_back(line.substr(start, index - start));
  }
  return tokens;
}

// A header key written after the first section is a common editing mistake.
// Recognizing its shape lets the parser say where the header belongs instead
// of complaining that '=' is not a field kind.
std::optional<std::string_view> headerEntryKey(const std::string_view line) {
  const auto tokens = splitOnWhitespace(line);
  if (tokens.size() >= 2U && tokens[1] == "=") return tokens[0];
  return std::nullopt;
}

std::optional<ConversationFieldAlias> parseAlias(
    const std::string_view specification, const bool per_character,
    const std::size_t line_number, std::vector<std::string>& errors) {
  const std::string_view keyword = per_character ? "cut" : "alias";
  const auto separator = specification.find('>');
  if (separator == std::string_view::npos ||
      specification.find('>', separator + 1U) != std::string_view::npos) {
    addProblem(errors, line_number,
               std::string{keyword} + "= must be written as A>B, but is " +
                   describe(specification));
    return std::nullopt;
  }
  const auto observed = specification.substr(0, separator);
  const auto normalized = specification.substr(separator + 1U);
  if (observed.empty() || normalized.empty()) {
    addProblem(errors, line_number,
               std::string{keyword} + "= needs text on both sides of '>'");
    return std::nullopt;
  }
  // A cut number substitutes one character for one character while the rest of
  // a value is read normally, so a multi-character side has no meaning the
  // decoder could act on.
  if (per_character && (observed.size() != 1U || normalized.size() != 1U)) {
    addProblem(errors, line_number,
               "cut= reads one character as another, so both sides must be a "
               "single character; use alias= for a whole token");
    return std::nullopt;
  }
  return ConversationFieldAlias{.observed = std::string{observed},
                                .normalized = std::string{normalized},
                                .per_character = per_character};
}

bool parseAllowedValues(const std::string_view specification,
                        const std::size_t line_number,
                        std::vector<std::string>& errors,
                        std::vector<std::string>& values) {
  if (equalsIgnoringCase(specification, "cq-zones")) {
    values = cqZoneValues();
    return true;
  }
  bool accepted = true;
  std::size_t start = 0;
  while (true) {
    const auto separator = specification.find(',', start);
    const auto entry = specification.substr(
        start, separator == std::string_view::npos ? std::string_view::npos
                                                   : separator - start);
    if (entry.empty()) {
      addProblem(errors, line_number,
                 "values= has an empty entry; write it as values=A,B,C with "
                 "no spaces");
      accepted = false;
    } else {
      values.emplace_back(entry);
    }
    if (separator == std::string_view::npos) break;
    start = separator + 1U;
  }
  return accepted;
}

std::optional<ConversationFieldDefinition> parseFieldDefinition(
    const std::vector<std::string_view>& tokens, const std::size_t line_number,
    std::vector<std::string>& errors) {
  if (tokens.size() < 3U) {
    addProblem(errors, line_number,
               "a field line needs '<id> <kind> <required|optional>' before "
               "any option");
    return std::nullopt;
  }
  bool accepted = true;
  ConversationFieldDefinition definition;
  definition.maximum_length = kDefaultMaximumFieldLength;

  if (!identifierValid(tokens[0])) {
    addProblem(errors, line_number,
               "field id " + describe(tokens[0]) +
                   " must be letters, digits, '-' or '_'");
    accepted = false;
  }
  definition.id = std::string{tokens[0]};

  const auto kind = fieldKind(tokens[1]);
  if (!kind) {
    addProblem(errors, line_number,
               "unknown field kind " + describe(tokens[1]) +
                   "; expected rst, serial, enumeration, callsign or text");
    accepted = false;
  } else {
    definition.kind = *kind;
  }

  if (equalsIgnoringCase(tokens[2], "required")) {
    definition.required_for_completion = true;
  } else if (equalsIgnoringCase(tokens[2], "optional")) {
    definition.required_for_completion = false;
  } else {
    addProblem(errors, line_number,
               "field " + describe(tokens[0]) + " must say required or "
               "optional, not " + describe(tokens[2]));
    accepted = false;
  }

  bool maximum_seen = false;
  bool values_seen = false;
  for (std::size_t index = 3; index < tokens.size(); ++index) {
    const std::string_view option = tokens[index];
    const auto separator = option.find('=');
    if (separator == std::string_view::npos) {
      addProblem(errors, line_number,
                 "field option " + describe(option) +
                     " is not written as name=value");
      accepted = false;
      continue;
    }
    const auto name = option.substr(0, separator);
    const auto value = option.substr(separator + 1U);
    if (equalsIgnoringCase(name, "max")) {
      if (maximum_seen) {
        addProblem(errors, line_number, "max= is given more than once");
        accepted = false;
      }
      maximum_seen = true;
      const auto length = parseUnsigned(value, kMaximumLengthDigits);
      if (!length || *length == 0U) {
        addProblem(errors, line_number,
                   "max= needs a positive character count, not " +
                       describe(value));
        accepted = false;
      } else {
        definition.maximum_length = *length;
      }
    } else if (equalsIgnoringCase(name, "values")) {
      if (values_seen) {
        addProblem(errors, line_number, "values= is given more than once");
        accepted = false;
      }
      values_seen = true;
      // An enumeration is the only kind whose value set is checked, so values=
      // on any other kind is a mistake about what the field accepts rather
      // than a harmless extra: it is rejected instead of ignored.
      if (kind && *kind != ConversationFieldKind::Enumeration) {
        addProblem(errors, line_number,
                   "values= applies only to an enumeration field");
        accepted = false;
      }
      std::vector<std::string> allowed;
      if (!parseAllowedValues(value, line_number, errors, allowed)) {
        accepted = false;
      }
      definition.allowed_values = std::move(allowed);
    } else if (equalsIgnoringCase(name, "alias") ||
               equalsIgnoringCase(name, "cut")) {
      const bool per_character = equalsIgnoringCase(name, "cut");
      const auto alias =
          parseAlias(value, per_character, line_number, errors);
      if (!alias) {
        accepted = false;
      } else {
        definition.aliases.push_back(*alias);
      }
    } else {
      addProblem(errors, line_number,
                 "unknown field option " + describe(name) +
                     "; expected max, values, alias or cut");
      accepted = false;
    }
  }

  if (kind && *kind == ConversationFieldKind::Enumeration && !values_seen) {
    addProblem(errors, line_number,
               "an enumeration field needs values=, otherwise nothing the "
               "other station sends can match it");
    accepted = false;
  }

  // max= may be written after values=, alias= or cut=, so the lengths are
  // compared once the whole line is known.
  std::unordered_set<std::string> distinct_values;
  for (const auto& value : definition.allowed_values) {
    if (!distinct_values.insert(uppercase(value)).second) {
      addProblem(errors, line_number,
                 "values= repeats " + describe(value));
      accepted = false;
    } else if (value.size() > definition.maximum_length) {
      addProblem(errors, line_number,
                 "value " + describe(value) + " is longer than max=" +
                     std::to_string(definition.maximum_length));
      accepted = false;
    }
  }
  std::unordered_set<std::string> distinct_aliases;
  for (const auto& alias : definition.aliases) {
    if (!distinct_aliases.insert(uppercase(alias.observed)).second) {
      addProblem(errors, line_number,
                 "more than one alias= or cut= reads " +
                     describe(alias.observed));
      accepted = false;
    } else if (alias.observed.size() > definition.maximum_length ||
               alias.normalized.size() > definition.maximum_length) {
      addProblem(errors, line_number,
                 "alias " + describe(alias.observed) + " is longer than max=" +
                     std::to_string(definition.maximum_length));
      accepted = false;
    }
  }

  if (!accepted) return std::nullopt;
  return definition;
}

std::optional<ConversationMacroToken> parseMacroToken(
    const std::string_view line, const std::size_t line_number,
    std::vector<std::string>& errors) {
  if (line.size() >= kLiteralPrefix.size() &&
      equalsIgnoringCase(line.substr(0, kLiteralPrefix.size()),
                         kLiteralPrefix)) {
    // The remainder is kept as written, interior and leading spaces included,
    // because a macro sends the literal exactly as it appears here. Only the
    // trailing whitespace a line ending carries has already been removed.
    const auto value = line.substr(kLiteralPrefix.size());
    if (value.empty()) {
      addProblem(errors, line_number, "literal: has no text to send");
      return std::nullopt;
    }
    return ConversationMacroToken{
        .source = ConversationMacroTokenSource::Literal,
        .value = std::string{value}};
  }

  // A field token is checked for shape only. Whether the field exists is
  // settled against the finished profile, because the runner and caller
  // orders legitimately name fields the application adds itself, such as the
  // remote callsign, which no file declares.
  const bool received = line.size() >= kReceivedPrefix.size() &&
      equalsIgnoringCase(line.substr(0, kReceivedPrefix.size()),
                         kReceivedPrefix);
  const bool station = line.size() >= kStationPrefix.size() &&
      equalsIgnoringCase(line.substr(0, kStationPrefix.size()),
                         kStationPrefix);
  if (!received && !station) {
    addProblem(errors, line_number,
               "expected received:, station: or literal:, not " +
                   describe(line));
    return std::nullopt;
  }
  const auto id = trim(line.substr(
      received ? kReceivedPrefix.size() : kStationPrefix.size()));
  if (!identifierValid(id)) {
    addProblem(errors, line_number,
               "field name " + describe(id) +
                   " must be letters, digits, '-' or '_'");
    return std::nullopt;
  }
  return ConversationMacroToken{
      .source = received ? ConversationMacroTokenSource::ReceivedField
                         : ConversationMacroTokenSource::StationField,
      .value = std::string{id}};
}

struct HeaderKeysSeen {
  bool id{false};
  bool title{false};
  bool revision{false};
  bool rules_url{false};
  bool valid_from{false};
  bool valid_to{false};
};

struct ParserState {
  ConversationProfileParse result;
  Section section{Section::Header};
  HeaderKeysSeen header_keys;
  bool received_seen{false};
  bool station_seen{false};
  bool runner_seen{false};
  bool caller_seen{false};
  // Field identifiers must be unique across both exchanges, and the four the
  // application contributes to every contest profile are reserved. Seeding
  // them here turns a collision into one clear message on the offending line.
  std::unordered_set<std::string> claimed_field_ids{
      "remote_call", "addressed_call", "my_call", "rst_tx"};
};

bool claimHeaderKey(bool& seen, const std::string_view key,
                    const std::size_t line_number,
                    std::vector<std::string>& errors) {
  if (seen) {
    addProblem(errors, line_number,
               "header key " + describe(key) + " is given more than once");
    return false;
  }
  seen = true;
  return true;
}

void parseHeaderEntry(const std::string_view line,
                      const std::size_t line_number, ParserState& state) {
  auto& errors = state.result.errors;
  auto& metadata = state.result.profile.metadata;
  const auto separator = line.find('=');
  if (separator == std::string_view::npos) {
    addProblem(errors, line_number,
               "expected a 'key = value' header line or a section such as "
               "[received], not " + describe(line));
    return;
  }
  const auto key = trim(line.substr(0, separator));
  // Only the first '=' separates the pair, so a rules URL carrying a query
  // string survives intact.
  const auto value = trim(line.substr(separator + 1U));
  if (key.empty()) {
    addProblem(errors, line_number, "header line has no key before '='");
    return;
  }
  if (value.empty()) {
    addProblem(errors, line_number,
               "header key " + describe(key) + " has no value");
    return;
  }

  if (equalsIgnoringCase(key, "id")) {
    if (!claimHeaderKey(state.header_keys.id, key, line_number, errors)) return;
    if (!identifierValid(value)) {
      addProblem(errors, line_number,
                 "id " + describe(value) +
                     " must be letters, digits, '-' or '_'");
      return;
    }
    metadata.id = std::string{value};
  } else if (equalsIgnoringCase(key, "title")) {
    if (!claimHeaderKey(state.header_keys.title, key, line_number, errors)) {
      return;
    }
    metadata.title = std::string{value};
  } else if (equalsIgnoringCase(key, "revision")) {
    if (!claimHeaderKey(state.header_keys.revision, key, line_number, errors)) {
      return;
    }
    const auto revision = parseUnsigned(value, kMaximumRevisionDigits);
    if (!revision || *revision == 0U) {
      addProblem(errors, line_number,
                 "revision must be a positive whole number, not " +
                     describe(value));
      return;
    }
    metadata.revision = static_cast<std::uint32_t>(*revision);
  } else if (equalsIgnoringCase(key, "rules-url")) {
    if (!claimHeaderKey(state.header_keys.rules_url, key, line_number,
                        errors)) {
      return;
    }
    metadata.rules_url = std::string{value};
  } else if (equalsIgnoringCase(key, "valid-from")) {
    if (!claimHeaderKey(state.header_keys.valid_from, key, line_number,
                        errors)) {
      return;
    }
    metadata.valid_from = std::string{value};
  } else if (equalsIgnoringCase(key, "valid-to")) {
    if (!claimHeaderKey(state.header_keys.valid_to, key, line_number, errors)) {
      return;
    }
    metadata.valid_to = std::string{value};
  } else {
    // An unknown key is rejected rather than skipped: it is usually a
    // misspelling of one that matters, and accepting it would present the
    // operator a profile missing whatever it was meant to say.
    addProblem(errors, line_number,
               "unknown header key " + describe(key) +
                   "; expected id, title, revision, rules-url, valid-from or "
                   "valid-to");
  }
}

void parseSectionHeader(const std::string_view line,
                        const std::size_t line_number, ParserState& state) {
  auto& errors = state.result.errors;
  if (line.size() < 2U || line.back() != ']') {
    addProblem(errors, line_number,
               "section name " + describe(line) +
                   " must be closed with ']'");
    return;
  }
  const auto name = trim(line.substr(1U, line.size() - 2U));
  Section section = Section::Header;
  bool* seen = nullptr;
  if (equalsIgnoringCase(name, "received")) {
    section = Section::Received;
    seen = &state.received_seen;
  } else if (equalsIgnoringCase(name, "station")) {
    section = Section::Station;
    seen = &state.station_seen;
  } else if (equalsIgnoringCase(name, "runner")) {
    section = Section::Runner;
    seen = &state.runner_seen;
  } else if (equalsIgnoringCase(name, "caller")) {
    section = Section::Caller;
    seen = &state.caller_seen;
  } else {
    addProblem(errors, line_number,
               "unknown section " + describe(name) +
                   "; expected received, station, runner or caller");
    return;
  }
  // A repeated section is refused rather than merged, because the order of
  // the runner and caller tokens is the order they go on the air and a second
  // block makes that order ambiguous to read.
  if (*seen) {
    addProblem(errors, line_number,
               "section " + describe(name) + " appears more than once");
    return;
  }
  *seen = true;
  state.section = section;
}

void parseFieldLine(const std::string_view line, const std::size_t line_number,
                    ParserState& state,
                    std::vector<ConversationFieldDefinition>& fields) {
  auto& errors = state.result.errors;
  auto definition =
      parseFieldDefinition(splitOnWhitespace(line), line_number, errors);
  if (!definition) return;
  if (!state.claimed_field_ids.insert(definition->id).second) {
    addProblem(errors, line_number,
               "field id " + describe(definition->id) +
                   " is already declared or is reserved by the application");
    return;
  }
  fields.push_back(std::move(*definition));
}

void parseTokenLine(const std::string_view line, const std::size_t line_number,
                    ParserState& state,
                    std::vector<ConversationMacroToken>& tokens) {
  auto token = parseMacroToken(line, line_number, state.result.errors);
  if (!token) return;
  tokens.push_back(std::move(*token));
}

void parseLine(const std::string_view raw_line, const std::size_t line_number,
               ParserState& state) {
  auto& errors = state.result.errors;
  if (raw_line.size() > kMaximumLineLength) {
    addProblem(errors, line_number,
               "line is longer than " + std::to_string(kMaximumLineLength) +
                   " characters");
    return;
  }
  // trim() also removes the carriage return of a CRLF ending, so a file
  // written on any platform reads the same.
  const auto line = trim(raw_line);
  if (line.empty() || line.front() == '#') return;
  if (line.front() == '[') {
    parseSectionHeader(line, line_number, state);
    return;
  }
  auto& profile = state.result.profile;
  if (state.section != Section::Header) {
    if (const auto key = headerEntryKey(line)) {
      addProblem(errors, line_number,
                 "header key " + describe(*key) +
                     " must appear before the first section");
      return;
    }
  }
  switch (state.section) {
    case Section::Header:
      parseHeaderEntry(line, line_number, state);
      return;
    case Section::Received:
      parseFieldLine(line, line_number, state,
                     profile.received_exchange_fields);
      return;
    case Section::Station:
      parseFieldLine(line, line_number, state,
                     profile.station_exchange_fields);
      return;
    case Section::Runner:
      parseTokenLine(line, line_number, state,
                     profile.runner_exchange_tokens);
      return;
    case Section::Caller:
      parseTokenLine(line, line_number, state,
                     profile.caller_exchange_tokens);
      return;
  }
}

ConversationProfileParse parseText(std::string_view text) {
  ParserState state;
  // The kind is fixed rather than read: these files exist to describe contest
  // exchanges, and a file that could name its own kind could ask for a
  // completion policy the contest rules do not allow.
  state.result.profile.metadata.kind = ConversationKind::Contest;

  if (text.size() > kMaximumTextBytes) {
    state.result.errors.push_back(
        "profile text is larger than " + std::to_string(kMaximumTextBytes) +
        " bytes and was not read");
    return state.result;
  }

  // An operator editing a contest on Windows can save the file with a byte
  // order mark. Skipping it keeps the first header key readable instead of
  // rejecting the whole contest over three invisible bytes.
  constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
  if (text.starts_with(kByteOrderMark)) {
    text.remove_prefix(kByteOrderMark.size());
  }

  std::size_t line_start = 0;
  std::size_t line_number = 0;
  while (line_start <= text.size()) {
    const auto line_break = text.find('\n', line_start);
    const std::size_t line_end =
        line_break == std::string_view::npos ? text.size() : line_break;
    ++line_number;
    parseLine(text.substr(line_start, line_end - line_start), line_number,
              state);
    if (line_break == std::string_view::npos) break;
    line_start = line_break + 1U;
  }

  if (!state.header_keys.id) {
    state.result.errors.emplace_back("the profile has no id");
  }
  if (!state.header_keys.title) {
    state.result.errors.emplace_back("the profile has no title");
  }
  state.result.accepted = state.result.errors.empty();
  return state.result;
}

}  // namespace

ConversationProfileParse parse_conversation_profile_text(
    const std::string_view text) {
  ConversationProfileParse result;
  try {
    result = parseText(text);
  } catch (...) {
    // The core is dependency-free and reports failure by value, so nothing
    // escapes here. Only allocation can throw on this path, and a file that
    // cannot be held in memory is simply not a profile.
    result.accepted = false;
    result.errors.clear();
    result.errors.emplace_back("the profile could not be read");
  }
  if (!result.accepted) {
    // Never hand back a partial exchange. A caller that overlooked the flag
    // would otherwise send a contest exchange missing whatever the rejected
    // lines declared, which is wrong on the air rather than merely incomplete.
    result.profile = ParsedConversationProfile{};
    result.profile.metadata.kind = ConversationKind::Contest;
  }
  return result;
}

}  // namespace cwassistant::core
