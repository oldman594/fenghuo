#include "room/room.hpp"

#include <algorithm>
#include <type_traits>

namespace fenghuo::room {

namespace {

Error invalid(std::string message) {
    return {ErrorCode::InvalidArgument, std::move(message)};
}

Error conflict(std::string message) {
    return {ErrorCode::Conflict, std::move(message)};
}

Result<void> check_room(const RoomState& state, const std::string& room_id) {
    if (state.room_id.empty()) {
        return Result<void>::err(conflict("room must be created before applying room events"));
    }
    if (state.room_id != room_id) {
        return Result<void>::err(conflict("event room_id does not match room state"));
    }
    return Result<void>::ok();
}

Result<void> require_open(const RoomState& state, std::string event_type) {
    if (state.phase != RoomPhase::Open) {
        return Result<void>::err(conflict(std::move(event_type) + " is only accepted while room is open"));
    }
    return Result<void>::ok();
}

void mark_latest(RoomState& state, const std::string& event_id, std::int64_t occurred_at_ms) {
    state.latest_room_event_id = event_id;
    state.updated_at_ms = occurred_at_ms;
}

std::uint32_t player_count_for_team(const RoomState& state, const std::string& team_id) {
    return static_cast<std::uint32_t>(std::count_if(
        state.players.begin(), state.players.end(), [&team_id](const auto& entry) {
            return entry.second.team_id == team_id;
        }));
}

bool has_module_id(const RoomState& state, const std::string& module_id) {
    if (module_id.empty()) {
        return false;
    }
    return std::any_of(state.players.begin(), state.players.end(), [&module_id](const auto& entry) {
        return entry.second.module_id == module_id;
    });
}

Result<void> check_team_capacity(const RoomState& state, const std::string& team_id) {
    auto team = state.teams.find(team_id);
    if (team == state.teams.end()) {
        return Result<void>::err(conflict("team does not exist"));
    }
    if (team->second.max_players > 0 && player_count_for_team(state, team_id) >= team->second.max_players) {
        return Result<void>::err(conflict("team is full"));
    }
    return Result<void>::ok();
}

Result<void> apply_room_created(RoomState& state, const RoomCreated& event) {
    if (!state.room_id.empty()) {
        return Result<void>::err(conflict("room already created"));
    }
    if (event.room_id.empty()) {
        return Result<void>::err(invalid("room_id is required"));
    }
    if (event.event_id.empty()) {
        return Result<void>::err(invalid("event_id is required"));
    }
    if (event.max_players == 0) {
        return Result<void>::err(invalid("max_players must be positive"));
    }
    if (event.teams.empty()) {
        return Result<void>::err(invalid("room must define at least one team"));
    }

    std::map<std::string, RoomTeamState> teams;
    std::uint32_t total_team_capacity = 0;
    for (const auto& team : event.teams) {
        if (team.team_id.empty()) {
            return Result<void>::err(invalid("team_id is required"));
        }
        if (team.max_players == 0) {
            return Result<void>::err(invalid("team max_players must be positive"));
        }
        if (teams.contains(team.team_id)) {
            return Result<void>::err(conflict("duplicate team_id"));
        }
        total_team_capacity += team.max_players;
        teams.emplace(team.team_id, RoomTeamState{team.team_id, team.display_name, team.max_players});
    }
    if (total_team_capacity < event.max_players) {
        return Result<void>::err(invalid("team capacity must cover room max_players"));
    }

    state.room_id = event.room_id;
    state.room_code = event.room_code;
    state.name = event.name;
    state.mode = event.mode;
    state.phase = RoomPhase::Open;
    state.created_at_ms = event.occurred_at_ms;
    state.updated_at_ms = event.occurred_at_ms;
    state.host_player_id = event.host_player_id;
    state.max_players = event.max_players;
    state.teams = std::move(teams);
    state.players.clear();
    state.battle_id.reset();
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_player_joined(RoomState& state, const RoomPlayerJoined& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (auto open = require_open(state, "room_player_joined"); !open) {
        return open;
    }
    if (event.player_id.empty()) {
        return Result<void>::err(invalid("player_id is required"));
    }
    if (state.players.contains(event.player_id)) {
        return Result<void>::err(conflict("player already joined"));
    }
    if (state.players.size() >= state.max_players) {
        return Result<void>::err(conflict("room is full"));
    }
    if (has_module_id(state, event.module_id)) {
        return Result<void>::err(conflict("module already assigned"));
    }
    if (auto capacity = check_team_capacity(state, event.team_id); !capacity) {
        return capacity;
    }

    state.players.emplace(event.player_id,
                          RoomPlayerState{event.player_id,
                                          event.display_name,
                                          event.team_id,
                                          event.module_id,
                                          false,
                                          event.occurred_at_ms});
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_player_left(RoomState& state, const RoomPlayerLeft& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (auto open = require_open(state, "room_player_left"); !open) {
        return open;
    }

    auto player = state.players.find(event.player_id);
    if (player == state.players.end()) {
        return Result<void>::err({ErrorCode::NotFound, "player is not in room"});
    }
    state.players.erase(player);
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_player_team_changed(RoomState& state, const RoomPlayerTeamChanged& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (auto open = require_open(state, "room_player_team_changed"); !open) {
        return open;
    }

    auto player = state.players.find(event.player_id);
    if (player == state.players.end()) {
        return Result<void>::err({ErrorCode::NotFound, "player is not in room"});
    }
    if (player->second.team_id != event.team_id) {
        if (auto capacity = check_team_capacity(state, event.team_id); !capacity) {
            return capacity;
        }
        player->second.team_id = event.team_id;
    } else if (!state.teams.contains(event.team_id)) {
        return Result<void>::err(conflict("team does not exist"));
    }
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_player_ready_changed(RoomState& state, const RoomPlayerReadyChanged& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (auto open = require_open(state, "room_player_ready_changed"); !open) {
        return open;
    }

    auto player = state.players.find(event.player_id);
    if (player == state.players.end()) {
        return Result<void>::err({ErrorCode::NotFound, "player is not in room"});
    }
    player->second.ready = event.ready;
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_room_started(RoomState& state, const RoomStarted& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (state.phase != RoomPhase::Open) {
        return Result<void>::err(conflict("room_started is only accepted while room is open"));
    }
    if (state.players.empty()) {
        return Result<void>::err(conflict("room cannot start without players"));
    }
    const auto unready = std::any_of(state.players.begin(), state.players.end(), [](const auto& entry) {
        return !entry.second.ready;
    });
    if (unready) {
        return Result<void>::err(conflict("room cannot start until all players are ready"));
    }
    if (event.battle_id.empty()) {
        return Result<void>::err(invalid("battle_id is required"));
    }

    state.phase = RoomPhase::Active;
    state.battle_id = event.battle_id;
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_room_ended(RoomState& state, const RoomEnded& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (state.phase != RoomPhase::Active) {
        return Result<void>::err(conflict("room_ended is only accepted while room is active"));
    }
    state.phase = RoomPhase::Ended;
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

Result<void> apply_room_closed(RoomState& state, const RoomClosed& event) {
    if (auto check = check_room(state, event.room_id); !check) {
        return check;
    }
    if (state.phase == RoomPhase::Active) {
        return Result<void>::err(conflict("active room cannot be closed"));
    }
    if (state.phase == RoomPhase::Closed) {
        return Result<void>::err(conflict("room already closed"));
    }
    state.phase = RoomPhase::Closed;
    mark_latest(state, event.event_id, event.occurred_at_ms);
    return Result<void>::ok();
}

template <typename Event>
Result<void> apply_event_to_state(RoomState& state, const Event& event) {
    if constexpr (std::is_same_v<Event, RoomCreated>) {
        return apply_room_created(state, event);
    } else if constexpr (std::is_same_v<Event, RoomPlayerJoined>) {
        return apply_player_joined(state, event);
    } else if constexpr (std::is_same_v<Event, RoomPlayerLeft>) {
        return apply_player_left(state, event);
    } else if constexpr (std::is_same_v<Event, RoomPlayerTeamChanged>) {
        return apply_player_team_changed(state, event);
    } else if constexpr (std::is_same_v<Event, RoomPlayerReadyChanged>) {
        return apply_player_ready_changed(state, event);
    } else if constexpr (std::is_same_v<Event, RoomStarted>) {
        return apply_room_started(state, event);
    } else if constexpr (std::is_same_v<Event, RoomEnded>) {
        return apply_room_ended(state, event);
    } else {
        return apply_room_closed(state, event);
    }
}

} // namespace

Result<RoomSnapshot> apply_event(RoomState& state, const RoomEvent& event) {
    auto applied =
        std::visit([&state](const auto& concrete) { return apply_event_to_state(state, concrete); },
                   event);
    if (!applied) {
        return Result<RoomSnapshot>::err(applied.error());
    }
    return Result<RoomSnapshot>::ok(state);
}

std::string to_string(RoomPhase phase) {
    switch (phase) {
        case RoomPhase::Open:
            return "open";
        case RoomPhase::Active:
            return "active";
        case RoomPhase::Ended:
            return "ended";
        case RoomPhase::Closed:
            return "closed";
    }
    return "unknown";
}

} // namespace fenghuo::room
