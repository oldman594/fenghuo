#include "room_runtime/runtime.hpp"

#include <algorithm>
#include <chrono>

namespace fenghuo::room_runtime {

namespace {

SubmitRoomEventResult rejected(Error error, room::RoomSnapshot snapshot = {}) {
    return {SubmitRoomEventResult::Status::Rejected, std::move(snapshot), std::move(error)};
}

SubmitRoomEventResult storage_failed(Error error, room::RoomSnapshot snapshot = {}) {
    return {SubmitRoomEventResult::Status::StorageFailed, std::move(snapshot), std::move(error)};
}

} // namespace

RoomRuntime::RoomRuntime(std::shared_ptr<storage::RoomEventStore> store,
                         std::shared_ptr<RoomUpdateSink> sink)
    : store_(std::move(store)), sink_(std::move(sink)) {}

Result<void> RoomRuntime::replay(const std::vector<storage::AcceptedRoomEventRecord>& records) {
    std::lock_guard lock(mutex_);
    rooms_.clear();
    accepted_events_.clear();
    latest_sequence_by_source_.clear();
    acceptance_sequence_ = 0;

    for (const auto& record : records) {
        auto applied = apply_to_state(record.envelope);
        if (!applied) {
            return Result<void>::err(applied.error());
        }
        rooms_[record.envelope.room_id] = applied.value();
        accepted_events_[record.envelope.event_id] = room::canonical_json(record.envelope);
        latest_sequence_by_source_[record.envelope.source_id] = record.envelope.sequence;
        acceptance_sequence_ = std::max(acceptance_sequence_, record.metadata.acceptance_sequence);
    }

    return Result<void>::ok();
}

SubmitRoomEventResult RoomRuntime::submit_event(const room::RoomEventEnvelope& envelope) {
    std::lock_guard lock(mutex_);

    const auto canonical = room::canonical_json(envelope);
    if (const auto duplicate = accepted_events_.find(envelope.event_id);
        duplicate != accepted_events_.end()) {
        auto snapshot_it = rooms_.find(envelope.room_id);
        room::RoomSnapshot snapshot = snapshot_it == rooms_.end() ? room::RoomSnapshot{} : snapshot_it->second;
        if (duplicate->second == canonical) {
            return {SubmitRoomEventResult::Status::Duplicate, std::move(snapshot), std::nullopt};
        }
        return rejected({ErrorCode::Conflict, "duplicate room event_id has different content"},
                        std::move(snapshot));
    }

    if (const auto latest_sequence = latest_sequence_by_source_.find(envelope.source_id);
        latest_sequence != latest_sequence_by_source_.end() &&
        envelope.sequence <= latest_sequence->second) {
        auto snapshot_it = rooms_.find(envelope.room_id);
        room::RoomSnapshot snapshot = snapshot_it == rooms_.end() ? room::RoomSnapshot{} : snapshot_it->second;
        return rejected({ErrorCode::Conflict, "stale room source sequence"}, std::move(snapshot));
    }

    auto existing = rooms_.find(envelope.room_id);
    auto applied = apply_to_state(envelope);
    if (!applied) {
        const auto snapshot = existing == rooms_.end() ? room::RoomSnapshot{} : existing->second;
        return rejected(applied.error(), snapshot);
    }
    auto current = std::move(applied).value();

    const auto receive_time = now_ms();
    const auto accepted_time = now_ms();
    storage::AcceptedEventMetadata metadata{
        receive_time,
        accepted_time,
        acceptance_sequence_ + 1,
    };
    if (auto stored = store_->append(envelope, metadata); !stored) {
        const auto snapshot = existing == rooms_.end() ? room::RoomSnapshot{} : existing->second;
        return storage_failed(stored.error(), snapshot);
    }

    acceptance_sequence_ += 1;
    rooms_[envelope.room_id] = current;
    accepted_events_.emplace(envelope.event_id, canonical);
    latest_sequence_by_source_[envelope.source_id] = envelope.sequence;

    if (sink_) {
        sink_->publish_room_updated(envelope, current);
    }

    return {SubmitRoomEventResult::Status::Accepted, std::move(current), std::nullopt};
}

Result<room::RoomSnapshot> RoomRuntime::apply_to_state(const room::RoomEventEnvelope& envelope) {
    auto room_event = room::to_room_event(envelope);
    if (!room_event) {
        return Result<room::RoomSnapshot>::err(room_event.error());
    }

    auto existing = rooms_.find(envelope.room_id);
    auto current = existing == rooms_.end() ? room::RoomState{} : existing->second;
    auto applied = room::apply_event(current, room_event.value());
    if (!applied) {
        return applied;
    }

    return Result<room::RoomSnapshot>::ok(std::move(current));
}

Result<room::RoomSnapshot> RoomRuntime::snapshot(std::string room_id) const {
    std::lock_guard lock(mutex_);
    auto room = rooms_.find(room_id);
    if (room == rooms_.end()) {
        return Result<room::RoomSnapshot>::err({ErrorCode::NotFound, "room not found"});
    }
    return Result<room::RoomSnapshot>::ok(room->second);
}

std::vector<room::RoomSnapshot> RoomRuntime::snapshots() const {
    std::lock_guard lock(mutex_);
    std::vector<room::RoomSnapshot> output;
    output.reserve(rooms_.size());
    for (const auto& [_, room] : rooms_) {
        output.push_back(room);
    }
    return output;
}

std::int64_t RoomRuntime::now_ms() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void CompositeRoomUpdateSink::add(std::shared_ptr<RoomUpdateSink> sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void CompositeRoomUpdateSink::publish_room_updated(const room::RoomEventEnvelope& envelope,
                                                   const room::RoomSnapshot& snapshot) {
    std::vector<std::shared_ptr<RoomUpdateSink>> sinks;
    {
        std::lock_guard lock(mutex_);
        sinks = sinks_;
    }
    for (const auto& sink : sinks) {
        if (sink) {
            sink->publish_room_updated(envelope, snapshot);
        }
    }
}

} // namespace fenghuo::room_runtime
