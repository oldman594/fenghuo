#include "room/protocol.hpp"

#include <limits>

namespace fenghuo::room {

namespace {

Error parse_error(std::string message) {
    return {ErrorCode::ParseError, std::move(message)};
}

Error missing(std::string field) {
    return {ErrorCode::MissingField, "missing field: " + std::move(field)};
}

Error invalid(std::string message) {
    return {ErrorCode::InvalidPayload, std::move(message)};
}

Result<std::string> optional_string(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return Result<std::string>::ok({});
    }
    if (!object.at(key).is_string()) {
        return Result<std::string>::err(invalid(std::string(key) + " must be a string"));
    }
    return Result<std::string>::ok(object.at(key).get<std::string>());
}

Result<std::string> required_string(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<std::string>::err(missing(key));
    }
    if (!object.at(key).is_string()) {
        return Result<std::string>::err(invalid(std::string(key) + " must be a string"));
    }
    auto value = object.at(key).get<std::string>();
    if (value.empty()) {
        return Result<std::string>::err(invalid(std::string(key) + " must not be empty"));
    }
    return Result<std::string>::ok(std::move(value));
}

Result<int> required_int(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<int>::err(missing(key));
    }
    if (!object.at(key).is_number_integer()) {
        return Result<int>::err(invalid(std::string(key) + " must be an integer"));
    }
    return Result<int>::ok(object.at(key).get<int>());
}

Result<std::int64_t> required_i64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<std::int64_t>::err(missing(key));
    }
    if (!object.at(key).is_number_integer()) {
        return Result<std::int64_t>::err(invalid(std::string(key) + " must be an integer"));
    }
    return Result<std::int64_t>::ok(object.at(key).get<std::int64_t>());
}

Result<std::uint64_t> required_u64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<std::uint64_t>::err(missing(key));
    }
    if (!object.at(key).is_number_unsigned()) {
        return Result<std::uint64_t>::err(invalid(std::string(key) + " must be an unsigned integer"));
    }
    return Result<std::uint64_t>::ok(object.at(key).get<std::uint64_t>());
}

Result<std::uint32_t> required_u32(const nlohmann::json& object, const char* key) {
    auto value = required_u64(object, key);
    if (!value) {
        return Result<std::uint32_t>::err(value.error());
    }
    if (value.value() > std::numeric_limits<std::uint32_t>::max()) {
        return Result<std::uint32_t>::err(invalid(std::string(key) + " is too large"));
    }
    return Result<std::uint32_t>::ok(static_cast<std::uint32_t>(value.value()));
}

Result<bool> required_bool(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<bool>::err(missing(key));
    }
    if (!object.at(key).is_boolean()) {
        return Result<bool>::err(invalid(std::string(key) + " must be a boolean"));
    }
    return Result<bool>::ok(object.at(key).get<bool>());
}

Result<void> require_object(const nlohmann::json& object, const char* name) {
    if (!object.is_object()) {
        return Result<void>::err(invalid(std::string(name) + " must be an object"));
    }
    return Result<void>::ok();
}

template <typename T>
bool assign_or_error(Result<T>& result, T& out) {
    if (!result) {
        return false;
    }
    out = std::move(result).value();
    return true;
}

Result<std::vector<RoomTeamSpec>> required_team_specs(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        return Result<std::vector<RoomTeamSpec>>::err(missing(key));
    }
    if (!object.at(key).is_array()) {
        return Result<std::vector<RoomTeamSpec>>::err(invalid(std::string(key) + " must be an array"));
    }

    std::vector<RoomTeamSpec> teams;
    for (const auto& item : object.at(key)) {
        if (auto ok = require_object(item, "team"); !ok) {
            return Result<std::vector<RoomTeamSpec>>::err(ok.error());
        }
        RoomTeamSpec team;
        auto team_id = required_string(item, "team_id");
        if (!assign_or_error(team_id, team.team_id)) {
            return Result<std::vector<RoomTeamSpec>>::err(team_id.error());
        }
        auto display_name = required_string(item, "display_name");
        if (!assign_or_error(display_name, team.display_name)) {
            return Result<std::vector<RoomTeamSpec>>::err(display_name.error());
        }
        auto max_players = required_u32(item, "max_players");
        if (!assign_or_error(max_players, team.max_players)) {
            return Result<std::vector<RoomTeamSpec>>::err(max_players.error());
        }
        teams.push_back(std::move(team));
    }
    return Result<std::vector<RoomTeamSpec>>::ok(std::move(teams));
}

Result<RoomEvent> room_created(const RoomEventEnvelope& envelope) {
    RoomCreated event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto room_code = optional_string(envelope.payload, "room_code");
    if (!assign_or_error(room_code, event.room_code)) {
        return Result<RoomEvent>::err(room_code.error());
    }
    auto name = required_string(envelope.payload, "name");
    if (!assign_or_error(name, event.name)) {
        return Result<RoomEvent>::err(name.error());
    }
    auto mode = required_string(envelope.payload, "mode");
    if (!assign_or_error(mode, event.mode)) {
        return Result<RoomEvent>::err(mode.error());
    }
    auto host_player_id = optional_string(envelope.payload, "host_player_id");
    if (!assign_or_error(host_player_id, event.host_player_id)) {
        return Result<RoomEvent>::err(host_player_id.error());
    }
    auto max_players = required_u32(envelope.payload, "max_players");
    if (!assign_or_error(max_players, event.max_players)) {
        return Result<RoomEvent>::err(max_players.error());
    }
    auto teams = required_team_specs(envelope.payload, "teams");
    if (!assign_or_error(teams, event.teams)) {
        return Result<RoomEvent>::err(teams.error());
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_player_joined(const RoomEventEnvelope& envelope) {
    RoomPlayerJoined event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<RoomEvent>::err(player_id.error());
    }
    auto display_name = required_string(envelope.payload, "display_name");
    if (!assign_or_error(display_name, event.display_name)) {
        return Result<RoomEvent>::err(display_name.error());
    }
    auto team_id = required_string(envelope.payload, "team_id");
    if (!assign_or_error(team_id, event.team_id)) {
        return Result<RoomEvent>::err(team_id.error());
    }
    auto module_id = optional_string(envelope.payload, "module_id");
    if (!assign_or_error(module_id, event.module_id)) {
        return Result<RoomEvent>::err(module_id.error());
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_player_left(const RoomEventEnvelope& envelope) {
    RoomPlayerLeft event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;
    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<RoomEvent>::err(player_id.error());
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_player_team_changed(const RoomEventEnvelope& envelope) {
    RoomPlayerTeamChanged event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;
    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<RoomEvent>::err(player_id.error());
    }
    auto team_id = required_string(envelope.payload, "team_id");
    if (!assign_or_error(team_id, event.team_id)) {
        return Result<RoomEvent>::err(team_id.error());
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_player_ready_changed(const RoomEventEnvelope& envelope) {
    RoomPlayerReadyChanged event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;
    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<RoomEvent>::err(player_id.error());
    }
    auto ready = required_bool(envelope.payload, "ready");
    if (!assign_or_error(ready, event.ready)) {
        return Result<RoomEvent>::err(ready.error());
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_started(const RoomEventEnvelope& envelope) {
    RoomStarted event;
    event.room_id = envelope.room_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;
    auto battle_id = required_string(envelope.payload, "battle_id");
    if (!assign_or_error(battle_id, event.battle_id)) {
        return Result<RoomEvent>::err(battle_id.error());
    }
    auto duration_ms = required_i64(envelope.payload, "duration_ms");
    if (!assign_or_error(duration_ms, event.duration_ms)) {
        return Result<RoomEvent>::err(duration_ms.error());
    }
    if (event.duration_ms <= 0) {
        return Result<RoomEvent>::err(invalid("duration_ms must be positive"));
    }
    return Result<RoomEvent>::ok(event);
}

Result<RoomEvent> room_ended(const RoomEventEnvelope& envelope) {
    return Result<RoomEvent>::ok(
        RoomEnded{envelope.room_id, envelope.event_id, envelope.occurred_at_ms});
}

Result<RoomEvent> room_closed(const RoomEventEnvelope& envelope) {
    return Result<RoomEvent>::ok(
        RoomClosed{envelope.room_id, envelope.event_id, envelope.occurred_at_ms});
}

} // namespace

Result<RoomEventEnvelope> parse_room_event_json(std::string_view json_text) {
    nlohmann::json object;
    try {
        object = nlohmann::json::parse(json_text.begin(), json_text.end());
    } catch (const nlohmann::json::parse_error& error) {
        return Result<RoomEventEnvelope>::err(parse_error(error.what()));
    }

    if (auto ok = require_object(object, "room event envelope"); !ok) {
        return Result<RoomEventEnvelope>::err(ok.error());
    }

    RoomEventEnvelope envelope;
    envelope.raw = object;

    auto schema = required_int(object, "schema_version");
    if (!assign_or_error(schema, envelope.schema_version)) {
        return Result<RoomEventEnvelope>::err(schema.error());
    }
    if (envelope.schema_version != kRoomSchemaVersion) {
        return Result<RoomEventEnvelope>::err(
            {ErrorCode::UnsupportedSchema, "unsupported schema_version"});
    }

    auto event_id = required_string(object, "event_id");
    if (!assign_or_error(event_id, envelope.event_id)) {
        return Result<RoomEventEnvelope>::err(event_id.error());
    }
    auto event_type = required_string(object, "event_type");
    if (!assign_or_error(event_type, envelope.event_type)) {
        return Result<RoomEventEnvelope>::err(event_type.error());
    }
    auto room_id = required_string(object, "room_id");
    if (!assign_or_error(room_id, envelope.room_id)) {
        return Result<RoomEventEnvelope>::err(room_id.error());
    }
    auto source_id = required_string(object, "source_id");
    if (!assign_or_error(source_id, envelope.source_id)) {
        return Result<RoomEventEnvelope>::err(source_id.error());
    }
    auto sequence = required_u64(object, "sequence");
    if (!assign_or_error(sequence, envelope.sequence)) {
        return Result<RoomEventEnvelope>::err(sequence.error());
    }
    auto occurred_at = required_i64(object, "occurred_at_ms");
    if (!assign_or_error(occurred_at, envelope.occurred_at_ms)) {
        return Result<RoomEventEnvelope>::err(occurred_at.error());
    }

    if (!object.contains("payload")) {
        return Result<RoomEventEnvelope>::err(missing("payload"));
    }
    envelope.payload = object.at("payload");
    if (auto ok = require_object(envelope.payload, "payload"); !ok) {
        return Result<RoomEventEnvelope>::err(ok.error());
    }

    auto room_event = to_room_event(envelope);
    if (!room_event) {
        return Result<RoomEventEnvelope>::err(room_event.error());
    }

    return Result<RoomEventEnvelope>::ok(std::move(envelope));
}

Result<RoomEvent> to_room_event(const RoomEventEnvelope& envelope) {
    if (envelope.event_type == "room_created") {
        return room_created(envelope);
    }
    if (envelope.event_type == "room_player_joined") {
        return room_player_joined(envelope);
    }
    if (envelope.event_type == "room_player_left") {
        return room_player_left(envelope);
    }
    if (envelope.event_type == "room_player_team_changed") {
        return room_player_team_changed(envelope);
    }
    if (envelope.event_type == "room_player_ready_changed") {
        return room_player_ready_changed(envelope);
    }
    if (envelope.event_type == "room_started") {
        return room_started(envelope);
    }
    if (envelope.event_type == "room_ended") {
        return room_ended(envelope);
    }
    if (envelope.event_type == "room_closed") {
        return room_closed(envelope);
    }
    return Result<RoomEvent>::err(
        {ErrorCode::UnknownEventType, "unknown event_type: " + envelope.event_type});
}

nlohmann::json to_json(const RoomEventEnvelope& envelope) {
    return {
        {"schema_version", envelope.schema_version},
        {"event_id", envelope.event_id},
        {"event_type", envelope.event_type},
        {"room_id", envelope.room_id},
        {"source_id", envelope.source_id},
        {"sequence", envelope.sequence},
        {"occurred_at_ms", envelope.occurred_at_ms},
        {"payload", envelope.payload},
    };
}

nlohmann::json to_json(const RoomSnapshot& snapshot) {
    nlohmann::json teams = nlohmann::json::object();
    for (const auto& [team_id, team] : snapshot.teams) {
        teams[team_id] = {
            {"team_id", team.team_id},
            {"display_name", team.display_name},
            {"max_players", team.max_players},
        };
    }

    nlohmann::json players = nlohmann::json::object();
    for (const auto& [player_id, player] : snapshot.players) {
        players[player_id] = {
            {"player_id", player.player_id},
            {"display_name", player.display_name},
            {"team_id", player.team_id},
            {"module_id", player.module_id},
            {"ready", player.ready},
            {"joined_at_ms", player.joined_at_ms},
        };
    }

    nlohmann::json output = {
        {"room_id", snapshot.room_id},
        {"room_code", snapshot.room_code},
        {"name", snapshot.name},
        {"mode", snapshot.mode},
        {"phase", to_string(snapshot.phase)},
        {"created_at_ms", snapshot.created_at_ms},
        {"updated_at_ms", snapshot.updated_at_ms},
        {"host_player_id", snapshot.host_player_id},
        {"max_players", snapshot.max_players},
        {"teams", std::move(teams)},
        {"players", std::move(players)},
        {"latest_room_event_id", snapshot.latest_room_event_id.value_or("")},
    };
    if (snapshot.battle_id) {
        output["battle_id"] = *snapshot.battle_id;
    }
    return output;
}

std::string canonical_json(const RoomEventEnvelope& envelope) {
    return to_json(envelope).dump();
}

} // namespace fenghuo::room
