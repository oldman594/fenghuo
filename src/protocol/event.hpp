#pragma once

#include "core/battle.hpp"
#include "fenghuo/result.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace fenghuo::protocol {

inline constexpr int kSchemaVersion = 0;

struct EventEnvelope {
    int schema_version{0};
    std::string event_id;
    std::string event_type;
    std::string battle_id;
    std::string source_id;
    std::uint64_t sequence{0};
    std::int64_t occurred_at_ms{0};
    nlohmann::json payload;
    nlohmann::json raw;
};

Result<EventEnvelope> parse_event_json(std::string_view json_text);
Result<core::BattleEvent> to_core_event(const EventEnvelope& envelope);

nlohmann::json to_json(const EventEnvelope& envelope);
nlohmann::json to_json(const core::BattleSnapshot& snapshot);
std::string canonical_json(const EventEnvelope& envelope);

} // namespace fenghuo::protocol
