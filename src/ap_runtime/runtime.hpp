#pragma once

#include "core/battle.hpp"
#include "protocol/event.hpp"
#include "storage/event_store.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fenghuo::ap_runtime {

struct SubmitEventResult {
    enum class Status {
        Accepted,
        Duplicate,
        Rejected,
        StorageFailed,
    };

    Status status{Status::Rejected};
    core::BattleSnapshot snapshot;
    std::optional<Error> error;
};

class BattleUpdateSink {
public:
    virtual ~BattleUpdateSink() = default;

    virtual void publish_accepted_event(const protocol::EventEnvelope& envelope,
                                        const core::BattleSnapshot& snapshot) = 0;
};

class ApRuntime {
public:
    ApRuntime(std::shared_ptr<storage::BattleEventStore> store,
              std::shared_ptr<BattleUpdateSink> sink);

    Result<void> replay(const std::vector<storage::AcceptedEventRecord>& records);
    SubmitEventResult submit_event(const protocol::EventEnvelope& envelope);
    Result<core::BattleSnapshot> snapshot(std::string battle_id) const;

private:
    Result<core::BattleSnapshot> apply_to_state(const protocol::EventEnvelope& envelope);
    std::int64_t now_ms() const;

    std::shared_ptr<storage::BattleEventStore> store_;
    std::shared_ptr<BattleUpdateSink> sink_;
    mutable std::mutex mutex_;
    std::map<std::string, core::BattleState> battles_;
    std::unordered_map<std::string, std::string> accepted_events_;
    std::unordered_map<std::string, std::uint64_t> latest_sequence_by_source_;
    std::uint64_t acceptance_sequence_{0};
};

class CompositeBattleUpdateSink final : public BattleUpdateSink {
public:
    void add(std::shared_ptr<BattleUpdateSink> sink);
    void publish_accepted_event(const protocol::EventEnvelope& envelope,
                                const core::BattleSnapshot& snapshot) override;

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<BattleUpdateSink>> sinks_;
};

} // namespace fenghuo::ap_runtime
