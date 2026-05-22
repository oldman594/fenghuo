#pragma once

#include "room/room.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace fenghuo::room {

inline constexpr int kRoomSchemaVersion = 0;

struct RoomEventEnvelope {
    int schema_version{0};
    std::string event_id;
    std::string event_type;
    std::string room_id;
    std::string source_id;
    std::uint64_t sequence{0};
    std::int64_t occurred_at_ms{0};
    nlohmann::json payload;
    nlohmann::json raw;
};

Result<RoomEventEnvelope> parse_room_event_json(std::string_view json_text);
Result<RoomEvent> to_room_event(const RoomEventEnvelope& envelope);

nlohmann::json to_json(const RoomEventEnvelope& envelope);
nlohmann::json to_json(const RoomSnapshot& snapshot);
std::string canonical_json(const RoomEventEnvelope& envelope);

} // namespace fenghuo::room
