#include "protocol/event.hpp"

#include <limits>

namespace fenghuo::protocol {

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

Result<core::BattleEvent> player_joined(const EventEnvelope& envelope) {
    core::PlayerJoined event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<core::BattleEvent>::err(player_id.error());
    }
    auto display_name = required_string(envelope.payload, "display_name");
    if (!assign_or_error(display_name, event.display_name)) {
        return Result<core::BattleEvent>::err(display_name.error());
    }
    auto team_id = required_string(envelope.payload, "team_id");
    if (!assign_or_error(team_id, event.team_id)) {
        return Result<core::BattleEvent>::err(team_id.error());
    }
    auto module_id = required_string(envelope.payload, "module_id");
    if (!assign_or_error(module_id, event.module_id)) {
        return Result<core::BattleEvent>::err(module_id.error());
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> battle_started(const EventEnvelope& envelope) {
    core::BattleStarted event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto mode = required_string(envelope.payload, "mode");
    if (!assign_or_error(mode, event.mode)) {
        return Result<core::BattleEvent>::err(mode.error());
    }
    auto duration = required_i64(envelope.payload, "duration_ms");
    if (!assign_or_error(duration, event.duration_ms)) {
        return Result<core::BattleEvent>::err(duration.error());
    }
    if (event.duration_ms <= 0) {
        return Result<core::BattleEvent>::err(invalid("duration_ms must be positive"));
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> shot(const EventEnvelope& envelope) {
    core::Shot event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<core::BattleEvent>::err(player_id.error());
    }
    auto weapon_id = required_string(envelope.payload, "weapon_id");
    if (!assign_or_error(weapon_id, event.weapon_id)) {
        return Result<core::BattleEvent>::err(weapon_id.error());
    }
    auto ammo_after = required_int(envelope.payload, "ammo_after");
    if (!assign_or_error(ammo_after, event.ammo_after)) {
        return Result<core::BattleEvent>::err(ammo_after.error());
    }
    if (event.ammo_after < 0) {
        return Result<core::BattleEvent>::err(invalid("ammo_after must not be negative"));
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> hit(const EventEnvelope& envelope) {
    core::Hit event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto attacker = required_string(envelope.payload, "attacker_player_id");
    if (!assign_or_error(attacker, event.attacker_player_id)) {
        return Result<core::BattleEvent>::err(attacker.error());
    }
    auto target = required_string(envelope.payload, "target_player_id");
    if (!assign_or_error(target, event.target_player_id)) {
        return Result<core::BattleEvent>::err(target.error());
    }
    auto weapon_id = required_string(envelope.payload, "weapon_id");
    if (!assign_or_error(weapon_id, event.weapon_id)) {
        return Result<core::BattleEvent>::err(weapon_id.error());
    }
    auto damage = required_int(envelope.payload, "damage");
    if (!assign_or_error(damage, event.damage)) {
        return Result<core::BattleEvent>::err(damage.error());
    }
    auto hit_zone = required_string(envelope.payload, "hit_zone");
    if (!assign_or_error(hit_zone, event.hit_zone)) {
        return Result<core::BattleEvent>::err(hit_zone.error());
    }
    if (event.damage <= 0) {
        return Result<core::BattleEvent>::err(invalid("damage must be positive"));
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> player_state(const EventEnvelope& envelope) {
    core::PlayerStateUpdated event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto player_id = required_string(envelope.payload, "player_id");
    if (!assign_or_error(player_id, event.player_id)) {
        return Result<core::BattleEvent>::err(player_id.error());
    }
    auto health = required_int(envelope.payload, "health");
    if (!assign_or_error(health, event.health)) {
        return Result<core::BattleEvent>::err(health.error());
    }
    auto ammo = required_int(envelope.payload, "ammo");
    if (!assign_or_error(ammo, event.ammo)) {
        return Result<core::BattleEvent>::err(ammo.error());
    }
    auto alive = required_bool(envelope.payload, "alive");
    if (!assign_or_error(alive, event.alive)) {
        return Result<core::BattleEvent>::err(alive.error());
    }
    if (event.health < 0 || event.ammo < 0) {
        return Result<core::BattleEvent>::err(invalid("health and ammo must not be negative"));
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> battle_ended(const EventEnvelope& envelope) {
    core::BattleEnded event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto reason = required_string(envelope.payload, "reason");
    if (!assign_or_error(reason, event.reason)) {
        return Result<core::BattleEvent>::err(reason.error());
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> battle_paused(const EventEnvelope& envelope) {
    core::BattlePaused event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;

    auto reason = required_string(envelope.payload, "reason");
    if (!assign_or_error(reason, event.reason)) {
        return Result<core::BattleEvent>::err(reason.error());
    }
    return Result<core::BattleEvent>::ok(event);
}

Result<core::BattleEvent> battle_resumed(const EventEnvelope& envelope) {
    core::BattleResumed event;
    event.battle_id = envelope.battle_id;
    event.event_id = envelope.event_id;
    event.occurred_at_ms = envelope.occurred_at_ms;
    return Result<core::BattleEvent>::ok(event);
}

} // namespace

Result<EventEnvelope> parse_event_json(std::string_view json_text) {
    nlohmann::json object;
    try {
        object = nlohmann::json::parse(json_text.begin(), json_text.end());
    } catch (const nlohmann::json::parse_error& error) {
        return Result<EventEnvelope>::err(parse_error(error.what()));
    }

    if (auto ok = require_object(object, "event envelope"); !ok) {
        return Result<EventEnvelope>::err(ok.error());
    }

    EventEnvelope envelope;
    envelope.raw = object;

    auto schema = required_int(object, "schema_version");
    if (!assign_or_error(schema, envelope.schema_version)) {
        return Result<EventEnvelope>::err(schema.error());
    }
    if (envelope.schema_version != kSchemaVersion) {
        return Result<EventEnvelope>::err(
            {ErrorCode::UnsupportedSchema, "unsupported schema_version"});
    }

    auto event_id = required_string(object, "event_id");
    if (!assign_or_error(event_id, envelope.event_id)) {
        return Result<EventEnvelope>::err(event_id.error());
    }
    auto event_type = required_string(object, "event_type");
    if (!assign_or_error(event_type, envelope.event_type)) {
        return Result<EventEnvelope>::err(event_type.error());
    }
    auto battle_id = required_string(object, "battle_id");
    if (!assign_or_error(battle_id, envelope.battle_id)) {
        return Result<EventEnvelope>::err(battle_id.error());
    }
    auto source_id = required_string(object, "source_id");
    if (!assign_or_error(source_id, envelope.source_id)) {
        return Result<EventEnvelope>::err(source_id.error());
    }
    auto sequence = required_u64(object, "sequence");
    if (!assign_or_error(sequence, envelope.sequence)) {
        return Result<EventEnvelope>::err(sequence.error());
    }
    auto occurred_at = required_i64(object, "occurred_at_ms");
    if (!assign_or_error(occurred_at, envelope.occurred_at_ms)) {
        return Result<EventEnvelope>::err(occurred_at.error());
    }

    if (!object.contains("payload")) {
        return Result<EventEnvelope>::err(missing("payload"));
    }
    envelope.payload = object.at("payload");
    if (auto ok = require_object(envelope.payload, "payload"); !ok) {
        return Result<EventEnvelope>::err(ok.error());
    }

    auto core_event = to_core_event(envelope);
    if (!core_event) {
        return Result<EventEnvelope>::err(core_event.error());
    }

    return Result<EventEnvelope>::ok(std::move(envelope));
}

Result<core::BattleEvent> to_core_event(const EventEnvelope& envelope) {
    if (envelope.event_type == "player_joined") {
        return player_joined(envelope);
    }
    if (envelope.event_type == "battle_started") {
        return battle_started(envelope);
    }
    if (envelope.event_type == "shot") {
        return shot(envelope);
    }
    if (envelope.event_type == "hit") {
        return hit(envelope);
    }
    if (envelope.event_type == "player_state") {
        return player_state(envelope);
    }
    if (envelope.event_type == "battle_ended") {
        return battle_ended(envelope);
    }
    if (envelope.event_type == "battle_paused") {
        return battle_paused(envelope);
    }
    if (envelope.event_type == "battle_resumed") {
        return battle_resumed(envelope);
    }
    return Result<core::BattleEvent>::err(
        {ErrorCode::UnknownEventType, "unknown event_type: " + envelope.event_type});
}

nlohmann::json to_json(const EventEnvelope& envelope) {
    return {
        {"schema_version", envelope.schema_version},
        {"event_id", envelope.event_id},
        {"event_type", envelope.event_type},
        {"battle_id", envelope.battle_id},
        {"source_id", envelope.source_id},
        {"sequence", envelope.sequence},
        {"occurred_at_ms", envelope.occurred_at_ms},
        {"payload", envelope.payload},
    };
}

nlohmann::json to_json(const core::BattleSnapshot& snapshot) {
    nlohmann::json players = nlohmann::json::object();
    for (const auto& [player_id, player] : snapshot.players) {
        players[player_id] = {
            {"player_id", player.player_id},
            {"display_name", player.display_name},
            {"team_id", player.team_id},
            {"module_id", player.module_id},
            {"health", player.health},
            {"ammo", player.ammo},
            {"alive", player.alive},
            {"shot_count", player.shot_count},
            {"hit_count", player.hit_count},
        };
    }

    nlohmann::json teams = nlohmann::json::object();
    for (const auto& [team_id, team] : snapshot.teams) {
        teams[team_id] = {
            {"team_id", team.team_id},
            {"score", team.score},
        };
    }

    nlohmann::json output = {
        {"battle_id", snapshot.battle_id},
        {"phase", core::to_string(snapshot.phase)},
        {"mode", snapshot.mode},
        {"players", std::move(players)},
        {"teams", std::move(teams)},
        {"latest_event_id", snapshot.latest_event_id.value_or("")},
    };
    if (snapshot.started_at_ms) {
        output["started_at_ms"] = *snapshot.started_at_ms;
    }
    if (snapshot.ended_at_ms) {
        output["ended_at_ms"] = *snapshot.ended_at_ms;
    }
    return output;
}

std::string canonical_json(const EventEnvelope& envelope) {
    return to_json(envelope).dump();
}

} // namespace fenghuo::protocol
