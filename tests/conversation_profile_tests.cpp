#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "cwassistant/core/conversation_profile.hpp"

namespace {
int failures = 0;
void expect(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void testReferenceProfiles() {
  using namespace cwassistant::core;
  expect(validate_conversation_profile(neutral_monitoring_profile()).valid(),
         "neutral receive-only monitoring validates");
  expect(validate_conversation_profile(ordinary_cw_profile()).valid(),
         "ordinary CW profile validates");
  expect(validate_conversation_profile(cq_ww_cw_profile()).valid(),
         "CQ WW validates");
  expect(validate_conversation_profile(cq_wpx_cw_profile()).valid(),
         "CQ WPX validates");
  expect(validate_conversation_profile(arrl_field_day_cw_profile()).valid(),
         "Field Day validates");
  expect(validate_conversation_profile(arrl_sweepstakes_cw_profile()).valid(),
         "Sweepstakes validates");
  expect(neutral_monitoring_profile().macros.empty(),
         "neutral monitoring has no transmit macros");
}

void testTypedFieldSuggestions() {
  using namespace cwassistant::core;
  const auto& wpx = cq_wpx_cw_profile();
  const auto rst = suggest_received_field_value(wpx, "rst_rx", "5nn");
  expect(rst && rst->syntactically_valid && rst->normalized == "599",
         "RST shorthand normalizes within RST field");
  const auto serial = suggest_received_field_value(wpx, "serial_rx", "1T3N");
  expect(serial && serial->syntactically_valid && serial->normalized == "1039",
         "declared cut numbers normalize within serial field");
  const auto call = suggest_received_field_value(wpx, "remote_call", "n1abc");
  expect(call && call->syntactically_valid && call->normalized == "N1ABC",
         "callsign has typed validation without serial aliases");
  const auto invalid_call = suggest_received_field_value(wpx, "remote_call", "N");
  expect(invalid_call && !invalid_call->syntactically_valid,
         "call fragment is not a valid callsign");
  const auto invalid_rst = suggest_received_field_value(wpx, "rst_rx", "999");
  expect(invalid_rst && !invalid_rst->syntactically_valid,
         "RST digit ranges are validated");

  const auto& cqww = cq_ww_cw_profile();
  const auto zone = suggest_received_field_value(cqww, "cq_zone_rx", "5");
  expect(zone && !zone->syntactically_valid,
         "enumeration does not invent zero padding");
  const auto valid_zone = suggest_received_field_value(cqww, "cq_zone_rx", "05");
  expect(valid_zone && valid_zone->syntactically_valid,
         "declared contest enumeration validates");
  expect(!suggest_received_field_value(cqww, "not-a-field", "05"),
         "unknown field cannot produce a suggestion");
}

void testSuggestionAuthorityAndSafetyInvariants() {
  using namespace cwassistant::core;
  const auto provider = suggest_received_field_value(
      cq_wpx_cw_profile(), "remote_call", "N1ABC",
      ConversationSuggestionOrigin::Provider);
  expect(provider && provider->syntactically_valid &&
             provider->origin == ConversationSuggestionOrigin::Provider,
         "provider result remains visibly a suggestion");
  static_assert(ConversationMacroSafety::requires_armed_station());
  static_assert(ConversationMacroSafety::requires_exact_callsign_confirmation());
  static_assert(ConversationMacroSafety::requires_message_preview());
  static_assert(ConversationMacroSafety::requires_explicit_send_action());
  static_assert(!ConversationMacroSafety::decoder_event_may_transmit());
}

void testOrdinaryRemainsOpenEnded() {
  using namespace cwassistant::core;
  auto invalid = ordinary_cw_profile();
  invalid.completion_policy = ConversationCompletionPolicy::RequiredFields;
  expect(!validate_conversation_profile(invalid).valid(),
         "ordinary conversation cannot use contest completion");
  invalid = ordinary_cw_profile();
  invalid.received_fields.front().required_for_completion = true;
  expect(!validate_conversation_profile(invalid).valid(),
         "ordinary fields cannot gate overall completion");
  invalid = ordinary_cw_profile();
  for (auto& state : invalid.states) state.allows_free_text = false;
  expect(!validate_conversation_profile(invalid).valid(),
         "ordinary profile requires reachable free text");
}

void testRoleSafeFlowsAndMacroTokens() {
  using namespace cwassistant::core;
  const auto& profile = cq_ww_cw_profile();
  expect(profile.flows.size() == 2U &&
             profile.flows[0].local_role == ConversationRole::Runner &&
             profile.flows[1].local_role == ConversationRole::Caller,
         "contest separates runner and caller flows");
  auto invalid = profile;
  invalid.macros.front().flow_id = "caller";
  expect(!validate_conversation_profile(invalid).valid(),
         "macro cannot borrow opposite-role states");
  invalid = profile;
  invalid.macros.front().tokens.push_back(
      {.source = ConversationMacroTokenSource::StationField,
       .value = "undeclared"});
  expect(!validate_conversation_profile(invalid).valid(),
         "macro token must reference declared station field");
  invalid = profile;
  invalid.macros.front().safety.maximum_key_down_ms = 60'001;
  expect(!validate_conversation_profile(invalid).valid(),
         "macro cannot exceed hard key-down cap");
}

void testGraphAndLengthValidation() {
  using namespace cwassistant::core;
  auto invalid = ordinary_cw_profile();
  invalid.states.push_back({.id = "orphan", .flow_id = "ordinary",
                            .expected_remote_role = ConversationRole::Unknown,
                            .accepted_fields = {}, .required_fields = {},
                            .next_states = {}, .allows_free_text = false,
                            .terminal = true});
  expect(!validate_conversation_profile(invalid).valid(),
         "unreachable state is rejected");
  invalid = ordinary_cw_profile();
  invalid.states.front().next_states = {"ordinary-closed"};
  expect(!validate_conversation_profile(invalid).valid(),
         "unreachable free-text state is rejected");
  invalid = ordinary_cw_profile();
  invalid.states.back().next_states = {"ordinary-identification"};
  expect(!validate_conversation_profile(invalid).valid(),
         "terminal state cannot transition");
  invalid = ordinary_cw_profile();
  invalid.metadata.id = std::string(65, 'x');
  expect(!validate_conversation_profile(invalid).valid(),
         "oversized identifier is rejected");
}
}  // namespace

int main() {
  testReferenceProfiles();
  testTypedFieldSuggestions();
  testSuggestionAuthorityAndSafetyInvariants();
  testOrdinaryRemainsOpenEnded();
  testRoleSafeFlowsAndMacroTokens();
  testGraphAndLengthValidation();
  if (failures != 0) {
    std::cerr << failures << " conversation-profile test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All conversation-profile tests passed\n";
  return EXIT_SUCCESS;
}
