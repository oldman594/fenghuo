#pragma once

#include "fenghuo/result.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fenghuo::room {

enum class RoomPhase {
    Open,
    Active,
    Ended,
    Closed,
};

struct RoomTeamState {
    std::string team_id;
    std::string display_name;
    std::uint32_t max_players{0};
};

struct RoomPlayerState {
    std::string player_id;
    std::string display_name;
    std::string team_id;
    std::string module_id;
    bool ready{false};
    std::int64_t joined_at_ms{0};
};

struct RoomDeviceState {
    std::string device_id;
    std::string device_kind;
    std::string display_name;
    bool online{true};
    std::optional<int> battery_percent;
    std::optional<int> signal_strength;
    std::optional<std::string> bound_player_id;
    std::int64_t last_seen_at_ms{0};
    std::int64_t registered_at_ms{0};
};

struct RoomPlayerPositionState {
    std::string player_id;
    std::string source_device_id;
    double x{0.0};
    double y{0.0};
    double heading_deg{0.0};
    double velocity_mps{0.0};
    std::int64_t updated_at_ms{0};
};

struct RoomState {
    std::string room_id;
    std::string room_code;
    std::string name;
    std::string mode;
    RoomPhase phase{RoomPhase::Open};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
    std::string host_player_id;
    std::uint32_t max_players{0};
    std::map<std::string, RoomTeamState> teams;
    std::map<std::string, RoomPlayerState> players;
    std::map<std::string, RoomDeviceState> devices;
    std::map<std::string, RoomPlayerPositionState> positions;
    std::optional<std::string> battle_id;
    std::optional<std::string> latest_room_event_id;
};

using RoomSnapshot = RoomState;

struct RoomTeamSpec {
    std::string team_id;
    std::string display_name;
    std::uint32_t max_players{0};
};

struct RoomCreated {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string room_code;
    std::string name;
    std::string mode;
    std::string host_player_id;
    std::uint32_t max_players{0};
    std::vector<RoomTeamSpec> teams;
};

struct RoomPlayerJoined {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    std::string display_name;
    std::string team_id;
    std::string module_id;
};

struct RoomPlayerLeft {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
};

struct RoomPlayerTeamChanged {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    std::string team_id;
};

struct RoomPlayerReadyChanged {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    bool ready{false};
};

struct RoomDeviceRegistered {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string device_id;
    std::string device_kind;
    std::string display_name;
    std::optional<int> battery_percent;
    std::optional<int> signal_strength;
};

struct RoomDeviceHeartbeatUpdated {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string device_id;
    std::optional<int> battery_percent;
    std::optional<int> signal_strength;
    bool online{true};
};

struct RoomDeviceBound {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string device_id;
    std::string player_id;
};

struct RoomDeviceUnbound {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string device_id;
};

struct RoomPlayerPositionUpdated {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string player_id;
    std::string source_device_id;
    double x{0.0};
    double y{0.0};
    double heading_deg{0.0};
    double velocity_mps{0.0};
};

struct RoomStarted {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
    std::string battle_id;
    std::int64_t duration_ms{0};
};

struct RoomEnded {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
};

struct RoomClosed {
    std::string room_id;
    std::string event_id;
    std::int64_t occurred_at_ms{0};
};

using RoomEvent = std::variant<RoomCreated,
                               RoomPlayerJoined,
                               RoomPlayerLeft,
                               RoomPlayerTeamChanged,
                               RoomPlayerReadyChanged,
                               RoomDeviceRegistered,
                               RoomDeviceHeartbeatUpdated,
                               RoomDeviceBound,
                               RoomDeviceUnbound,
                               RoomPlayerPositionUpdated,
                               RoomStarted,
                               RoomEnded,
                               RoomClosed>;

Result<RoomSnapshot> apply_event(RoomState& state, const RoomEvent& event);

std::string to_string(RoomPhase phase);

} // namespace fenghuo::room
