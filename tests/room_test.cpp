#include "room/protocol.hpp"
#include "room/room.hpp"
#include "room_runtime/runtime.hpp"
#include "storage/event_store.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fenghuo::room::RoomCreated created_event() {
    return {
        "room-001",
        "room-evt-001",
        1730000000001,
        "483921",
        "Friday match",
        "team_deathmatch",
        "p-red-01",
        2,
        {
            {"red", "Red", 1},
            {"blue", "Blue", 1},
        },
    };
}

fenghuo::room::RoomState created_room() {
    fenghuo::room::RoomState state;
    auto result = fenghuo::room::apply_event(state, created_event());
    if (!result) {
        throw std::runtime_error(result.error().message);
    }
    return state;
}

fenghuo::room::RoomPlayerJoined join_red(std::string event_id = "room-evt-002") {
    return {
        "room-001",
        std::move(event_id),
        1730000000002,
        "p-red-01",
        "Red 01",
        "red",
        "module-red-01",
    };
}

fenghuo::room::RoomPlayerJoined join_blue(std::string event_id = "room-evt-003") {
    return {
        "room-001",
        std::move(event_id),
        1730000000003,
        "p-blue-01",
        "Blue 01",
        "blue",
        "module-blue-01",
    };
}

void apply_or_throw(fenghuo::room::RoomState& state, const fenghuo::room::RoomEvent& event) {
    auto result = fenghuo::room::apply_event(state, event);
    if (!result) {
        throw std::runtime_error(result.error().message);
    }
}

class FakeRoomEventStore final : public fenghuo::storage::RoomEventStore {
public:
    fenghuo::Result<void> append(const fenghuo::room::RoomEventEnvelope& envelope,
                                 const fenghuo::storage::AcceptedEventMetadata&) override {
        appended.push_back(envelope.event_id);
        return fenghuo::Result<void>::ok();
    }

    std::vector<std::string> appended;
};

class FailingRoomEventStore final : public fenghuo::storage::RoomEventStore {
public:
    fenghuo::Result<void> append(const fenghuo::room::RoomEventEnvelope&,
                                 const fenghuo::storage::AcceptedEventMetadata&) override {
        return fenghuo::Result<void>::err({fenghuo::ErrorCode::StorageFailure, "forced failure"});
    }
};

class CollectingRoomUpdateSink final : public fenghuo::room_runtime::RoomUpdateSink {
public:
    void publish_room_updated(const fenghuo::room::RoomEventEnvelope& envelope,
                              const fenghuo::room::RoomSnapshot& snapshot) override {
        published.push_back(envelope.event_id);
        snapshots.push_back(snapshot);
    }

    std::vector<std::string> published;
    std::vector<fenghuo::room::RoomSnapshot> snapshots;
};

void test_create_room() {
    const auto state = created_room();
    expect(state.room_id == "room-001", "room should be created");
    expect(state.phase == fenghuo::room::RoomPhase::Open, "room should start open");
    expect(state.max_players == 2, "room max players should be set");
    expect(state.teams.size() == 2, "room should contain teams");
    expect(state.latest_room_event_id == "room-evt-001", "latest event should be tracked");
}

void test_create_rejects_invalid_capacity() {
    fenghuo::room::RoomState state;
    auto event = created_event();
    event.max_players = 3;
    auto result = fenghuo::room::apply_event(state, event);
    expect(!result, "team capacity below room capacity should be rejected");
    expect(result.error().code == fenghuo::ErrorCode::InvalidArgument,
           "invalid capacity should be an invalid argument");
}

void test_join_player_and_reject_duplicates() {
    auto state = created_room();
    apply_or_throw(state, join_red());
    expect(state.players.size() == 1, "player should join");
    expect(!state.players.at("p-red-01").ready, "joined player should start unready");

    auto duplicate = fenghuo::room::apply_event(state, join_red("room-evt-duplicate"));
    expect(!duplicate, "duplicate player should be rejected");
    expect(duplicate.error().code == fenghuo::ErrorCode::Conflict,
           "duplicate player should be conflict");
}

void test_join_rejects_duplicate_module() {
    auto state = created_room();
    apply_or_throw(state, join_red());

    auto duplicate_module = join_blue();
    duplicate_module.module_id = "module-red-01";
    auto result = fenghuo::room::apply_event(state, duplicate_module);
    expect(!result, "duplicate module should be rejected");
    expect(result.error().code == fenghuo::ErrorCode::Conflict,
           "duplicate module should be conflict");
}

void test_join_rejects_full_team_and_full_room() {
    auto state = created_room();
    apply_or_throw(state, join_red());

    auto full_team = join_blue();
    full_team.team_id = "red";
    auto team_result = fenghuo::room::apply_event(state, full_team);
    expect(!team_result, "full team should reject new player");

    apply_or_throw(state, join_blue());
    auto full_room = fenghuo::room::RoomPlayerJoined{
        "room-001", "room-evt-004", 1730000000004, "p-extra-01", "Extra", "blue", "module-extra-01"};
    auto room_result = fenghuo::room::apply_event(state, full_room);
    expect(!room_result, "full room should reject new player");
}

void test_team_change_and_ready() {
    auto state = created_room();
    auto red = join_red();
    red.team_id = "blue";
    apply_or_throw(state, red);

    apply_or_throw(state, fenghuo::room::RoomPlayerTeamChanged{
                              "room-001", "room-evt-003", 1730000000003, "p-red-01", "red"});
    expect(state.players.at("p-red-01").team_id == "red", "player should change team");

    apply_or_throw(state, fenghuo::room::RoomPlayerReadyChanged{
                              "room-001", "room-evt-004", 1730000000004, "p-red-01", true});
    expect(state.players.at("p-red-01").ready, "player should become ready");
}

void test_start_requires_ready_players() {
    auto state = created_room();
    apply_or_throw(state, join_red());

    auto result = fenghuo::room::apply_event(
        state, fenghuo::room::RoomStarted{"room-001", "room-evt-003", 1730000000003, "battle-001", 600000});
    expect(!result, "room should not start while player is unready");
    expect(state.phase == fenghuo::room::RoomPhase::Open, "failed start should not mutate room phase");
}

void test_start_freezes_roster_and_end_close_lifecycle() {
    auto state = created_room();
    apply_or_throw(state, join_red());
    apply_or_throw(state, fenghuo::room::RoomPlayerReadyChanged{
                              "room-001", "room-evt-003", 1730000000003, "p-red-01", true});
    apply_or_throw(state, fenghuo::room::RoomStarted{
                              "room-001", "room-evt-004", 1730000000004, "battle-001", 600000});
    expect(state.phase == fenghuo::room::RoomPhase::Active, "started room should be active");
    expect(state.battle_id == "battle-001", "started room should link battle");

    auto late_join = fenghuo::room::apply_event(state, join_blue("room-evt-005"));
    expect(!late_join, "late join should be rejected after start");

    auto close_active = fenghuo::room::apply_event(
        state, fenghuo::room::RoomClosed{"room-001", "room-evt-006", 1730000000006});
    expect(!close_active, "active room should not close directly");

    apply_or_throw(state, fenghuo::room::RoomEnded{"room-001", "room-evt-007", 1730000000007});
    expect(state.phase == fenghuo::room::RoomPhase::Ended, "room should end from active");

    apply_or_throw(state, fenghuo::room::RoomClosed{"room-001", "room-evt-008", 1730000000008});
    expect(state.phase == fenghuo::room::RoomPhase::Closed, "ended room should close");
}

void test_close_open_room() {
    auto state = created_room();
    apply_or_throw(state, fenghuo::room::RoomClosed{"room-001", "room-evt-002", 1730000000002});
    expect(state.phase == fenghuo::room::RoomPhase::Closed, "open room should close");
}

nlohmann::json room_event_json(std::string event_id, std::string event_type, std::uint64_t sequence,
                               nlohmann::json payload) {
    return {
        {"schema_version", 0},
        {"event_id", std::move(event_id)},
        {"event_type", std::move(event_type)},
        {"room_id", "room-001"},
        {"source_id", "operator-ui"},
        {"sequence", sequence},
        {"occurred_at_ms", 1730000000000 + static_cast<std::int64_t>(sequence)},
        {"payload", std::move(payload)},
    };
}

fenghuo::room::RoomEventEnvelope parse_room_event(nlohmann::json json) {
    auto parsed = fenghuo::room::parse_room_event_json(json.dump());
    if (!parsed) {
        throw std::runtime_error(parsed.error().message);
    }
    return std::move(parsed).value();
}

void test_room_protocol_parses_create_event() {
    auto parsed = fenghuo::room::parse_room_event_json(
        room_event_json("room-evt-json-001", "room_created", 1,
                        {{"room_code", "483921"},
                         {"name", "Friday match"},
                         {"mode", "team_deathmatch"},
                         {"host_player_id", "p-red-01"},
                         {"max_players", 2},
                         {"teams",
                          nlohmann::json::array({{{"team_id", "red"},
                                                  {"display_name", "Red"},
                                                  {"max_players", 1}},
                                                 {{"team_id", "blue"},
                                                  {"display_name", "Blue"},
                                                  {"max_players", 1}}})}})
            .dump());
    expect(parsed.has_value(), "room_created JSON should parse");

    auto event = fenghuo::room::to_room_event(parsed.value());
    expect(event.has_value(), "room_created envelope should convert to domain event");

    fenghuo::room::RoomState state;
    auto snapshot = fenghuo::room::apply_event(state, event.value());
    expect(snapshot.has_value(), "parsed room_created event should apply");
    expect(snapshot.value().teams.size() == 2, "parsed room should include teams");
}

void test_room_protocol_rejects_invalid_payload() {
    auto parsed = fenghuo::room::parse_room_event_json(
        room_event_json("room-evt-json-bad", "room_player_ready_changed", 2,
                        {{"player_id", "p-red-01"}})
            .dump());
    expect(!parsed, "room protocol should reject missing ready");
    expect(parsed.error().code == fenghuo::ErrorCode::MissingField,
           "room protocol should report missing field");
}

void test_room_protocol_rejects_unknown_event_type() {
    auto parsed = fenghuo::room::parse_room_event_json(
        room_event_json("room-evt-json-unknown", "unknown_room_event", 3, nlohmann::json::object())
            .dump());
    expect(!parsed, "room protocol should reject unknown event type");
    expect(parsed.error().code == fenghuo::ErrorCode::UnknownEventType,
           "unknown room event should report unknown event type");
}

void test_room_snapshot_json() {
    auto state = created_room();
    apply_or_throw(state, join_red());
    auto json = fenghuo::room::to_json(state);
    expect(json.at("room_id") == "room-001", "room snapshot JSON should include room_id");
    expect(json.at("phase") == "open", "room snapshot JSON should include phase");
    expect(json.at("players").at("p-red-01").at("ready") == false,
           "room snapshot JSON should include player ready state");
}

void test_room_jsonl_store_appends_records() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-room-jsonl-test";
    std::filesystem::remove_all(root);

    fenghuo::storage::JsonlRoomEventStore store(root);
    auto envelope = parse_room_event(room_event_json(
        "room-evt-jsonl-001", "room_created", 1,
        {{"room_code", "483921"},
         {"name", "Friday match"},
         {"mode", "team_deathmatch"},
         {"max_players", 2},
         {"teams",
          nlohmann::json::array({{{"team_id", "red"}, {"display_name", "Red"}, {"max_players", 1}},
                                 {{"team_id", "blue"}, {"display_name", "Blue"}, {"max_players", 1}}})}}));
    auto appended = store.append(envelope, {1, 2, 3});
    expect(appended.has_value(), "room JSONL append should succeed");

    std::ifstream input(root / "rooms" / "room-001.jsonl");
    std::string line;
    std::getline(input, line);
    auto record = nlohmann::json::parse(line);
    expect(record.at("event").at("event_id") == "room-evt-jsonl-001",
           "room JSONL should store event envelope");
    expect(record.at("metadata").at("acceptance_sequence") == 3,
           "room JSONL should store metadata");

    std::filesystem::remove_all(root);
}

void test_room_jsonl_replay_restores_room_state() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-room-jsonl-replay-test";
    std::filesystem::remove_all(root);

    std::vector<fenghuo::room::RoomEventEnvelope> envelopes = {
        parse_room_event(room_event_json(
            "room-evt-replay-001", "room_created", 1,
            {{"room_code", "483921"},
             {"name", "Friday match"},
             {"mode", "team_deathmatch"},
             {"max_players", 2},
             {"teams",
              nlohmann::json::array({{{"team_id", "red"}, {"display_name", "Red"}, {"max_players", 1}},
                                     {{"team_id", "blue"},
                                      {"display_name", "Blue"},
                                      {"max_players", 1}}})}})),
        parse_room_event(room_event_json("room-evt-replay-002", "room_player_joined", 2,
                                         {{"player_id", "p-red-01"},
                                          {"display_name", "Red 01"},
                                          {"team_id", "red"},
                                          {"module_id", "module-red-01"}})),
        parse_room_event(room_event_json("room-evt-replay-003", "room_player_ready_changed", 3,
                                         {{"player_id", "p-red-01"}, {"ready", true}})),
        parse_room_event(room_event_json("room-evt-replay-004", "room_started", 4,
                                         {{"battle_id", "battle-room-001"},
                                          {"duration_ms", 600000}})),
    };

    fenghuo::storage::JsonlRoomEventStore store(root);
    std::uint64_t sequence = 0;
    for (const auto& envelope : envelopes) {
        ++sequence;
        auto appended = store.append(envelope, {1, 2, sequence});
        expect(appended.has_value(), "seed room event should append");
    }

    auto records = store.read_all();
    expect(records.has_value(), "room JSONL read_all should succeed");
    expect(records.value().size() == envelopes.size(), "room JSONL should read all records");

    fenghuo::room::RoomState state;
    for (const auto& record : records.value()) {
        auto event = fenghuo::room::to_room_event(record.envelope);
        expect(event.has_value(), "room replay envelope should convert to domain event");
        auto applied = fenghuo::room::apply_event(state, event.value());
        expect(applied.has_value(), "room replay event should apply");
    }
    expect(state.phase == fenghuo::room::RoomPhase::Active, "room replay should restore active phase");
    expect(state.battle_id == "battle-room-001", "room replay should restore battle link");

    std::filesystem::remove_all(root);
}

std::vector<fenghuo::room::RoomEventEnvelope> normal_room_sequence() {
    return {
        parse_room_event(room_event_json(
            "room-evt-runtime-001", "room_created", 1,
            {{"room_code", "483921"},
             {"name", "Friday match"},
             {"mode", "team_deathmatch"},
             {"max_players", 2},
             {"teams",
              nlohmann::json::array({{{"team_id", "red"}, {"display_name", "Red"}, {"max_players", 1}},
                                     {{"team_id", "blue"},
                                      {"display_name", "Blue"},
                                      {"max_players", 1}}})}})),
        parse_room_event(room_event_json("room-evt-runtime-002", "room_player_joined", 2,
                                         {{"player_id", "p-red-01"},
                                          {"display_name", "Red 01"},
                                          {"team_id", "red"},
                                          {"module_id", "module-red-01"}})),
        parse_room_event(room_event_json("room-evt-runtime-003", "room_player_ready_changed", 3,
                                         {{"player_id", "p-red-01"}, {"ready", true}})),
        parse_room_event(room_event_json("room-evt-runtime-004", "room_started", 4,
                                         {{"battle_id", "battle-room-001"},
                                          {"duration_ms", 600000}})),
    };
}

void test_room_runtime_sequence_and_duplicates() {
    auto store = std::make_shared<FakeRoomEventStore>();
    auto sink = std::make_shared<CollectingRoomUpdateSink>();
    fenghuo::room_runtime::RoomRuntime runtime(store, sink);

    fenghuo::room_runtime::SubmitRoomEventResult last;
    for (const auto& envelope : normal_room_sequence()) {
        last = runtime.submit_event(envelope);
        expect(last.status == fenghuo::room_runtime::SubmitRoomEventResult::Status::Accepted,
               "normal room event should be accepted");
    }
    expect(last.snapshot.phase == fenghuo::room::RoomPhase::Active, "room should become active");
    expect(store->appended.size() == 4, "accepted room events should be stored");
    expect(sink->published.size() == 4, "accepted room events should be published");

    auto duplicate = runtime.submit_event(normal_room_sequence().front());
    expect(duplicate.status == fenghuo::room_runtime::SubmitRoomEventResult::Status::Duplicate,
           "identical room duplicate should be reported as duplicate");
    expect(store->appended.size() == 4, "room duplicate should not be stored again");
    expect(sink->published.size() == 4, "room duplicate should not publish again");
}

void test_room_runtime_storage_failure_blocks_broadcast_and_commit() {
    auto store = std::make_shared<FailingRoomEventStore>();
    auto sink = std::make_shared<CollectingRoomUpdateSink>();
    fenghuo::room_runtime::RoomRuntime runtime(store, sink);

    auto result = runtime.submit_event(normal_room_sequence().front());
    expect(result.status == fenghuo::room_runtime::SubmitRoomEventResult::Status::StorageFailed,
           "room storage failure should be surfaced");
    expect(sink->published.empty(), "room storage failure must block broadcast");

    auto snapshot = runtime.snapshot("room-001");
    expect(!snapshot, "room storage failure must block state commit");
}

void test_room_runtime_replay_restores_state_and_dedupe() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-room-runtime-replay-test";
    std::filesystem::remove_all(root);

    {
        auto store = std::make_shared<fenghuo::storage::JsonlRoomEventStore>(root);
        auto sink = std::make_shared<CollectingRoomUpdateSink>();
        fenghuo::room_runtime::RoomRuntime runtime(store, sink);
        for (const auto& envelope : normal_room_sequence()) {
            auto result = runtime.submit_event(envelope);
            expect(result.status == fenghuo::room_runtime::SubmitRoomEventResult::Status::Accepted,
                   "seed room runtime event should be accepted");
        }
    }

    auto replay_store = std::make_shared<fenghuo::storage::JsonlRoomEventStore>(root);
    auto records = replay_store->read_all();
    expect(records.has_value(), "room runtime replay read_all should succeed");
    expect(records.value().size() == 4, "room runtime replay should read all records");

    auto replay_sink = std::make_shared<CollectingRoomUpdateSink>();
    fenghuo::room_runtime::RoomRuntime replayed(replay_store, replay_sink);
    auto replay = replayed.replay(records.value());
    expect(replay.has_value(), "room runtime replay should succeed");
    expect(replay_sink->published.empty(), "room runtime replay should not publish");

    auto snapshot = replayed.snapshot("room-001");
    expect(snapshot.has_value(), "room runtime replay should restore room snapshot");
    expect(snapshot.value().phase == fenghuo::room::RoomPhase::Active,
           "room runtime replay should restore active room");

    auto duplicate = replayed.submit_event(normal_room_sequence().front());
    expect(duplicate.status == fenghuo::room_runtime::SubmitRoomEventResult::Status::Duplicate,
           "room runtime replay should restore duplicate table");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_create_room();
    test_create_rejects_invalid_capacity();
    test_join_player_and_reject_duplicates();
    test_join_rejects_duplicate_module();
    test_join_rejects_full_team_and_full_room();
    test_team_change_and_ready();
    test_start_requires_ready_players();
    test_start_freezes_roster_and_end_close_lifecycle();
    test_close_open_room();
    test_room_protocol_parses_create_event();
    test_room_protocol_rejects_invalid_payload();
    test_room_protocol_rejects_unknown_event_type();
    test_room_snapshot_json();
    test_room_jsonl_store_appends_records();
    test_room_jsonl_replay_restores_room_state();
    test_room_runtime_sequence_and_duplicates();
    test_room_runtime_storage_failure_blocks_broadcast_and_commit();
    test_room_runtime_replay_restores_state_and_dedupe();
    return 0;
}
