#include "ap_runtime/runtime.hpp"
#include "room_runtime/runtime.hpp"
#include "server/server.hpp"
#include "storage/event_store.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string event_json(std::string event_id, std::string event_type, std::uint64_t sequence,
                       nlohmann::json payload) {
    return nlohmann::json{
        {"schema_version", 0},
        {"event_id", std::move(event_id)},
        {"event_type", std::move(event_type)},
        {"battle_id", "battle-001"},
        {"source_id", "sim"},
        {"sequence", sequence},
        {"occurred_at_ms", 1730000000000 + static_cast<std::int64_t>(sequence)},
        {"payload", std::move(payload)},
    }.dump();
}

fenghuo::protocol::EventEnvelope parse(std::string text) {
    auto parsed = fenghuo::protocol::parse_event_json(text);
    if (!parsed) {
        throw std::runtime_error(parsed.error().message);
    }
    return std::move(parsed).value();
}

class FakeBattleEventStore final : public fenghuo::storage::BattleEventStore {
public:
    fenghuo::Result<void> append(const fenghuo::protocol::EventEnvelope& envelope,
                                 const fenghuo::storage::AcceptedEventMetadata&) override {
        appended.push_back(envelope.event_id);
        return fenghuo::Result<void>::ok();
    }

    std::vector<std::string> appended;
};

class FailingBattleEventStore final : public fenghuo::storage::BattleEventStore {
public:
    fenghuo::Result<void> append(const fenghuo::protocol::EventEnvelope&,
                                 const fenghuo::storage::AcceptedEventMetadata&) override {
        return fenghuo::Result<void>::err({fenghuo::ErrorCode::StorageFailure, "forced failure"});
    }
};

class FakeRoomEventStore final : public fenghuo::storage::RoomEventStore {
public:
    fenghuo::Result<void> append(const fenghuo::room::RoomEventEnvelope&,
                                 const fenghuo::storage::AcceptedEventMetadata&) override {
        return fenghuo::Result<void>::ok();
    }
};

class CollectingBattleUpdateSink final : public fenghuo::ap_runtime::BattleUpdateSink {
public:
    void publish_accepted_event(const fenghuo::protocol::EventEnvelope& envelope,
                                const fenghuo::core::BattleSnapshot& snapshot) override {
        published.push_back(envelope.event_id);
        snapshots.push_back(snapshot);
    }

    std::vector<std::string> published;
    std::vector<fenghuo::core::BattleSnapshot> snapshots;
};

std::vector<fenghuo::protocol::EventEnvelope> normal_sequence() {
    return {
        parse(event_json("evt-001", "player_joined", 1,
                         {{"player_id", "p-red-01"},
                          {"display_name", "Red 01"},
                          {"team_id", "red"},
                          {"module_id", "module-red-01"}})),
        parse(event_json("evt-002", "player_joined", 2,
                         {{"player_id", "p-blue-01"},
                          {"display_name", "Blue 01"},
                          {"team_id", "blue"},
                          {"module_id", "module-blue-01"}})),
        parse(event_json("evt-003", "battle_started", 3,
                         {{"mode", "team_deathmatch"}, {"duration_ms", 600000}})),
        parse(event_json("evt-004", "shot", 4,
                         {{"player_id", "p-red-01"}, {"weapon_id", "rifle-01"}, {"ammo_after", 29}})),
        parse(event_json("evt-005", "hit", 5,
                         {{"attacker_player_id", "p-red-01"},
                          {"target_player_id", "p-blue-01"},
                          {"weapon_id", "rifle-01"},
                          {"damage", 10},
                          {"hit_zone", "torso"}})),
        parse(event_json("evt-006", "battle_ended", 6, {{"reason", "time_limit"}})),
    };
}

void test_runtime_sequence_and_duplicates() {
    auto store = std::make_shared<FakeBattleEventStore>();
    auto sink = std::make_shared<CollectingBattleUpdateSink>();
    fenghuo::ap_runtime::ApRuntime runtime(store, sink);

    fenghuo::ap_runtime::SubmitEventResult last;
    for (const auto& envelope : normal_sequence()) {
        last = runtime.submit_event(envelope);
        expect(last.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
               "normal event should be accepted");
    }

    expect(last.snapshot.phase == fenghuo::core::BattlePhase::Ended, "battle should end");
    expect(last.snapshot.players.at("p-blue-01").health == 90, "hit should damage target");
    expect(last.snapshot.players.at("p-red-01").shot_count == 1, "shot count should increment");
    expect(last.snapshot.players.at("p-red-01").hit_count == 1, "hit count should increment");
    expect(last.snapshot.teams.at("red").score == 1, "hit should increment team score");
    expect(store->appended.size() == 6, "accepted events should be stored");
    expect(sink->published.size() == 6, "accepted events should be published");

    auto duplicate = runtime.submit_event(normal_sequence().front());
    expect(duplicate.status == fenghuo::ap_runtime::SubmitEventResult::Status::Duplicate,
           "identical duplicate should be reported as duplicate");
    expect(store->appended.size() == 6, "duplicate should not be stored again");
    expect(sink->published.size() == 6, "duplicate should not be published again");
}

void test_battle_pause_blocks_gameplay_until_resume() {
    auto store = std::make_shared<FakeBattleEventStore>();
    auto sink = std::make_shared<CollectingBattleUpdateSink>();
    fenghuo::ap_runtime::ApRuntime runtime(store, sink);

    auto sequence = normal_sequence();
    for (std::size_t index = 0; index < 3; ++index) {
        auto result = runtime.submit_event(sequence.at(index));
        expect(result.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
               "setup event should be accepted");
    }

    auto paused = runtime.submit_event(parse(event_json(
        "evt-pause", "battle_paused", 10, {{"reason", "operator"}})));
    expect(paused.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
           "pause should be accepted");
    expect(paused.snapshot.phase == fenghuo::core::BattlePhase::Paused,
           "pause should set paused phase");

    auto hit_while_paused = runtime.submit_event(sequence.at(4));
    expect(hit_while_paused.status == fenghuo::ap_runtime::SubmitEventResult::Status::Rejected,
           "hit while paused should be rejected");

    auto resumed = runtime.submit_event(parse(event_json(
        "evt-resume", "battle_resumed", 11, nlohmann::json::object())));
    expect(resumed.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
           "resume should be accepted");
    expect(resumed.snapshot.phase == fenghuo::core::BattlePhase::Active,
           "resume should return to active phase");

    auto hit_after_resume = runtime.submit_event(parse(event_json(
        "evt-hit-after-resume", "hit", 12,
        {{"attacker_player_id", "p-red-01"},
         {"target_player_id", "p-blue-01"},
         {"weapon_id", "rifle-01"},
         {"damage", 10},
         {"hit_zone", "torso"}})));
    expect(hit_after_resume.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
           "hit after resume should be accepted");
}

void test_storage_failure_blocks_broadcast_and_commit() {
    auto store = std::make_shared<FailingBattleEventStore>();
    auto sink = std::make_shared<CollectingBattleUpdateSink>();
    fenghuo::ap_runtime::ApRuntime runtime(store, sink);

    auto result = runtime.submit_event(normal_sequence().front());
    expect(result.status == fenghuo::ap_runtime::SubmitEventResult::Status::StorageFailed,
           "storage failure should be surfaced");
    expect(sink->published.empty(), "storage failure must block broadcast");

    auto snapshot = runtime.snapshot("battle-001");
    expect(!snapshot, "storage failure must block state commit");
}

void test_jsonl_store_appends_records() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-ap-loop-test";
    std::filesystem::remove_all(root);

    fenghuo::storage::JsonlBattleEventStore store(root);
    auto envelope = normal_sequence().front();
    auto result = store.append(envelope, {1, 2, 3});
    expect(result.has_value(), "jsonl append should succeed");

    std::ifstream input(root / "battle-001.jsonl");
    std::string line;
    std::getline(input, line);
    auto record = nlohmann::json::parse(line);
    expect(record.at("event").at("event_id") == "evt-001", "jsonl should store event envelope");
    expect(record.at("metadata").at("acceptance_sequence") == 3,
           "jsonl should store accepted metadata");

    std::filesystem::remove_all(root);
}

void test_jsonl_replay_restores_runtime_state_and_dedupe() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-ap-loop-replay-test";
    std::filesystem::remove_all(root);

    {
        auto store = std::make_shared<fenghuo::storage::JsonlBattleEventStore>(root);
        auto sink = std::make_shared<CollectingBattleUpdateSink>();
        fenghuo::ap_runtime::ApRuntime runtime(store, sink);
        for (const auto& envelope : normal_sequence()) {
            auto result = runtime.submit_event(envelope);
            expect(result.status == fenghuo::ap_runtime::SubmitEventResult::Status::Accepted,
                   "seed event should be accepted");
        }
    }

    auto replay_store = std::make_shared<fenghuo::storage::JsonlBattleEventStore>(root);
    auto records = replay_store->read_all();
    expect(records.has_value(), "JSONL read_all should succeed");
    expect(records.value().size() == 6, "JSONL read_all should return accepted events");

    auto replay_sink = std::make_shared<CollectingBattleUpdateSink>();
    fenghuo::ap_runtime::ApRuntime replayed(replay_store, replay_sink);
    auto replay = replayed.replay(records.value());
    expect(replay.has_value(), "runtime replay should succeed");
    expect(replay_sink->published.empty(), "replay should not broadcast accepted events");

    auto snapshot = replayed.snapshot("battle-001");
    expect(snapshot.has_value(), "replay should restore battle snapshot");
    expect(snapshot.value().phase == fenghuo::core::BattlePhase::Ended,
           "replayed battle should be ended");
    expect(snapshot.value().players.at("p-blue-01").health == 90,
           "replay should restore player health");
    expect(snapshot.value().teams.at("red").score == 1, "replay should restore team score");

    auto duplicate = replayed.submit_event(normal_sequence().front());
    expect(duplicate.status == fenghuo::ap_runtime::SubmitEventResult::Status::Duplicate,
           "replayed duplicate table should reject already accepted event");

    auto records_after_duplicate = replay_store->read_all();
    expect(records_after_duplicate.has_value(), "JSONL read_all after duplicate should succeed");
    expect(records_after_duplicate.value().size() == 6,
           "duplicate after replay should not append another record");

    std::filesystem::remove_all(root);
}

void test_protocol_rejects_invalid_payload() {
    auto parsed = fenghuo::protocol::parse_event_json(
        event_json("evt-bad", "hit", 10,
                   {{"attacker_player_id", "p-red-01"}, {"target_player_id", "p-blue-01"}}));
    expect(!parsed, "protocol should reject malformed hit payload");
    expect(parsed.error().code == fenghuo::ErrorCode::MissingField,
           "protocol should report missing field");
}

void test_server_stop_unblocks_run() {
    auto store = std::make_shared<FakeBattleEventStore>();
    auto composite_sink = std::make_shared<fenghuo::ap_runtime::CompositeBattleUpdateSink>();
    auto room_composite_sink = std::make_shared<fenghuo::room_runtime::CompositeRoomUpdateSink>();
    auto broadcaster = std::make_shared<fenghuo::server::WebSocketBroadcaster>();
    composite_sink->add(broadcaster);
    room_composite_sink->add(broadcaster);
    fenghuo::ap_runtime::ApRuntime runtime(store, composite_sink);
    fenghuo::room_runtime::RoomRuntime room_runtime(
        std::make_shared<FakeRoomEventStore>(), room_composite_sink);
    boost::asio::io_context io;
    fenghuo::server::ApServer server(io, runtime, room_runtime, broadcaster, 18081);

    fenghuo::Result<void> run_result = fenghuo::Result<void>::ok();
    std::thread server_thread([&] { run_result = server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    server.stop();
    server_thread.join();
    expect(run_result.has_value(), "server stop should make run return success");
}

} // namespace

int main() {
    test_protocol_rejects_invalid_payload();
    test_runtime_sequence_and_duplicates();
    test_battle_pause_blocks_gameplay_until_resume();
    test_storage_failure_blocks_broadcast_and_commit();
    test_jsonl_store_appends_records();
    test_jsonl_replay_restores_runtime_state_and_dedupe();
    test_server_stop_unblocks_run();
    return 0;
}
