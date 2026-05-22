#pragma once

#include "room/protocol.hpp"
#include "storage/event_store.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fenghuo::room_runtime {

struct SubmitRoomEventResult {
    enum class Status {
        Accepted,
        Duplicate,
        Rejected,
        StorageFailed,
    };

    Status status{Status::Rejected};
    room::RoomSnapshot snapshot;
    std::optional<Error> error;
};

class RoomUpdateSink {
public:
    virtual ~RoomUpdateSink() = default;

    virtual void publish_room_updated(const room::RoomEventEnvelope& envelope,
                                      const room::RoomSnapshot& snapshot) = 0;
};

class RoomRuntime {
public:
    RoomRuntime(std::shared_ptr<storage::RoomEventStore> store,
                std::shared_ptr<RoomUpdateSink> sink);

    Result<void> replay(const std::vector<storage::AcceptedRoomEventRecord>& records);
    SubmitRoomEventResult submit_event(const room::RoomEventEnvelope& envelope);
    Result<room::RoomSnapshot> snapshot(std::string room_id) const;
    std::vector<room::RoomSnapshot> snapshots() const;

private:
    Result<room::RoomSnapshot> apply_to_state(const room::RoomEventEnvelope& envelope);
    std::int64_t now_ms() const;

    std::shared_ptr<storage::RoomEventStore> store_;
    std::shared_ptr<RoomUpdateSink> sink_;
    mutable std::mutex mutex_;
    std::map<std::string, room::RoomState> rooms_;
    std::unordered_map<std::string, std::string> accepted_events_;
    std::unordered_map<std::string, std::uint64_t> latest_sequence_by_source_;
    std::uint64_t acceptance_sequence_{0};
};

class CompositeRoomUpdateSink final : public RoomUpdateSink {
public:
    void add(std::shared_ptr<RoomUpdateSink> sink);
    void publish_room_updated(const room::RoomEventEnvelope& envelope,
                              const room::RoomSnapshot& snapshot) override;

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<RoomUpdateSink>> sinks_;
};

} // namespace fenghuo::room_runtime
