#include "ap_runtime/runtime.hpp"

#include <algorithm>
#include <chrono>

namespace fenghuo::ap_runtime {

namespace {

SubmitEventResult rejected(Error error, core::BattleSnapshot snapshot = {}) {
    return {SubmitEventResult::Status::Rejected, std::move(snapshot), std::move(error)};
}

SubmitEventResult storage_failed(Error error, core::BattleSnapshot snapshot = {}) {
    return {SubmitEventResult::Status::StorageFailed, std::move(snapshot), std::move(error)};
}

} // namespace

ApRuntime::ApRuntime(std::shared_ptr<storage::BattleEventStore> store,
                     std::shared_ptr<BattleUpdateSink> sink)
    : store_(std::move(store)), sink_(std::move(sink)) {}

Result<void> ApRuntime::replay(const std::vector<storage::AcceptedEventRecord>& records) {
    std::lock_guard lock(mutex_);
    battles_.clear();
    accepted_events_.clear();
    latest_sequence_by_source_.clear();
    acceptance_sequence_ = 0;

    for (const auto& record : records) {
        auto applied = apply_to_state(record.envelope);
        if (!applied) {
            return Result<void>::err(applied.error());
        }
        battles_[record.envelope.battle_id] = applied.value();
        accepted_events_[record.envelope.event_id] = protocol::canonical_json(record.envelope);
        latest_sequence_by_source_[record.envelope.source_id] = record.envelope.sequence;
        acceptance_sequence_ = std::max(acceptance_sequence_, record.metadata.acceptance_sequence);
    }

    return Result<void>::ok();
}

SubmitEventResult ApRuntime::submit_event(const protocol::EventEnvelope& envelope) {
    std::lock_guard lock(mutex_);

    const auto canonical = protocol::canonical_json(envelope);
    if (const auto duplicate = accepted_events_.find(envelope.event_id);
        duplicate != accepted_events_.end()) {
        auto snapshot_it = battles_.find(envelope.battle_id);
        core::BattleSnapshot snapshot = snapshot_it == battles_.end() ? core::BattleSnapshot{} : snapshot_it->second;
        if (duplicate->second == canonical) {
            return {SubmitEventResult::Status::Duplicate, std::move(snapshot), std::nullopt};
        }
        return rejected({ErrorCode::Conflict, "duplicate event_id has different content"},
                        std::move(snapshot));
    }

    if (const auto latest_sequence = latest_sequence_by_source_.find(envelope.source_id);
        latest_sequence != latest_sequence_by_source_.end() && envelope.sequence <= latest_sequence->second) {
        auto snapshot_it = battles_.find(envelope.battle_id);
        core::BattleSnapshot snapshot = snapshot_it == battles_.end() ? core::BattleSnapshot{} : snapshot_it->second;
        return rejected({ErrorCode::Conflict, "stale source sequence"}, std::move(snapshot));
    }

    auto existing = battles_.find(envelope.battle_id);
    auto applied = apply_to_state(envelope);
    if (!applied) {
        const auto snapshot = existing == battles_.end() ? core::BattleSnapshot{} : existing->second;
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
        const auto snapshot = existing == battles_.end() ? core::BattleSnapshot{} : existing->second;
        return storage_failed(stored.error(), snapshot);
    }

    acceptance_sequence_ += 1;
    battles_[envelope.battle_id] = current;
    accepted_events_.emplace(envelope.event_id, canonical);
    latest_sequence_by_source_[envelope.source_id] = envelope.sequence;

    if (sink_) {
        sink_->publish_accepted_event(envelope, current);
    }

    return {SubmitEventResult::Status::Accepted, std::move(current), std::nullopt};
}

Result<core::BattleSnapshot> ApRuntime::apply_to_state(const protocol::EventEnvelope& envelope) {
    auto core_event = protocol::to_core_event(envelope);
    if (!core_event) {
        return Result<core::BattleSnapshot>::err(core_event.error());
    }

    auto existing = battles_.find(envelope.battle_id);
    auto current = existing == battles_.end() ? core::BattleState{} : existing->second;
    if (current.battle_id.empty()) {
        current.battle_id = envelope.battle_id;
    }

    auto applied = core::apply_event(current, core_event.value());
    if (!applied) {
        return applied;
    }

    return Result<core::BattleSnapshot>::ok(std::move(current));
}

Result<core::BattleSnapshot> ApRuntime::snapshot(std::string battle_id) const {
    std::lock_guard lock(mutex_);
    auto battle = battles_.find(battle_id);
    if (battle == battles_.end()) {
        return Result<core::BattleSnapshot>::err({ErrorCode::NotFound, "battle not found"});
    }
    return Result<core::BattleSnapshot>::ok(battle->second);
}

std::int64_t ApRuntime::now_ms() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void CompositeBattleUpdateSink::add(std::shared_ptr<BattleUpdateSink> sink) {
    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void CompositeBattleUpdateSink::publish_accepted_event(const protocol::EventEnvelope& envelope,
                                                       const core::BattleSnapshot& snapshot) {
    std::vector<std::shared_ptr<BattleUpdateSink>> sinks;
    {
        std::lock_guard lock(mutex_);
        sinks = sinks_;
    }
    for (const auto& sink : sinks) {
        if (sink) {
            sink->publish_accepted_event(envelope, snapshot);
        }
    }
}

} // namespace fenghuo::ap_runtime
