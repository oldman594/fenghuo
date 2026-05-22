#include "core/battle.hpp"

#include <algorithm>
#include <type_traits>

namespace fenghuo::core {

namespace {

Error conflict(std::string message) {
    return {ErrorCode::Conflict, std::move(message)};
}

void ensure_battle_id(BattleState& state, const std::string& battle_id) {
    if (state.battle_id.empty()) {
        state.battle_id = battle_id;
    }
}

Result<void> check_battle(BattleState& state, const std::string& battle_id) {
    ensure_battle_id(state, battle_id);
    if (state.battle_id != battle_id) {
        return Result<void>::err(conflict("event battle_id does not match battle state"));
    }
    return Result<void>::ok();
}

void mark_latest(BattleState& state, const std::string& event_id) {
    state.latest_event_id = event_id;
}

Result<void> apply_player_joined(BattleState& state, const PlayerJoined& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (state.phase != BattlePhase::Lobby) {
        return Result<void>::err(conflict("player_joined is only accepted in lobby"));
    }
    if (state.players.contains(event.player_id)) {
        return Result<void>::err(conflict("player already joined"));
    }

    state.players.emplace(
        event.player_id,
        PlayerState{event.player_id, event.display_name, event.team_id, event.module_id});
    state.teams.try_emplace(event.team_id, TeamState{event.team_id, 0});
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_battle_started(BattleState& state, const BattleStarted& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (state.phase != BattlePhase::Lobby) {
        return Result<void>::err(conflict("battle_started is only accepted in lobby"));
    }

    state.phase = BattlePhase::Active;
    state.mode = event.mode;
    state.started_at_ms = event.occurred_at_ms;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> require_active(const BattleState& state, std::string event_type) {
    if (state.phase != BattlePhase::Active) {
        return Result<void>::err(
            conflict(std::move(event_type) + " is only accepted while battle is active"));
    }
    return Result<void>::ok();
}

Result<void> apply_shot(BattleState& state, const Shot& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (auto active = require_active(state, "shot"); !active) {
        return active;
    }

    auto player = state.players.find(event.player_id);
    if (player == state.players.end()) {
        return Result<void>::err(conflict("shot player is not registered"));
    }

    player->second.shot_count += 1;
    player->second.ammo = event.ammo_after;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_hit(BattleState& state, const Hit& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (auto active = require_active(state, "hit"); !active) {
        return active;
    }

    auto attacker = state.players.find(event.attacker_player_id);
    auto target = state.players.find(event.target_player_id);
    if (attacker == state.players.end()) {
        return Result<void>::err(conflict("hit attacker is not registered"));
    }
    if (target == state.players.end()) {
        return Result<void>::err(conflict("hit target is not registered"));
    }

    target->second.health = std::max(0, target->second.health - event.damage);
    target->second.alive = target->second.health > 0;
    attacker->second.hit_count += 1;
    state.teams.try_emplace(attacker->second.team_id, TeamState{attacker->second.team_id, 0});
    state.teams.at(attacker->second.team_id).score += 1;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_player_state(BattleState& state, const PlayerStateUpdated& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (auto active = require_active(state, "player_state"); !active) {
        return active;
    }

    auto player = state.players.find(event.player_id);
    if (player == state.players.end()) {
        return Result<void>::err(conflict("player_state player is not registered"));
    }

    player->second.health = std::max(0, event.health);
    player->second.ammo = event.ammo;
    player->second.alive = event.alive && player->second.health > 0;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_battle_ended(BattleState& state, const BattleEnded& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (state.phase != BattlePhase::Active && state.phase != BattlePhase::Paused) {
        return Result<void>::err(
            conflict("battle_ended is only accepted while battle is active or paused"));
    }

    state.phase = BattlePhase::Ended;
    state.ended_at_ms = event.occurred_at_ms;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_battle_paused(BattleState& state, const BattlePaused& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (state.phase != BattlePhase::Active) {
        return Result<void>::err(conflict("battle_paused is only accepted while battle is active"));
    }
    state.phase = BattlePhase::Paused;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

Result<void> apply_battle_resumed(BattleState& state, const BattleResumed& event) {
    if (auto check = check_battle(state, event.battle_id); !check) {
        return check;
    }
    if (state.phase != BattlePhase::Paused) {
        return Result<void>::err(conflict("battle_resumed is only accepted while battle is paused"));
    }
    state.phase = BattlePhase::Active;
    mark_latest(state, event.event_id);
    return Result<void>::ok();
}

template <typename Event>
Result<void> apply_event_to_state(BattleState& state, const Event& event) {
    if constexpr (std::is_same_v<Event, PlayerJoined>) {
        return apply_player_joined(state, event);
    } else if constexpr (std::is_same_v<Event, BattleStarted>) {
        return apply_battle_started(state, event);
    } else if constexpr (std::is_same_v<Event, Shot>) {
        return apply_shot(state, event);
    } else if constexpr (std::is_same_v<Event, Hit>) {
        return apply_hit(state, event);
    } else if constexpr (std::is_same_v<Event, PlayerStateUpdated>) {
        return apply_player_state(state, event);
    } else if constexpr (std::is_same_v<Event, BattleEnded>) {
        return apply_battle_ended(state, event);
    } else if constexpr (std::is_same_v<Event, BattlePaused>) {
        return apply_battle_paused(state, event);
    } else {
        return apply_battle_resumed(state, event);
    }
}

} // namespace

Result<BattleSnapshot> apply_event(BattleState& state, const BattleEvent& event) {
    auto applied = std::visit([&state](const auto& concrete) { return apply_event_to_state(state, concrete); },
                              event);
    if (!applied) {
        return Result<BattleSnapshot>::err(applied.error());
    }
    return Result<BattleSnapshot>::ok(state);
}

std::string to_string(BattlePhase phase) {
    switch (phase) {
        case BattlePhase::Lobby:
            return "lobby";
        case BattlePhase::Active:
            return "active";
        case BattlePhase::Paused:
            return "paused";
        case BattlePhase::Ended:
            return "ended";
    }
    return "unknown";
}

} // namespace fenghuo::core
