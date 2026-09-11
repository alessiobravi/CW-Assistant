#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cwassistant/core/conversation_profile.hpp"

namespace cwassistant::core {

// A contest exchange file describes only the exchange: the fields each station
// sends and the order each side sends them in. The conversation flow, its
// states, and every transmit safety gate are built by the application from
// these inputs, so this type deliberately mirrors the builder's parameters
// instead of a whole ConversationProfile. There is no way to express a
// conversation kind, a completion policy, a flow, a state, or a key-down
// timeout here, and that absence is the point: a file an operator can edit
// must not be able to relax a transmit gate.
struct ParsedConversationProfile {
  ConversationProfileMetadata metadata;
  std::vector<ConversationFieldDefinition> received_exchange_fields;
  std::vector<ConversationFieldDefinition> station_exchange_fields;
  std::vector<ConversationMacroToken> runner_exchange_tokens;
  std::vector<ConversationMacroToken> caller_exchange_tokens;
};

struct ConversationProfileParse {
  bool accepted{false};
  ParsedConversationProfile profile;
  // One human-readable entry per problem, each naming the line it was found
  // on so an operator can correct the file that produced it.
  std::vector<std::string> errors;
};

// Parses the text of one contest exchange file, as documented in
// dictionaries/contests/README.txt.
//
// The parse is all or nothing: an unreadable line is never skipped, because a
// silently dropped exchange field is a contest exchange that is quietly wrong
// on the air. When any problem is reported the returned profile is empty and
// `accepted` is false. The function reports every problem it finds rather than
// stopping at the first, and it never throws: malformed input, including
// binary content, is reported through `errors`.
[[nodiscard]] ConversationProfileParse parse_conversation_profile_text(
    std::string_view text);

}  // namespace cwassistant::core
