#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwassistant::core {

enum class ConversationKind { Monitoring, Ordinary, DxPileup, SpecialEvent, Contest };
enum class ConversationRole { Unknown, Initiator, Responder, Runner, Caller };
enum class ConversationFieldKind { Callsign, Rst, Serial, Enumeration, FreeText };
enum class ConversationCompletionPolicy { OperatorControlled, RequiredFields };

struct ConversationProfileMetadata {
  std::string id;
  std::string title;
  ConversationKind kind{ConversationKind::Monitoring};
  std::uint32_t revision{1};
  std::string valid_from;
  std::string valid_to;
  std::string rules_url;
};

struct ConversationFieldAlias {
  std::string observed;
  std::string normalized;
  bool per_character{false};
};

struct ConversationFieldDefinition {
  std::string id;
  ConversationFieldKind kind{ConversationFieldKind::FreeText};
  bool required_for_completion{false};
  std::size_t maximum_length{64};
  std::vector<std::string> allowed_values;
  std::vector<ConversationFieldAlias> aliases;
};

// Syntax and normalization do not confer authority. In particular, Provider
// remains a suggestion and can never represent operator acceptance or exact
// callsign confirmation; those belong to the QSO/TX guard.
enum class ConversationSuggestionOrigin { Acoustic, Context, Provider };

struct ConversationFieldSuggestion {
  std::string field_id;
  std::string observed;
  std::string normalized;
  ConversationSuggestionOrigin origin{ConversationSuggestionOrigin::Acoustic};
  bool syntactically_valid{false};
  std::string diagnostic;
};

struct ConversationFlowDefinition {
  std::string id;
  ConversationRole local_role{ConversationRole::Unknown};
  std::string initial_state;
};

struct ConversationStateDefinition {
  std::string id;
  std::string flow_id;
  ConversationRole expected_remote_role{ConversationRole::Unknown};
  std::vector<std::string> accepted_fields;
  std::vector<std::string> required_fields;
  std::vector<std::string> next_states;
  bool allows_free_text{false};
  bool terminal{false};
};

enum class ConversationMacroTokenSource { Literal, StationField, ReceivedField };

struct ConversationMacroToken {
  ConversationMacroTokenSource source{ConversationMacroTokenSource::Literal};
  std::string value;
};

// Mandatory gates are invariants, not profile-configurable flags. This type
// carries only a bounded timeout and exposes no expansion or execution API.
struct ConversationMacroSafety {
  static constexpr std::uint32_t maximum_allowed_key_down_ms = 60'000;
  std::uint32_t maximum_key_down_ms{30'000};

  [[nodiscard]] static constexpr bool requires_armed_station() noexcept {
    return true;
  }
  [[nodiscard]] static constexpr bool requires_exact_callsign_confirmation()
      noexcept {
    return true;
  }
  [[nodiscard]] static constexpr bool requires_message_preview() noexcept {
    return true;
  }
  [[nodiscard]] static constexpr bool requires_explicit_send_action() noexcept {
    return true;
  }
  [[nodiscard]] static constexpr bool decoder_event_may_transmit() noexcept {
    return false;
  }
};

struct ConversationMacroDefinition {
  std::string id;
  std::string label;
  std::string flow_id;
  std::vector<ConversationMacroToken> tokens;
  std::vector<std::string> allowed_states;
  std::vector<std::string> required_received_fields;
  ConversationMacroSafety safety;
};

struct ConversationProfile {
  ConversationProfileMetadata metadata;
  ConversationCompletionPolicy completion_policy{
      ConversationCompletionPolicy::OperatorControlled};
  std::vector<ConversationFieldDefinition> received_fields;
  std::vector<ConversationFieldDefinition> station_fields;
  std::vector<ConversationFlowDefinition> flows;
  std::vector<ConversationStateDefinition> states;
  std::vector<ConversationMacroDefinition> macros;
};

struct ConversationProfileValidation {
  std::vector<std::string> errors;
  [[nodiscard]] bool valid() const noexcept { return errors.empty(); }
};

[[nodiscard]] ConversationProfileValidation validate_conversation_profile(
    const ConversationProfile& profile);
[[nodiscard]] ConversationFieldSuggestion suggest_conversation_field_value(
    const ConversationFieldDefinition& field, std::string_view observed,
    ConversationSuggestionOrigin origin = ConversationSuggestionOrigin::Acoustic);
[[nodiscard]] std::optional<ConversationFieldSuggestion>
suggest_received_field_value(
    const ConversationProfile& profile, std::string_view field_id,
    std::string_view observed,
    ConversationSuggestionOrigin origin = ConversationSuggestionOrigin::Acoustic);

[[nodiscard]] const ConversationProfile& neutral_monitoring_profile();
[[nodiscard]] const ConversationProfile& ordinary_cw_profile();
[[nodiscard]] const ConversationProfile& cq_ww_cw_profile();
[[nodiscard]] const ConversationProfile& cq_wpx_cw_profile();
[[nodiscard]] const ConversationProfile& arrl_field_day_cw_profile();
[[nodiscard]] const ConversationProfile& arrl_sweepstakes_cw_profile();

}  // namespace cwassistant::core
