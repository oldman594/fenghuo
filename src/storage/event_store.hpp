#pragma once

#include "fenghuo/result.hpp"
#include "protocol/event.hpp"
#include "room/protocol.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace fenghuo::storage {

struct AcceptedEventMetadata {
    std::int64_t received_at_ms{0};
    std::int64_t accepted_at_ms{0};
    std::uint64_t acceptance_sequence{0};
};

struct AcceptedEventRecord {
    protocol::EventEnvelope envelope;
    AcceptedEventMetadata metadata;
};

struct AcceptedRoomEventRecord {
    room::RoomEventEnvelope envelope;
    AcceptedEventMetadata metadata;
};

class BattleEventStore {
public:
    virtual ~BattleEventStore() = default;

    virtual Result<void> append(const protocol::EventEnvelope& envelope,
                                const AcceptedEventMetadata& metadata) = 0;
};

class JsonlBattleEventStore final : public BattleEventStore {
public:
    explicit JsonlBattleEventStore(std::filesystem::path root);

    Result<void> append(const protocol::EventEnvelope& envelope,
                        const AcceptedEventMetadata& metadata) override;
    Result<std::vector<AcceptedEventRecord>> read_all() const;

private:
    std::filesystem::path root_;
};

class RoomEventStore {
public:
    virtual ~RoomEventStore() = default;

    virtual Result<void> append(const room::RoomEventEnvelope& envelope,
                                const AcceptedEventMetadata& metadata) = 0;
};

class JsonlRoomEventStore final : public RoomEventStore {
public:
    explicit JsonlRoomEventStore(std::filesystem::path root);

    Result<void> append(const room::RoomEventEnvelope& envelope,
                        const AcceptedEventMetadata& metadata) override;
    Result<std::vector<AcceptedRoomEventRecord>> read_all() const;

private:
    std::filesystem::path root_;
};

} // namespace fenghuo::storage
