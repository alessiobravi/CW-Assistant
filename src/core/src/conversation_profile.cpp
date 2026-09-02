#include "cwassistant/core/conversation_profile.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "cwassistant/core/callsign_policy.hpp"

namespace cwassistant::core {
namespace {

constexpr std::size_t kMaximumFields = 64;
constexpr std::size_t kMaximumFlows = 8;
constexpr std::size_t kMaximumStates = 96;
constexpr std::size_t kMaximumMacros = 64;
constexpr std::size_t kMaximumEdgesPerState = 16;
constexpr std::size_t kMaximumTokensPerMacro = 64;
constexpr std::size_t kMaximumValuesPerField = 256;
constexpr std::size_t kMaximumAliasesPerField = 32;
constexpr std::size_t kMaximumIdentifierLength = 64;
constexpr std::size_t kMaximumTitleLength = 160;
constexpr std::size_t kMaximumLiteralLength = 256;

std::string trim(const std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string{value.substr(first, last - first + 1U)};
}

std::string uppercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

bool identifierValid(const std::string_view value) {
  if (value.empty() || value.size() > kMaximumIdentifierLength) return false;
  return std::all_of(value.begin(), value.end(), [](const unsigned char value) {
    return std::isalnum(value) != 0 || value == '-' || value == '_';
  });
}

ConversationFieldDefinition field(
    std::string id, const ConversationFieldKind kind,
    const bool required_for_completion = false,
    const std::size_t maximum_length = 64,
    std::vector<std::string> allowed_values = {},
    std::vector<ConversationFieldAlias> aliases = {}) {
  return {.id = std::move(id),
          .kind = kind,
          .required_for_completion = required_for_completion,
          .maximum_length = maximum_length,
          .allowed_values = std::move(allowed_values),
          .aliases = std::move(aliases)};
}

ConversationMacroToken literal(std::string value) {
  return {.source = ConversationMacroTokenSource::Literal,
          .value = std::move(value)};
}
ConversationMacroToken station(std::string id) {
  return {.source = ConversationMacroTokenSource::StationField,
          .value = std::move(id)};
}
ConversationMacroToken received(std::string id) {
  return {.source = ConversationMacroTokenSource::ReceivedField,
          .value = std::move(id)};
}

ConversationMacroDefinition macro(
    std::string id, std::string label, std::string flow_id,
    std::vector<ConversationMacroToken> tokens,
    std::vector<std::string> allowed_states,
    std::vector<std::string> required_received_fields) {
  return {.id = std::move(id),
          .label = std::move(label),
          .flow_id = std::move(flow_id),
          .tokens = std::move(tokens),
          .allowed_states = std::move(allowed_states),
          .required_received_fields = std::move(required_received_fields),
          .safety = {}};
}

std::vector<std::string> cqZones() {
  std::vector<std::string> result;
  result.reserve(40);
  for (int zone = 1; zone <= 40; ++zone) {
    result.push_back(zone < 10 ? "0" + std::to_string(zone)
                              : std::to_string(zone));
  }
  return result;
}

std::vector<ConversationFieldDefinition> commonStationFields() {
  return {field("my_call", ConversationFieldKind::Callsign, false, 16),
          field("rst_tx", ConversationFieldKind::Rst, false, 3, {},
                {{"5NN", "599", false}})};
}

ConversationProfile contestProfile(
    ConversationProfileMetadata metadata,
    std::vector<ConversationFieldDefinition> received_exchange_fields,
    std::vector<ConversationFieldDefinition> station_exchange_fields,
    std::vector<ConversationMacroToken> runner_exchange_tokens,
    std::vector<ConversationMacroToken> caller_exchange_tokens) {
  std::vector<ConversationFieldDefinition> received_fields{
      field("remote_call", ConversationFieldKind::Callsign, true, 16),
      field("addressed_call", ConversationFieldKind::Callsign, false, 16)};
  received_fields.insert(received_fields.end(),
                         std::make_move_iterator(received_exchange_fields.begin()),
                         std::make_move_iterator(received_exchange_fields.end()));
  auto station_fields = commonStationFields();
  station_fields.insert(station_fields.end(),
                        std::make_move_iterator(station_exchange_fields.begin()),
                        std::make_move_iterator(station_exchange_fields.end()));

  std::vector<std::string> exchange_ids;
  for (const auto& definition : received_fields) {
    if (definition.id != "remote_call" && definition.required_for_completion) {
      exchange_ids.push_back(definition.id);
    }
  }
  auto caller_response_ids = exchange_ids;
  caller_response_ids.insert(caller_response_ids.begin(), "remote_call");
  auto runner_response_ids = exchange_ids;
  runner_response_ids.insert(runner_response_ids.begin(), "addressed_call");

  return {
      .metadata = std::move(metadata),
      .completion_policy = ConversationCompletionPolicy::RequiredFields,
      .received_fields = std::move(received_fields),
      .station_fields = std::move(station_fields),
      .flows = {{.id = "runner",
                 .local_role = ConversationRole::Runner,
                 .initial_state = "runner-wait-call"},
                {.id = "caller",
                 .local_role = ConversationRole::Caller,
                 .initial_state = "caller-wait-solicitation"}},
      .states = {
          {.id = "runner-wait-call",
           .flow_id = "runner",
           .expected_remote_role = ConversationRole::Caller,
           .accepted_fields = {"remote_call"},
           .required_fields = {"remote_call"},
           .next_states = {"runner-wait-exchange", "runner-closed"}},
          {.id = "runner-wait-exchange",
           .flow_id = "runner",
           .expected_remote_role = ConversationRole::Caller,
           .accepted_fields = caller_response_ids,
           .required_fields = exchange_ids,
           .next_states = {"runner-wait-call", "runner-wait-exchange",
                           "runner-closed"}},
          {.id = "runner-closed",
           .flow_id = "runner",
           .expected_remote_role = ConversationRole::Unknown,
           .accepted_fields = {},
           .required_fields = {},
           .next_states = {},
           .terminal = true},
          {.id = "caller-wait-solicitation",
           .flow_id = "caller",
           .expected_remote_role = ConversationRole::Runner,
           .accepted_fields = {"remote_call"},
           .required_fields = {"remote_call"},
           .next_states = {"caller-wait-exchange", "caller-closed"}},
          {.id = "caller-wait-exchange",
           .flow_id = "caller",
           .expected_remote_role = ConversationRole::Runner,
           .accepted_fields = runner_response_ids,
           .required_fields = exchange_ids,
           .next_states = {"caller-wait-ack", "caller-wait-exchange",
                           "caller-closed"}},
          {.id = "caller-wait-ack",
           .flow_id = "caller",
           .expected_remote_role = ConversationRole::Runner,
           .accepted_fields = {"remote_call"},
           .required_fields = {},
           .next_states = {"caller-closed"}},
          {.id = "caller-closed",
           .flow_id = "caller",
           .expected_remote_role = ConversationRole::Unknown,
           .accepted_fields = {},
           .required_fields = {},
           .next_states = {},
           .terminal = true}},
      .macros = {
          macro("runner-exchange", "Send runner exchange", "runner",
                std::move(runner_exchange_tokens), {"runner-wait-call"},
                {"remote_call"}),
          macro("runner-acknowledge", "Acknowledge and continue", "runner",
                {literal("TU"), station("my_call")},
                {"runner-wait-exchange"}, exchange_ids),
          macro("caller-identify", "Send caller callsign", "caller",
                {station("my_call")}, {"caller-wait-solicitation"},
                {"remote_call"}),
          macro("caller-exchange", "Send caller exchange", "caller",
                std::move(caller_exchange_tokens), {"caller-wait-exchange"},
                exchange_ids)}};
}

void addError(ConversationProfileValidation& result, std::string message) {
  result.errors.push_back(std::move(message));
}

void validateFieldSet(const std::vector<ConversationFieldDefinition>& fields,
                      const std::string_view set_name,
                      std::unordered_set<std::string>& ids,
                      ConversationProfileValidation& result) {
  if (fields.size() > kMaximumFields) {
    addError(result, std::string{set_name} + " field count exceeds limit");
  }
  for (const auto& definition : fields) {
    if (!identifierValid(definition.id) || !ids.insert(definition.id).second) {
      addError(result, std::string{set_name} +
                           " field ids must be valid and unique");
    }
    if (definition.maximum_length == 0U ||
        definition.maximum_length > kMaximumLiteralLength) {
      addError(result, "field maximum length is outside the safe range");
    }
    if (definition.kind == ConversationFieldKind::Enumeration &&
        definition.allowed_values.empty()) {
      addError(result, "enumeration field requires allowed values");
    }
    if (definition.allowed_values.size() > kMaximumValuesPerField ||
        definition.aliases.size() > kMaximumAliasesPerField) {
      addError(result, "field values or aliases exceed limits");
    }
    std::unordered_set<std::string> allowed;
    for (const auto& value : definition.allowed_values) {
      if (value.empty() || value.size() > definition.maximum_length ||
          !allowed.insert(uppercase(value)).second) {
        addError(result, "field allowed values must be bounded and unique");
      }
    }
    std::unordered_set<std::string> aliases;
    for (const auto& alias : definition.aliases) {
      const auto observed = uppercase(alias.observed);
      if (observed.empty() || alias.normalized.empty() ||
          observed.size() > definition.maximum_length ||
          alias.normalized.size() > definition.maximum_length ||
          !aliases.insert(observed).second ||
          (alias.per_character &&
           (observed.size() != 1U || alias.normalized.size() != 1U))) {
        addError(result, "field aliases must be bounded and unique");
      }
    }
  }
}

}  // namespace

ConversationFieldSuggestion suggest_conversation_field_value(
    const ConversationFieldDefinition& field, const std::string_view observed,
    const ConversationSuggestionOrigin origin) {
  ConversationFieldSuggestion result{.field_id = field.id,
                                     .observed = {},
                                     .normalized = {},
                                     .origin = origin,
                                     .syntactically_valid = false,
                                     .diagnostic = {}};
  if (observed.size() > kMaximumLiteralLength) {
    result.diagnostic = "observed value exceeds processing limit";
    return result;
  }
  result.observed = std::string{observed};
  result.normalized = uppercase(trim(observed));
  if (result.normalized.empty()) {
    result.diagnostic = "value is empty";
    return result;
  }
  for (const auto& alias : field.aliases) {
    if (!alias.per_character && uppercase(alias.observed) == result.normalized) {
      result.normalized = uppercase(alias.normalized);
      break;
    }
  }
  for (char& character : result.normalized) {
    for (const auto& alias : field.aliases) {
      if (alias.per_character && alias.observed.size() == 1U &&
          static_cast<char>(std::toupper(static_cast<unsigned char>(
              alias.observed.front()))) == character) {
        character = static_cast<char>(std::toupper(
            static_cast<unsigned char>(alias.normalized.front())));
        break;
      }
    }
  }
  if (result.normalized.size() > field.maximum_length) {
    result.diagnostic = "value exceeds field length";
    return result;
  }
  switch (field.kind) {
    case ConversationFieldKind::Callsign: {
      const auto call = CallsignPolicy::normalize(result.normalized);
      if (!call) {
        result.diagnostic = "invalid callsign syntax";
        return result;
      }
      result.normalized = *call;
      break;
    }
    case ConversationFieldKind::Rst:
      if (result.normalized.size() != 3U ||
          result.normalized[0] < '1' || result.normalized[0] > '5' ||
          result.normalized[1] < '1' || result.normalized[1] > '9' ||
          result.normalized[2] < '1' || result.normalized[2] > '9') {
        result.diagnostic = "RST must contain valid R, S, and T digits";
        return result;
      }
      break;
    case ConversationFieldKind::Serial:
      if (!std::all_of(result.normalized.begin(), result.normalized.end(),
                       [](const unsigned char character) {
                         return std::isdigit(character) != 0;
                       })) {
        result.diagnostic = "serial must contain digits or declared cut numbers";
        return result;
      }
      break;
    case ConversationFieldKind::Enumeration: {
      const auto match = std::find_if(
          field.allowed_values.begin(), field.allowed_values.end(),
          [&result](const std::string& value) {
            return uppercase(value) == result.normalized;
          });
      if (match == field.allowed_values.end()) {
        result.diagnostic = "value is not in the field enumeration";
        return result;
      }
      result.normalized = uppercase(*match);
      break;
    }
    case ConversationFieldKind::FreeText:
      if (std::any_of(result.normalized.begin(), result.normalized.end(),
                      [](const unsigned char character) {
                        return std::iscntrl(character) != 0;
                      })) {
        result.diagnostic = "free text contains a control character";
        return result;
      }
      break;
  }
  result.syntactically_valid = true;
  return result;
}

std::optional<ConversationFieldSuggestion> suggest_received_field_value(
    const ConversationProfile& profile, const std::string_view field_id,
    const std::string_view observed, const ConversationSuggestionOrigin origin) {
  const auto definition = std::find_if(
      profile.received_fields.begin(), profile.received_fields.end(),
      [field_id](const ConversationFieldDefinition& field) {
        return field.id == field_id;
      });
  if (definition == profile.received_fields.end()) return std::nullopt;
  return suggest_conversation_field_value(*definition, observed, origin);
}

ConversationProfileValidation validate_conversation_profile(
    const ConversationProfile& profile) {
  ConversationProfileValidation result;
  if (!identifierValid(profile.metadata.id)) addError(result, "invalid profile id");
  if (profile.metadata.title.empty() ||
      profile.metadata.title.size() > kMaximumTitleLength) {
    addError(result, "profile title is required and bounded");
  }
  if (profile.metadata.revision == 0U) addError(result, "revision must be positive");
  if (profile.metadata.valid_from.size() > 32U ||
      profile.metadata.valid_to.size() > 32U) {
    addError(result, "profile validity metadata exceeds limits");
  }
  if (profile.metadata.rules_url.size() > kMaximumLiteralLength) {
    addError(result, "profile rules URL exceeds limits");
  }
  if (profile.metadata.kind == ConversationKind::Contest) {
    if (profile.metadata.rules_url.empty() ||
        profile.metadata.rules_url.size() > kMaximumLiteralLength ||
        !profile.metadata.rules_url.starts_with("https://")) {
      addError(result, "contest profile requires a bounded rules URL");
    }
    if (profile.completion_policy != ConversationCompletionPolicy::RequiredFields) {
      addError(result, "contest profile requires rules-defined completion");
    }
  } else if (profile.completion_policy !=
             ConversationCompletionPolicy::OperatorControlled) {
    addError(result, "non-contest completion must remain operator-controlled");
  }

  std::unordered_set<std::string> received_ids;
  std::unordered_set<std::string> station_ids;
  validateFieldSet(profile.received_fields, "received", received_ids, result);
  validateFieldSet(profile.station_fields, "station", station_ids, result);
  for (const auto& definition : profile.station_fields) {
    if (definition.required_for_completion) {
      addError(result, "station fields cannot satisfy received completion");
    }
  }
  for (const auto& id : received_ids) {
    if (station_ids.contains(id)) {
      addError(result, "station and received field ids overlap");
    }
  }
  if (profile.metadata.kind != ConversationKind::Contest) {
    for (const auto& definition : profile.received_fields) {
      if (definition.required_for_completion) {
        addError(result, "non-contest fields cannot gate overall completion");
      }
    }
  }

  if (profile.flows.empty() || profile.flows.size() > kMaximumFlows) {
    addError(result, "profile flow count is outside limits");
  }
  std::unordered_map<std::string, const ConversationFlowDefinition*> flows;
  bool has_runner_flow = false;
  bool has_caller_flow = false;
  for (const auto& flow : profile.flows) {
    if (!identifierValid(flow.id) || !flows.emplace(flow.id, &flow).second ||
        !identifierValid(flow.initial_state)) {
      addError(result, "flow ids and initial states must be valid and unique");
    }
    has_runner_flow = has_runner_flow || flow.local_role == ConversationRole::Runner;
    has_caller_flow = has_caller_flow || flow.local_role == ConversationRole::Caller;
    if (profile.metadata.kind == ConversationKind::Contest &&
        flow.local_role != ConversationRole::Runner &&
        flow.local_role != ConversationRole::Caller) {
      addError(result, "contest flow requires an explicit local role");
    }
  }
  if (profile.metadata.kind == ConversationKind::Contest &&
      (!has_runner_flow || !has_caller_flow)) {
    addError(result, "contest profile requires runner and caller flows");
  }

  if (profile.states.empty() || profile.states.size() > kMaximumStates) {
    addError(result, "profile state count is outside limits");
  }
  std::unordered_map<std::string, const ConversationStateDefinition*> states;
  for (const auto& state : profile.states) {
    if (!identifierValid(state.id) || !states.emplace(state.id, &state).second) {
      addError(result, "state ids must be valid and unique");
    }
    if (!flows.contains(state.flow_id)) addError(result, "state references unknown flow");
    if (state.next_states.size() > kMaximumEdgesPerState) {
      addError(result, "state transition count exceeds limit");
    }
    if (state.accepted_fields.size() > kMaximumFields ||
        state.required_fields.size() > kMaximumFields) {
      addError(result, "state field references exceed limits");
    }
    if (state.terminal && !state.next_states.empty()) {
      addError(result, "terminal state cannot have transitions");
    }
    if (!state.terminal && state.next_states.empty()) {
      addError(result, "non-terminal state requires a transition");
    }
    for (const auto& id : state.accepted_fields) {
      if (!received_ids.contains(id)) addError(result, "state accepts unknown field");
    }
    for (const auto& id : state.required_fields) {
      if (!received_ids.contains(id) ||
          std::find(state.accepted_fields.begin(), state.accepted_fields.end(), id) ==
              state.accepted_fields.end()) {
        addError(result, "state requires a field it does not accept");
      }
    }
  }
  for (const auto& state : profile.states) {
    for (const auto& next_id : state.next_states) {
      const auto next = states.find(next_id);
      if (next == states.end()) {
        addError(result, "state references unknown transition");
      } else if (next->second->flow_id != state.flow_id) {
        addError(result, "state transition crosses role flow");
      }
    }
  }

  bool reachable_free_text = false;
  std::unordered_set<std::string> all_reachable;
  for (const auto& [flow_id, flow] : flows) {
    const auto initial = states.find(flow->initial_state);
    if (initial == states.end() || initial->second->flow_id != flow_id) {
      addError(result, "flow initial state is missing or belongs to another flow");
      continue;
    }
    std::vector<std::string> frontier{flow->initial_state};
    std::unordered_set<std::string> reachable;
    bool terminal_reachable = false;
    while (!frontier.empty()) {
      auto id = std::move(frontier.back());
      frontier.pop_back();
      if (!reachable.insert(id).second) continue;
      all_reachable.insert(id);
      const auto current = states.find(id);
      if (current == states.end()) continue;
      terminal_reachable = terminal_reachable || current->second->terminal;
      reachable_free_text = reachable_free_text || current->second->allows_free_text;
      frontier.insert(frontier.end(), current->second->next_states.begin(),
                      current->second->next_states.end());
    }
    if (!terminal_reachable) addError(result, "flow has no reachable terminal state");
  }
  if (all_reachable.size() != states.size()) {
    addError(result, "profile contains unreachable states");
  }
  if (profile.metadata.kind == ConversationKind::Ordinary && !reachable_free_text) {
    addError(result, "ordinary QSO requires reachable free-text conversation");
  }

  if (profile.macros.size() > kMaximumMacros) addError(result, "macro count exceeds limit");
  if (profile.metadata.kind == ConversationKind::Monitoring &&
      !profile.macros.empty()) {
    addError(result, "neutral monitoring cannot declare transmit macros");
  }
  std::unordered_set<std::string> macro_ids;
  for (const auto& definition : profile.macros) {
    if (!identifierValid(definition.id) || !macro_ids.insert(definition.id).second ||
        definition.label.empty() || definition.label.size() > kMaximumTitleLength) {
      addError(result, "macro id and label must be valid and unique");
    }
    if (!flows.contains(definition.flow_id)) addError(result, "macro references unknown flow");
    if (definition.tokens.empty() || definition.tokens.size() > kMaximumTokensPerMacro) {
      addError(result, "macro token count is outside limits");
    }
    if (definition.allowed_states.empty()) {
      addError(result, "macro requires at least one role-bound state");
    }
    if (definition.allowed_states.size() > kMaximumStates ||
        definition.required_received_fields.size() > kMaximumFields) {
      addError(result, "macro state or field references exceed limits");
    }
    for (const auto& token : definition.tokens) {
      if (token.value.empty() || token.value.size() > kMaximumLiteralLength) {
        addError(result, "macro token is empty or too long");
      } else if (token.source == ConversationMacroTokenSource::StationField &&
                 !station_ids.contains(token.value)) {
        addError(result, "macro references unknown station field");
      } else if (token.source == ConversationMacroTokenSource::ReceivedField &&
                 !received_ids.contains(token.value)) {
        addError(result, "macro references unknown received field");
      }
    }
    for (const auto& state_id : definition.allowed_states) {
      const auto state = states.find(state_id);
      if (state == states.end() || state->second->flow_id != definition.flow_id) {
        addError(result, "macro state is missing or belongs to another flow");
      }
    }
    for (const auto& field_id : definition.required_received_fields) {
      if (!received_ids.contains(field_id)) {
        addError(result, "macro requires unknown received field");
      }
    }
    if (definition.safety.maximum_key_down_ms == 0U ||
        definition.safety.maximum_key_down_ms >
            ConversationMacroSafety::maximum_allowed_key_down_ms) {
      addError(result, "macro key-down timeout is outside mandatory bounds");
    }
  }
  return result;
}

const ConversationProfile& neutral_monitoring_profile() {
  static const ConversationProfile profile{
      .metadata = {.id = "neutral-monitoring",
                   .title = "Neutral receive-only monitoring",
                   .kind = ConversationKind::Monitoring,
                   .revision = 1,
                   .valid_from = {}, .valid_to = {}, .rules_url = {}},
      .completion_policy = ConversationCompletionPolicy::OperatorControlled,
      .received_fields = {field("decoded_text", ConversationFieldKind::FreeText)},
      .station_fields = {},
      .flows = {{.id = "monitor", .local_role = ConversationRole::Unknown,
                 .initial_state = "monitoring"}},
      .states = {{.id = "monitoring", .flow_id = "monitor",
                  .expected_remote_role = ConversationRole::Unknown,
                  .accepted_fields = {"decoded_text"}, .required_fields = {},
                  .next_states = {"monitoring", "monitoring-closed"},
                  .allows_free_text = true},
                 {.id = "monitoring-closed", .flow_id = "monitor",
                  .expected_remote_role = ConversationRole::Unknown,
                  .accepted_fields = {}, .required_fields = {}, .next_states = {},
                  .allows_free_text = false, .terminal = true}},
      .macros = {}};
  return profile;
}

const ConversationProfile& ordinary_cw_profile() {
  static const ConversationProfile profile{
      .metadata = {.id = "ordinary-cw", .title = "Ordinary CW conversation",
                   .kind = ConversationKind::Ordinary, .revision = 1,
                   .valid_from = {}, .valid_to = {}, .rules_url = {}},
      .completion_policy = ConversationCompletionPolicy::OperatorControlled,
      .received_fields = {
          field("remote_call", ConversationFieldKind::Callsign, false, 16),
          field("rst_rx", ConversationFieldKind::Rst, false, 3, {},
                {{"5NN", "599", false}}),
          field("name", ConversationFieldKind::FreeText, false, 64),
          field("qth", ConversationFieldKind::FreeText, false, 96),
          field("conversation", ConversationFieldKind::FreeText, false, 256)},
      .station_fields = commonStationFields(),
      .flows = {{.id = "ordinary", .local_role = ConversationRole::Unknown,
                 .initial_state = "ordinary-identification"}},
      .states = {
          {.id = "ordinary-identification", .flow_id = "ordinary",
           .expected_remote_role = ConversationRole::Unknown,
           .accepted_fields = {"remote_call", "rst_rx", "name", "qth"},
           .required_fields = {},
           .next_states = {"ordinary-identification", "ordinary-conversation",
                           "ordinary-closed"}},
          {.id = "ordinary-conversation", .flow_id = "ordinary",
           .expected_remote_role = ConversationRole::Unknown,
           .accepted_fields = {"rst_rx", "name", "qth", "conversation"},
           .required_fields = {},
           .next_states = {"ordinary-identification", "ordinary-conversation",
                           "ordinary-closed"},
           .allows_free_text = true},
          {.id = "ordinary-closed", .flow_id = "ordinary",
           .expected_remote_role = ConversationRole::Unknown,
           .accepted_fields = {}, .required_fields = {}, .next_states = {},
           .allows_free_text = false, .terminal = true}},
      .macros = {macro(
          "ordinary-reply", "Reply to station", "ordinary",
          {received("remote_call"), literal("DE"), station("my_call"),
           literal("UR RST"), station("rst_tx"), literal("K")},
          {"ordinary-identification", "ordinary-conversation"},
          {"remote_call"})}};
  return profile;
}

const ConversationProfile& cq_ww_cw_profile() {
  static const ConversationProfile profile = contestProfile(
      {.id = "cq-ww-cw", .title = "CQ World Wide DX Contest — CW",
       .kind = ConversationKind::Contest, .revision = 1,
       .valid_from = {}, .valid_to = {},
       .rules_url = "https://cqww.com/rules.htm"},
      {field("rst_rx", ConversationFieldKind::Rst, true, 3, {},
             {{"5NN", "599", false}}),
       field("cq_zone_rx", ConversationFieldKind::Enumeration, true, 2,
             cqZones())},
      {field("cq_zone_tx", ConversationFieldKind::Enumeration, false, 2,
             cqZones())},
      {received("remote_call"), station("rst_tx"), station("cq_zone_tx")},
      {station("rst_tx"), station("cq_zone_tx")});
  return profile;
}

const ConversationProfile& cq_wpx_cw_profile() {
  static const auto cut_numbers = std::vector<ConversationFieldAlias>{
      {"T", "0", true}, {"N", "9", true}};
  static const ConversationProfile profile = contestProfile(
      {.id = "cq-wpx-cw", .title = "CQ World Wide WPX Contest — CW",
       .kind = ConversationKind::Contest, .revision = 1,
       .valid_from = {}, .valid_to = {},
       .rules_url = "https://cqwpx.com/rules/"},
      {field("rst_rx", ConversationFieldKind::Rst, true, 3, {},
             {{"5NN", "599", false}}),
       field("serial_rx", ConversationFieldKind::Serial, true, 6, {},
             cut_numbers)},
      {field("serial_tx", ConversationFieldKind::Serial, false, 6)},
      {received("remote_call"), station("rst_tx"), station("serial_tx")},
      {station("rst_tx"), station("serial_tx")});
  return profile;
}

const ConversationProfile& arrl_field_day_cw_profile() {
  static const ConversationProfile profile = contestProfile(
      {.id = "arrl-field-day-cw", .title = "ARRL Field Day — CW",
       .kind = ConversationKind::Contest, .revision = 1,
       .valid_from = {}, .valid_to = {},
       .rules_url =
           "https://contests.arrl.org/ContestRules/Field-Day-Rules.pdf"},
      {field("class_rx", ConversationFieldKind::FreeText, true, 4),
       field("section_rx", ConversationFieldKind::FreeText, true, 4)},
      {field("class_tx", ConversationFieldKind::FreeText, false, 4),
       field("section_tx", ConversationFieldKind::FreeText, false, 4)},
      {received("remote_call"), station("class_tx"), station("section_tx")},
      {station("class_tx"), station("section_tx")});
  return profile;
}

const ConversationProfile& arrl_sweepstakes_cw_profile() {
  static const auto cut_numbers = std::vector<ConversationFieldAlias>{
      {"T", "0", true}, {"N", "9", true}};
  static const auto precedence =
      std::vector<std::string>{"Q", "A", "B", "U", "M", "S"};
  static const ConversationProfile profile = contestProfile(
      {.id = "arrl-sweepstakes-cw",
       .title = "ARRL November Sweepstakes — CW",
       .kind = ConversationKind::Contest, .revision = 1,
       .valid_from = {}, .valid_to = {},
       .rules_url = "https://www.arrl.org/sweepstakes"},
      {field("serial_rx", ConversationFieldKind::Serial, true, 6, {},
             cut_numbers),
       field("precedence_rx", ConversationFieldKind::Enumeration, true, 1,
             precedence),
       field("exchange_call_rx", ConversationFieldKind::Callsign, true, 16),
       field("check_rx", ConversationFieldKind::Serial, true, 2, {},
             cut_numbers),
       field("section_rx", ConversationFieldKind::FreeText, true, 4)},
      {field("serial_tx", ConversationFieldKind::Serial, false, 6),
       field("precedence_tx", ConversationFieldKind::Enumeration, false, 1,
             precedence),
       field("check_tx", ConversationFieldKind::Serial, false, 2),
       field("section_tx", ConversationFieldKind::FreeText, false, 4)},
      {received("remote_call"), station("serial_tx"),
       station("precedence_tx"), station("my_call"), station("check_tx"),
       station("section_tx")},
      {station("serial_tx"), station("precedence_tx"), station("my_call"),
       station("check_tx"), station("section_tx")});
  return profile;
}

}  // namespace cwassistant::core
