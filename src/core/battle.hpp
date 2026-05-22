#pragma once

#include "fenghuo/result.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace fenghuo::core {

struct BattleId {
    std::string value;
};

struct PlayerId {
    std::string value;
};

struct TeamId {
    std::string value;
};

struct EventId {
    std::string value;
};

enum class BattlePhase {
    Lobby,
    Active,
    Paused,
    Ended,
};

struct PlayerState {
    std::string player_id;
    std::string display_name;
    std::string team_id;
    std::string module_id;
    int health{100};
    int ammo{0};
    bool alive{true};
    std::uint64_t shot_count{0};
    std::uint64_t hit_count{0};
};

struct TeamState {
    std::string team_id;
    std::uint64_t score{0};
};

struct BattleState {
    std::string battle_id;
    BattlePhase phase{BattlePhase::Lobby};
    std::string mode;
    std::optional<std::int64_t> started_at_ms;
    std::optional<std::int64_t> ended_at_ms;
    std::map<std::string, PlayerState> players;
    std::map<std::string, TeamState> teams;
    std::optional<std::string> latest_event_id;
};

using BattleSnapshot = BattleState;

struct PlayerJoined {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    std::string display_name;
    std::string team_id;
    std::string module_id;
};

struct BattleStarted {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string mode;
    std::int64_t duration_ms{0};
};

struct Shot {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    std::string weapon_id;
    int ammo_after{0};
};

struct Hit {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string attacker_player_id;
    std::string target_player_id;
    std::string weapon_id;
    int damage{0};
    std::string hit_zone;
};

struct PlayerStateUpdated {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    int health{100};
    int ammo{0};
    bool alive{true};
};

struct BattleEnded {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string reason;
};

struct BattlePaused {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string reason;
};

struct BattleResumed {
    std::string battle_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
};

using BattleEvent =
    std::variant<PlayerJoined,
                 BattleStarted,
                 Shot,
                 Hit,
                 PlayerStateUpdated,
                 BattleEnded,
                 BattlePaused,
                 BattleResumed>;

Result<BattleSnapshot> apply_event(BattleState& state, const BattleEvent& event);

std::string to_string(BattlePhase phase);

} // namespace fenghuo::core
