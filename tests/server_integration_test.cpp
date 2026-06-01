#include "ap_runtime/runtime.hpp"
#include "room_runtime/runtime.hpp"
#include "server/server.hpp"
#include "storage/event_store.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using Tcp = boost::asio::ip::tcp;

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
        {"battle_id", "battle-http-001"},
        {"source_id", "sim-http"},
        {"sequence", sequence},
        {"occurred_at_ms", 1730000010000 + static_cast<std::int64_t>(sequence)},
        {"payload", std::move(payload)},
    }.dump();
}

std::vector<std::string> normal_sequence_json() {
    return {
        event_json("http-evt-001", "player_joined", 1,
                   {{"player_id", "p-red-01"},
                    {"display_name", "Red 01"},
                    {"team_id", "red"},
                    {"module_id", "module-red-01"}}),
        event_json("http-evt-002", "player_joined", 2,
                   {{"player_id", "p-blue-01"},
                    {"display_name", "Blue 01"},
                    {"team_id", "blue"},
                    {"module_id", "module-blue-01"}}),
        event_json("http-evt-003", "battle_started", 3,
                   {{"mode", "team_deathmatch"}, {"duration_ms", 600000}}),
        event_json("http-evt-004", "shot", 4,
                   {{"player_id", "p-red-01"}, {"weapon_id", "rifle-01"}, {"ammo_after", 29}}),
        event_json("http-evt-005", "hit", 5,
                   {{"attacker_player_id", "p-red-01"},
                    {"target_player_id", "p-blue-01"},
                    {"weapon_id", "rifle-01"},
                    {"damage", 10},
                    {"hit_zone", "torso"}}),
        event_json("http-evt-006", "battle_ended", 6, {{"reason", "time_limit"}}),
    };
}

http::response<http::string_body> http_request(std::uint16_t port, http::verb method,
                                               std::string target, std::string body = {}) {
    boost::asio::io_context io;
    Tcp::resolver resolver(io);
    beast::tcp_stream stream(io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
    stream.connect(endpoints);

    http::request<http::string_body> request{method, std::move(target), 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::user_agent, "fenghuo-server-integration-test");
    if (!body.empty()) {
        request.set(http::field::content_type, "application/json");
        request.body() = std::move(body);
        request.prepare_payload();
    }

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    beast::error_code ignored;
    stream.socket().shutdown(Tcp::socket::shutdown_both, ignored);
    return response;
}

class RunningServer {
public:
    explicit RunningServer(std::filesystem::path event_log_root)
        : root_(std::move(event_log_root)),
          store_(std::make_shared<fenghuo::storage::JsonlBattleEventStore>(root_)),
          room_store_(std::make_shared<fenghuo::storage::JsonlRoomEventStore>(root_)),
          composite_sink_(std::make_shared<fenghuo::ap_runtime::CompositeBattleUpdateSink>()),
          room_composite_sink_(std::make_shared<fenghuo::room_runtime::CompositeRoomUpdateSink>()),
          broadcaster_(std::make_shared<fenghuo::server::WebSocketBroadcaster>()),
          runtime_(store_, composite_sink_),
          room_runtime_(room_store_, room_composite_sink_),
          server_(io_, runtime_, room_runtime_, broadcaster_, 0),
          port_(server_.port()) {
        broadcaster_->bind_runtimes(&runtime_, &room_runtime_);
        composite_sink_->add(broadcaster_);
        room_composite_sink_->add(broadcaster_);
        server_thread_ = std::thread([this] { run_result_ = server_.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ~RunningServer() {
        server_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    std::uint16_t port() const { return port_; }

    void expect_clean_shutdown() const {
        expect(run_result_.has_value(), "server should stop cleanly");
    }

private:
    std::filesystem::path root_;
    boost::asio::io_context io_;
    std::shared_ptr<fenghuo::storage::JsonlBattleEventStore> store_;
    std::shared_ptr<fenghuo::storage::JsonlRoomEventStore> room_store_;
    std::shared_ptr<fenghuo::ap_runtime::CompositeBattleUpdateSink> composite_sink_;
    std::shared_ptr<fenghuo::room_runtime::CompositeRoomUpdateSink> room_composite_sink_;
    std::shared_ptr<fenghuo::server::WebSocketBroadcaster> broadcaster_;
    fenghuo::ap_runtime::ApRuntime runtime_;
    fenghuo::room_runtime::RoomRuntime room_runtime_;
    fenghuo::server::ApServer server_;
    std::uint16_t port_;
    fenghuo::Result<void> run_result_{fenghuo::Result<void>::ok()};
    std::thread server_thread_;
};

void test_http_post_snapshot_and_websocket_live() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-integration-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    boost::asio::io_context ws_io;
    Tcp::resolver resolver(ws_io);
    websocket::stream<Tcp::socket> ws(ws_io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
    boost::asio::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/api/v0/live");

    for (const auto& event : normal_sequence_json()) {
        auto response = http_request(server.port(), http::verb::post, "/api/v0/events", event);
        expect(response.result() == http::status::accepted, "POST /events should accept valid event");

        beast::flat_buffer buffer;
        ws.read(buffer);
        auto message = nlohmann::json::parse(beast::buffers_to_string(buffer.data()));
        expect(message.at("type") == "accepted_event", "websocket should publish accepted_event");
        expect(message.at("event").at("event_id") ==
                   nlohmann::json::parse(event).at("event_id"),
               "websocket event should match posted event");
    }

    auto snapshot_response =
        http_request(server.port(), http::verb::get, "/api/v0/battles/battle-http-001/snapshot");
    expect(snapshot_response.result() == http::status::ok, "snapshot endpoint should return 200");
    auto snapshot_body = nlohmann::json::parse(snapshot_response.body());
    const auto& snapshot = snapshot_body.at("snapshot");
    expect(snapshot.at("phase") == "ended", "snapshot should show ended battle");
    expect(snapshot.at("players").at("p-blue-01").at("health") == 90,
           "snapshot should include hit damage");
    expect(snapshot.at("teams").at("red").at("score") == 1,
           "snapshot should include team score");

    auto duplicate_response =
        http_request(server.port(), http::verb::post, "/api/v0/events", normal_sequence_json().front());
    expect(duplicate_response.result() == http::status::ok, "duplicate event should return 200");
    auto duplicate_body = nlohmann::json::parse(duplicate_response.body());
    expect(duplicate_body.at("status") == "duplicate", "duplicate response should report status");

    std::ifstream log_file(root / "battle-http-001.jsonl");
    std::size_t lines = 0;
    for (std::string line; std::getline(log_file, line);) {
        ++lines;
    }
    expect(lines == 6, "JSONL store should contain accepted events only");

    beast::error_code ignored;
    ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    ws.next_layer().close(ignored);
    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_room_endpoints() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-room-integration-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    boost::asio::io_context ws_io;
    Tcp::resolver resolver(ws_io);
    websocket::stream<Tcp::socket> ws(ws_io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
    boost::asio::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/api/v0/live");
    const auto read_message = [&](beast::flat_buffer& local_buffer) {
        ws.read(local_buffer);
        auto message = nlohmann::json::parse(beast::buffers_to_string(local_buffer.data()));
        local_buffer.clear();
        return message;
    };
    const auto drain_messages = [&](int count) {
        beast::flat_buffer local_buffer;
        for (int index = 0; index < count; ++index) {
            read_message(local_buffer);
        }
    };

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-http-001"},
                       {"event_id", "room-http-evt-001"},
                       {"source_id", "room-http-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000020001},
                       {"name", "HTTP room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "POST /rooms should create room");
    beast::flat_buffer create_buffer;
    auto create_message = read_message(create_buffer);
    expect(create_message.at("type") == "room_updated", "websocket should publish room_updated");
    expect(create_message.at("room").at("room_id") == "room-http-001",
           "room_updated should include room");
    drain_messages(2);

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-http-001/players",
        nlohmann::json{{"event_id", "room-http-evt-002"},
                       {"source_id", "room-http-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000020002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::accepted, "POST room player should join");
    drain_messages(4);

    auto ready_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-http-001/players/p-red-01/ready",
        nlohmann::json{{"event_id", "room-http-evt-003"},
                       {"source_id", "room-http-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000020003},
                       {"ready", true}}
            .dump());
    expect(ready_response.result() == http::status::accepted, "POST room ready should update player");
    drain_messages(4);

    auto start_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-http-001/start",
        nlohmann::json{{"event_id", "room-http-evt-004"},
                       {"source_id", "room-http-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000020004},
                       {"battle_id", "battle-room-http-001"},
                       {"duration_ms", 600000}}
            .dump());
    expect(start_response.result() == http::status::accepted, "POST room start should activate room");
    auto start_body = nlohmann::json::parse(start_response.body());
    expect(start_body.at("room").at("phase") == "active", "started room response should be active");
    expect(start_body.contains("battle_snapshot"), "started room response should include battle snapshot");
    expect(start_body.at("battle_snapshot").at("phase") == "active",
           "room start should create active battle");
    expect(start_body.at("battle_snapshot").at("players").contains("p-red-01"),
           "room start should create battle player");
    drain_messages(8);

    auto room_response =
        http_request(server.port(), http::verb::get, "/api/v0/rooms/room-http-001");
    expect(room_response.result() == http::status::ok, "GET room should return room");
    auto room_body = nlohmann::json::parse(room_response.body());
    expect(room_body.at("room").at("battle_id") == "battle-room-http-001",
           "GET room should include linked battle");

    auto battle_response =
        http_request(server.port(), http::verb::get, "/api/v0/battles/battle-room-http-001/snapshot");
    expect(battle_response.result() == http::status::ok,
           "room start should make battle snapshot queryable");

    auto list_response = http_request(server.port(), http::verb::get, "/api/v0/rooms");
    expect(list_response.result() == http::status::ok, "GET rooms should return list");
    auto list_body = nlohmann::json::parse(list_response.body());
    expect(list_body.at("rooms").size() == 1, "GET rooms should include created room");

    std::ifstream log_file(root / "rooms" / "room-http-001.jsonl");
    std::size_t lines = 0;
    for (std::string line; std::getline(log_file, line);) {
        ++lines;
    }
    expect(lines == 4, "room JSONL store should contain accepted room events only");

    beast::error_code ignored;
    ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    ws.next_layer().close(ignored);
    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_room_device_endpoints() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-room-device-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-device-001"},
                       {"event_id", "room-device-evt-001"},
                       {"source_id", "room-device-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000030001},
                       {"name", "Device room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "device room should be created");

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-device-001/players",
        nlohmann::json{{"event_id", "room-device-evt-002"},
                       {"source_id", "room-device-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000030002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::accepted, "device room player should join");

    auto register_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-device-001/devices",
        nlohmann::json{{"event_id", "room-device-evt-003"},
                       {"source_id", "room-device-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000030003},
                       {"device_id", "device-head-red-01"},
                       {"device_kind", "headset_receiver"},
                       {"display_name", "Red Headset"},
                       {"battery_percent", 91},
                       {"signal_strength", 82}}
            .dump());
    expect(register_response.result() == http::status::accepted, "device should register");

    auto bind_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-device-001/devices/device-head-red-01/bind",
        nlohmann::json{{"event_id", "room-device-evt-004"},
                       {"source_id", "room-device-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000030004},
                       {"player_id", "p-red-01"}}
            .dump());
    expect(bind_response.result() == http::status::accepted, "device should bind to player");
    auto bind_body = nlohmann::json::parse(bind_response.body());
    expect(bind_body.at("room").at("devices").at("device-head-red-01").at("bound_player_id") == "p-red-01",
           "bind response should include bound player");

    auto heartbeat_response = http_request(
        server.port(), http::verb::post,
        "/api/v0/rooms/room-device-001/devices/device-head-red-01/heartbeat",
        nlohmann::json{{"event_id", "room-device-evt-005"},
                       {"source_id", "room-device-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000030005},
                       {"battery_percent", 88},
                       {"signal_strength", 74},
                       {"online", false}}
            .dump());
    expect(heartbeat_response.result() == http::status::accepted, "device heartbeat should update");
    auto heartbeat_body = nlohmann::json::parse(heartbeat_response.body());
    expect(heartbeat_body.at("room").at("devices").at("device-head-red-01").at("online") == false,
           "heartbeat should update online state");

    auto list_response = http_request(server.port(), http::verb::get, "/api/v0/rooms/room-device-001/devices");
    expect(list_response.result() == http::status::ok, "GET room devices should return list");
    auto list_body = nlohmann::json::parse(list_response.body());
    expect(list_body.at("devices").at("device-head-red-01").at("battery_percent") == 88,
           "GET room devices should include latest battery");

    auto unbind_response = http_request(
        server.port(), http::verb::post,
        "/api/v0/rooms/room-device-001/devices/device-head-red-01/unbind",
        nlohmann::json{{"event_id", "room-device-evt-006"},
                       {"source_id", "room-device-test"},
                       {"sequence", 6},
                       {"occurred_at_ms", 1730000030006}}
            .dump());
    expect(unbind_response.result() == http::status::accepted, "device should unbind");
    auto unbind_body = nlohmann::json::parse(unbind_response.body());
    expect(unbind_body.at("room").at("devices").at("device-head-red-01").at("bound_player_id").is_null(),
           "unbind response should clear bound player");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_room_map_endpoints() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-room-map-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-map-001"},
                       {"event_id", "room-map-evt-001"},
                       {"source_id", "room-map-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000040001},
                       {"name", "Map room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "map room should be created");

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-001/players",
        nlohmann::json{{"event_id", "room-map-evt-002"},
                       {"source_id", "room-map-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000040002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::accepted, "map room player should join");

    auto register_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-001/devices",
        nlohmann::json{{"event_id", "room-map-evt-003"},
                       {"source_id", "room-map-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000040003},
                       {"device_id", "device-head-red-01"},
                       {"device_kind", "headset_receiver"},
                       {"display_name", "Red Headset"}}
            .dump());
    expect(register_response.result() == http::status::accepted, "map room device should register");

    auto bind_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-001/devices/device-head-red-01/bind",
        nlohmann::json{{"event_id", "room-map-evt-004"},
                       {"source_id", "room-map-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000040004},
                       {"player_id", "p-red-01"}}
            .dump());
    expect(bind_response.result() == http::status::accepted, "map room device should bind");

    auto position_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-001/positions",
        nlohmann::json{{"event_id", "room-map-evt-005"},
                       {"source_id", "room-map-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000040005},
                       {"player_id", "p-red-01"},
                       {"source_device_id", "device-head-red-01"},
                       {"x", 12.5},
                       {"y", 8.25},
                       {"heading_deg", -90.0},
                       {"velocity_mps", 1.5}}
            .dump());
    expect(position_response.result() == http::status::accepted, "position update should be accepted");
    auto position_body = nlohmann::json::parse(position_response.body());
    expect(position_body.at("room").at("positions").at("p-red-01").at("heading_deg") == 270.0,
           "position response should normalize heading");

    auto map_response = http_request(server.port(), http::verb::get, "/api/v0/rooms/room-map-001/map");
    expect(map_response.result() == http::status::ok, "GET room map should return snapshot");
    auto map_body = nlohmann::json::parse(map_response.body());
    expect(map_body.at("positions").at("p-red-01").at("x") == 12.5,
           "room map should include latest x position");
    expect(map_body.at("positions").at("p-red-01").at("source_device_id") == "device-head-red-01",
           "room map should include source device");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_websocket_map_updated_message() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-map-websocket-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    boost::asio::io_context ws_io;
    Tcp::resolver resolver(ws_io);
    websocket::stream<Tcp::socket> ws(ws_io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
    boost::asio::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/api/v0/live");
    const auto read_message = [&](beast::flat_buffer& local_buffer) {
        ws.read(local_buffer);
        auto message = nlohmann::json::parse(beast::buffers_to_string(local_buffer.data()));
        local_buffer.clear();
        return message;
    };
    const auto drain_messages = [&](int count) {
        beast::flat_buffer local_buffer;
        for (int index = 0; index < count; ++index) {
            read_message(local_buffer);
        }
    };

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-map-ws-001"},
                       {"event_id", "room-map-ws-evt-001"},
                       {"source_id", "room-map-ws-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000050001},
                       {"name", "Map websocket room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "map websocket room should be created");
    drain_messages(3);

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-ws-001/players",
        nlohmann::json{{"event_id", "room-map-ws-evt-002"},
                       {"source_id", "room-map-ws-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000050002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::accepted, "map websocket player should join");
    drain_messages(4);

    auto register_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-ws-001/devices",
        nlohmann::json{{"event_id", "room-map-ws-evt-003"},
                       {"source_id", "room-map-ws-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000050003},
                       {"device_id", "device-head-red-01"},
                       {"device_kind", "headset_receiver"},
                       {"display_name", "Red Headset"}}
            .dump());
    expect(register_response.result() == http::status::accepted, "map websocket device should register");
    drain_messages(3);

    auto bind_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-ws-001/devices/device-head-red-01/bind",
        nlohmann::json{{"event_id", "room-map-ws-evt-004"},
                       {"source_id", "room-map-ws-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000050004},
                       {"player_id", "p-red-01"}}
            .dump());
    expect(bind_response.result() == http::status::accepted, "map websocket device should bind");
    drain_messages(4);

    auto position_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-map-ws-001/positions",
        nlohmann::json{{"event_id", "room-map-ws-evt-005"},
                       {"source_id", "room-map-ws-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000050005},
                       {"player_id", "p-red-01"},
                       {"source_device_id", "device-head-red-01"},
                       {"x", 12.5},
                       {"y", 8.25},
                       {"heading_deg", 180.0},
                       {"velocity_mps", 1.5}}
            .dump());
    expect(position_response.result() == http::status::accepted, "map websocket position should update");

    beast::flat_buffer buffer;
    bool room_updated_seen = false;
    bool room_summary_seen = false;
    bool room_detail_seen = false;
    bool player_status_seen = false;
    std::optional<nlohmann::json> map_message;
    while (!room_updated_seen || !room_summary_seen || !room_detail_seen || !player_status_seen ||
           !map_message.has_value()) {
        auto message = read_message(buffer);
        const auto type = message.at("type").get<std::string>();
        if (type == "room_updated") {
            room_updated_seen = true;
        } else if (type == "room_summary_updated") {
            room_summary_seen = true;
        } else if (type == "room_detail_updated") {
            room_detail_seen = true;
        } else if (type == "player_status_updated") {
            player_status_seen = true;
        } else if (type == "map_updated") {
            map_message = std::move(message);
        }
    }
    expect(map_message->at("type") == "map_updated", "position update should publish map_updated");
    expect(map_message->at("room_id") == "room-map-ws-001", "map_updated should include room id");
    expect(map_message->at("positions").at("p-red-01").at("x") == 12.5,
           "map_updated should include latest player position");

    beast::error_code ignored;
    ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    ws.next_layer().close(ignored);
    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_websocket_app_aggregate_messages() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-websocket-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    boost::asio::io_context ws_io;
    Tcp::resolver resolver(ws_io);
    websocket::stream<Tcp::socket> ws(ws_io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
    boost::asio::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/api/v0/live");
    const auto read_message = [&](beast::flat_buffer& local_buffer) {
        ws.read(local_buffer);
        auto message = nlohmann::json::parse(beast::buffers_to_string(local_buffer.data()));
        local_buffer.clear();
        return message;
    };

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-app-ws-001"},
                       {"event_id", "room-app-ws-evt-001"},
                       {"source_id", "room-app-ws-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000080001},
                       {"name", "App websocket room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "app websocket room should be created");

    beast::flat_buffer buffer;
    auto room_updated = read_message(buffer);
    expect(room_updated.at("type") == "room_updated", "first message should remain room_updated");

    auto room_summary = read_message(buffer);
    expect(room_summary.at("type") == "room_summary_updated",
           "second message should be room_summary_updated");
    expect(room_summary.at("room").at("room_id") == "room-app-ws-001",
           "room_summary_updated should include app room summary");

    auto room_detail = read_message(buffer);
    expect(room_detail.at("type") == "room_detail_updated",
           "third message should be room_detail_updated");
    expect(room_detail.at("room").at("phase") == "open",
           "room_detail_updated should include app room detail");
    buffer.clear();

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-app-ws-001/players",
        nlohmann::json{{"event_id", "room-app-ws-evt-002"},
                       {"source_id", "room-app-ws-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000080002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::accepted, "app websocket player should join");

    expect(read_message(buffer).at("type") == "room_updated",
           "join should still publish room_updated");

    expect(read_message(buffer).at("type") == "room_summary_updated",
           "join should publish room_summary_updated");

    expect(read_message(buffer).at("type") == "room_detail_updated",
           "join should publish room_detail_updated");

    auto player_status = read_message(buffer);
    expect(player_status.at("type") == "player_status_updated",
           "join should publish player_status_updated");
    expect(player_status.at("player_id") == "p-red-01",
           "player_status_updated should include target player id");
    expect(player_status.at("status").at("display_name") == "Red 01",
           "player_status_updated should use app player status shape");

    auto ready_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-app-ws-001/players/p-red-01/ready",
        nlohmann::json{{"event_id", "room-app-ws-evt-003"},
                       {"source_id", "room-app-ws-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000080003},
                       {"ready", true}}
            .dump());
    expect(ready_response.result() == http::status::accepted, "app websocket player should become ready");

    expect(read_message(buffer).at("type") == "room_updated",
           "ready change should publish room_updated");
    expect(read_message(buffer).at("type") == "room_summary_updated",
           "ready change should publish room_summary_updated");
    expect(read_message(buffer).at("type") == "room_detail_updated",
           "ready change should publish room_detail_updated");
    auto ready_status = read_message(buffer);
    expect(ready_status.at("type") == "player_status_updated",
           "ready change should publish player_status_updated");
    expect(ready_status.at("status").at("ready") == true,
           "player_status_updated should include latest ready state");

    beast::error_code ignored;
    ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    ws.next_layer().close(ignored);
    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_websocket_player_status_updated_from_battle() {
    const auto root =
        std::filesystem::temp_directory_path() / "fenghuo-server-player-status-battle-ws-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    boost::asio::io_context ws_io;
    Tcp::resolver resolver(ws_io);
    websocket::stream<Tcp::socket> ws(ws_io);
    auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
    boost::asio::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/api/v0/live");
    const auto read_message = [&](beast::flat_buffer& local_buffer) {
        ws.read(local_buffer);
        auto message = nlohmann::json::parse(beast::buffers_to_string(local_buffer.data()));
        local_buffer.clear();
        return message;
    };

    const auto post_room = [&](http::verb method, const std::string& target, nlohmann::json body,
                               http::status expected, int message_count) {
        auto response = http_request(server.port(), method, target, body.dump());
        expect(response.result() == expected, "room setup request should succeed");
        beast::flat_buffer local_buffer;
        for (int index = 0; index < message_count; ++index) {
            read_message(local_buffer);
        }
    };

    post_room(http::verb::post, "/api/v0/rooms",
              {{"room_id", "room-battle-ws-001"},
               {"event_id", "room-battle-ws-evt-001"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 1},
               {"occurred_at_ms", 1730000090001},
               {"name", "Battle websocket room"},
               {"mode", "team_deathmatch"},
               {"max_players", 2},
               {"teams",
                nlohmann::json::array({{{"team_id", "red"}, {"display_name", "Red"}, {"max_players", 1}},
                                       {{"team_id", "blue"}, {"display_name", "Blue"}, {"max_players", 1}}})}},
              http::status::created, 3);
    post_room(http::verb::post, "/api/v0/rooms/room-battle-ws-001/players",
              {{"event_id", "room-battle-ws-evt-002"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 2},
               {"occurred_at_ms", 1730000090002},
               {"player_id", "p-red-01"},
               {"display_name", "Red 01"},
               {"team_id", "red"},
               {"module_id", "module-red-01"}},
              http::status::accepted, 4);
    post_room(http::verb::post, "/api/v0/rooms/room-battle-ws-001/players",
              {{"event_id", "room-battle-ws-evt-003"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 3},
               {"occurred_at_ms", 1730000090003},
               {"player_id", "p-blue-01"},
               {"display_name", "Blue 01"},
               {"team_id", "blue"},
               {"module_id", "module-blue-01"}},
              http::status::accepted, 4);
    post_room(http::verb::post, "/api/v0/rooms/room-battle-ws-001/players/p-red-01/ready",
              {{"event_id", "room-battle-ws-evt-004"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 4},
               {"occurred_at_ms", 1730000090004},
               {"ready", true}},
              http::status::accepted, 4);
    post_room(http::verb::post, "/api/v0/rooms/room-battle-ws-001/players/p-blue-01/ready",
              {{"event_id", "room-battle-ws-evt-005"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 5},
               {"occurred_at_ms", 1730000090005},
               {"ready", true}},
              http::status::accepted, 4);
    post_room(http::verb::post, "/api/v0/rooms/room-battle-ws-001/start",
              {{"event_id", "room-battle-ws-evt-006"},
               {"source_id", "room-battle-ws-test"},
               {"sequence", 6},
               {"occurred_at_ms", 1730000090006},
               {"battle_id", "battle-battle-ws-001"},
               {"duration_ms", 600000}},
              http::status::accepted, 12);

    auto hit_response = http_request(
        server.port(), http::verb::post, "/api/v0/events",
        nlohmann::json{
            {"schema_version", 0},
            {"event_id", "battle-ws-hit-001"},
            {"event_type", "hit"},
            {"battle_id", "battle-battle-ws-001"},
            {"source_id", "sim-http"},
            {"sequence", 10},
            {"occurred_at_ms", 1730000090010},
            {"payload",
             {{"attacker_player_id", "p-red-01"},
              {"target_player_id", "p-blue-01"},
              {"weapon_id", "rifle-01"},
              {"damage", 10},
              {"hit_zone", "torso"}}},
        }
            .dump());
    expect(hit_response.result() == http::status::accepted, "battle hit should be accepted");

    beast::flat_buffer buffer;
    auto accepted_event = read_message(buffer);
    expect(accepted_event.at("type") == "accepted_event",
           "battle event should still publish accepted_event first");

    std::optional<nlohmann::json> attacker_status;
    std::optional<nlohmann::json> target_status;
    while (!attacker_status.has_value() || !target_status.has_value()) {
        auto status_message = read_message(buffer);
        expect(status_message.at("type") == "player_status_updated",
               "battle hit should publish player_status_updated");
        const auto player_id = status_message.at("player_id").get<std::string>();
        if (player_id == "p-red-01") {
            attacker_status = std::move(status_message);
        } else if (player_id == "p-blue-01") {
            target_status = std::move(status_message);
        }
    }

    expect(attacker_status->at("player_id") == "p-red-01",
           "battle hit should publish attacker player_status_updated");
    expect(target_status->at("player_id") == "p-blue-01",
           "battle hit should publish target player_status_updated");
    expect(target_status->at("status").at("health") == 90,
           "battle player_status_updated should include reduced health");
    expect(target_status->at("status").at("alive") == true,
           "battle player_status_updated should include alive state");

    beast::error_code ignored;
    ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    ws.next_layer().close(ignored);
    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_aggregate_queries() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-aggregate-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-001"},
                              {"event_id", "room-app-evt-001"},
                              {"source_id", "room-app-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000060001},
                              {"name", "App room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/players",
               nlohmann::json{{"event_id", "room-app-evt-002"},
                              {"source_id", "room-app-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000060002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::accepted,
           "app player should join");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/devices",
               nlohmann::json{{"event_id", "room-app-evt-003"},
                              {"source_id", "room-app-test"},
                              {"sequence", 3},
                              {"occurred_at_ms", 1730000060003},
                              {"device_id", "device-head-red-01"},
                              {"device_kind", "headset_receiver"},
                              {"display_name", "Red Headset"}}
                   .dump())
               .result() == http::status::accepted,
           "app device should register");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/devices/device-head-red-01/bind",
               nlohmann::json{{"event_id", "room-app-evt-004"},
                              {"source_id", "room-app-test"},
                              {"sequence", 4},
                              {"occurred_at_ms", 1730000060004},
                              {"player_id", "p-red-01"}}
                   .dump())
               .result() == http::status::accepted,
           "app device should bind");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/positions",
               nlohmann::json{{"event_id", "room-app-evt-005"},
                              {"source_id", "room-app-test"},
                              {"sequence", 5},
                              {"occurred_at_ms", 1730000060005},
                              {"player_id", "p-red-01"},
                              {"source_device_id", "device-head-red-01"},
                              {"x", 12.5},
                              {"y", 8.25},
                              {"heading_deg", 180.0},
                              {"velocity_mps", 1.5}}
                   .dump())
               .result() == http::status::accepted,
           "app position should update");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/players/p-red-01/ready",
               nlohmann::json{{"event_id", "room-app-evt-006"},
                              {"source_id", "room-app-test"},
                              {"sequence", 6},
                              {"occurred_at_ms", 1730000060006},
                              {"ready", true}}
                   .dump())
               .result() == http::status::accepted,
           "app player should become ready");

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms/room-app-001/start",
               nlohmann::json{{"event_id", "room-app-evt-007"},
                              {"source_id", "room-app-test"},
                              {"sequence", 7},
                              {"occurred_at_ms", 1730000060007},
                              {"battle_id", "battle-app-001"},
                              {"duration_ms", 600000}}
                   .dump())
               .result() == http::status::accepted,
           "app room should start");

    auto rooms_response = http_request(server.port(), http::verb::get, "/api/v1/rooms");
    expect(rooms_response.result() == http::status::ok, "GET /api/v1/rooms should return 200");
    auto rooms_body = nlohmann::json::parse(rooms_response.body());
    expect(rooms_body.at("rooms").size() == 1, "app room list should include one room");
    expect(rooms_body.at("rooms").at(0).at("player_count") == 1, "app room list should include player count");

    auto room_response = http_request(server.port(), http::verb::get, "/api/v1/rooms/room-app-001");
    expect(room_response.result() == http::status::ok, "GET /api/v1/rooms/{room_id} should return 200");
    auto room_body = nlohmann::json::parse(room_response.body());
    expect(room_body.at("players").at("p-red-01").at("team_id") == "red",
           "app room detail should include player state");
    expect(room_body.at("devices").at("device-head-red-01").at("bound_player_id") == "p-red-01",
           "app room detail should include bound device");

    auto map_response = http_request(server.port(), http::verb::get, "/api/v1/rooms/room-app-001/map");
    expect(map_response.result() == http::status::ok, "GET /api/v1/rooms/{room_id}/map should return 200");
    auto map_body = nlohmann::json::parse(map_response.body());
    expect(map_body.at("positions").at("p-red-01").at("x") == 12.5,
           "app map query should include latest position");

    auto status_response = http_request(server.port(), http::verb::get, "/api/v1/players/p-red-01/status");
    expect(status_response.result() == http::status::ok, "GET /api/v1/players/{player_id}/status should return 200");
    auto status_body = nlohmann::json::parse(status_response.body());
    expect(status_body.at("device_online") == true, "player status should include device online state");
    expect(status_body.at("position").at("source_device_id") == "device-head-red-01",
           "player status should include latest position");
    expect(status_body.at("alive") == true, "player status should include battle alive state");
    expect(status_body.at("health") == 100, "player status should include battle health");

    auto battle_response = http_request(server.port(), http::verb::get, "/api/v1/battles/battle-app-001");
    expect(battle_response.result() == http::status::ok, "GET /api/v1/battles/{battle_id} should return 200");
    auto battle_body = nlohmann::json::parse(battle_response.body());
    expect(battle_body.at("battle_id") == "battle-app-001", "app battle query should include battle id");
    expect(battle_body.at("phase") == "active", "app battle query should include battle phase");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_join_and_leave_commands() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-join-leave-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms",
        nlohmann::json{{"room_id", "room-app-cmd-001"},
                       {"event_id", "room-app-cmd-evt-001"},
                       {"source_id", "room-app-cmd-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000070001},
                       {"name", "App command room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 2},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 1}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 1}}})}}
            .dump());
    expect(create_response.result() == http::status::created, "app command room should be created");

    auto join_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-cmd-001/join",
        nlohmann::json{{"event_id", "room-app-cmd-evt-002"},
                       {"source_id", "room-app-cmd-test"},
                       {"sequence", 2},
                       {"occurred_at_ms", 1730000070002},
                       {"player_id", "p-red-01"},
                       {"display_name", "Red 01"},
                       {"team_id", "red"},
                       {"module_id", "module-red-01"}}
            .dump());
    expect(join_response.result() == http::status::ok, "app join should return 200");
    auto join_body = nlohmann::json::parse(join_response.body());
    expect(join_body.at("players").at("p-red-01").at("display_name") == "Red 01",
           "app join should return room detail with player");

    auto leave_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-cmd-001/leave",
        nlohmann::json{{"event_id", "room-app-cmd-evt-003"},
                       {"source_id", "room-app-cmd-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000070003},
                       {"player_id", "p-red-01"}}
            .dump());
    expect(leave_response.result() == http::status::ok, "app leave should return 200");
    auto leave_body = nlohmann::json::parse(leave_response.body());
    expect(!leave_body.at("players").contains("p-red-01"),
           "app leave should return room detail without player");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_create_room_command() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-create-room-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    auto create_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms",
        nlohmann::json{{"event_id", "room-app-create-evt-001"},
                       {"source_id", "room-app-create-test"},
                       {"sequence", 1},
                       {"occurred_at_ms", 1730000130001},
                       {"room_id", "room-app-create-001"},
                       {"name", "App create room"},
                       {"mode", "team_deathmatch"},
                       {"max_players", 4},
                       {"teams",
                        nlohmann::json::array({{{"team_id", "red"},
                                                {"display_name", "Red"},
                                                {"max_players", 2}},
                                               {{"team_id", "blue"},
                                                {"display_name", "Blue"},
                                                {"max_players", 2}}})}}
            .dump());
    expect(create_response.result() == http::status::ok, "app create room should return 200");
    auto create_body = nlohmann::json::parse(create_response.body());
    expect(create_body.at("room").at("room_id") == "room-app-create-001",
           "app create room should return room detail");
    expect(create_body.at("room").at("phase") == "open",
           "app create room should open room");

    auto list_response = http_request(server.port(), http::verb::get, "/api/v1/rooms");
    expect(list_response.result() == http::status::ok, "app room list should return 200");
    auto list_body = nlohmann::json::parse(list_response.body());
    expect(list_body.at("rooms").size() == 1, "app room list should include created room");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_position_command() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-position-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-position-001"},
                              {"event_id", "room-app-position-evt-001"},
                              {"source_id", "room-app-position-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000110001},
                              {"name", "App position room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app position room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-position-001/join",
               nlohmann::json{{"event_id", "room-app-position-evt-002"},
                              {"source_id", "room-app-position-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000110002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "app position player should join");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-position-001/devices",
               nlohmann::json{{"event_id", "room-app-position-evt-003"},
                              {"source_id", "room-app-position-test"},
                              {"sequence", 3},
                              {"occurred_at_ms", 1730000110003},
                              {"device_id", "device-head-red-01"},
                              {"device_kind", "headset_receiver"},
                              {"display_name", "Red Headset"}}
                   .dump())
               .result() == http::status::ok,
           "app position device should register");

    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-position-001/devices/device-head-red-01/bind",
               nlohmann::json{{"event_id", "room-app-position-evt-004"},
                              {"source_id", "room-app-position-test"},
                              {"sequence", 4},
                              {"occurred_at_ms", 1730000110004},
                              {"player_id", "p-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "app position device should bind");

    auto position_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-position-001/positions",
        nlohmann::json{{"event_id", "room-app-position-evt-005"},
                       {"source_id", "room-app-position-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000110005},
                       {"player_id", "p-red-01"},
                       {"source_device_id", "device-head-red-01"},
                       {"x", 42.25},
                       {"y", 19.75},
                       {"heading_deg", 450.0},
                       {"velocity_mps", 2.5}}
            .dump());
    expect(position_response.result() == http::status::ok, "app position command should return 200");
    auto position_body = nlohmann::json::parse(position_response.body());
    expect(position_body.at("positions").at("p-red-01").at("x") == 42.25,
           "app position command should return room detail with latest x");
    expect(position_body.at("positions").at("p-red-01").at("heading_deg") == 90.0,
           "app position command should return normalized heading");

    auto map_response =
        http_request(server.port(), http::verb::get, "/api/v1/rooms/room-app-position-001/map");
    expect(map_response.result() == http::status::ok, "app map query should return 200 after position write");
    auto map_body = nlohmann::json::parse(map_response.body());
    expect(map_body.at("positions").at("p-red-01").at("source_device_id") == "device-head-red-01",
           "app map query should include position source device");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_ready_start_and_close_commands() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-room-ops-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-ops-001"},
                              {"event_id", "room-app-ops-evt-001"},
                              {"source_id", "room-app-ops-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000100001},
                              {"name", "App ops room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app ops room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-ops-001/join",
               nlohmann::json{{"event_id", "room-app-ops-evt-002"},
                              {"source_id", "room-app-ops-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000100002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "app ops join should succeed");

    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-ops-001/players/p-red-01/ready",
               nlohmann::json{{"event_id", "room-app-ops-evt-003"},
                              {"source_id", "room-app-ops-test"},
                              {"sequence", 3},
                              {"occurred_at_ms", 1730000100003},
                              {"ready", true}}
                   .dump())
               .result() == http::status::ok,
           "app ops ready should succeed");

    auto ready_body = nlohmann::json::parse(http_request(
                                                server.port(), http::verb::post,
                                                "/api/v1/rooms/room-app-ops-001/players/p-red-01/ready",
                                                nlohmann::json{{"event_id", "room-app-ops-evt-003"},
                                                               {"source_id", "room-app-ops-test"},
                                                               {"sequence", 3},
                                                               {"occurred_at_ms", 1730000100003},
                                                               {"ready", true}}
                                                    .dump())
                                                .body());
    expect(ready_body.at("players").at("p-red-01").at("ready") == true,
           "app ready response should include ready player");

    auto start_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-ops-001/start",
        nlohmann::json{{"event_id", "room-app-ops-evt-004"},
                       {"source_id", "room-app-ops-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000100004},
                       {"battle_id", "battle-app-ops-001"},
                       {"duration_ms", 600000}}
            .dump());
    expect(start_response.result() == http::status::ok, "app start should return 200");
    auto start_body = nlohmann::json::parse(start_response.body());
    expect(start_body.at("room").at("phase") == "active", "app start should return active room");
    expect(start_body.at("battle_snapshot").at("battle_id") == "battle-app-ops-001",
           "app start should include battle snapshot");

    auto close_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-ops-001/close",
        nlohmann::json{{"event_id", "room-app-ops-evt-005"},
                       {"source_id", "room-app-ops-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000100005}}
            .dump());
    expect(close_response.result() == http::status::conflict,
           "app close should respect room phase constraints");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_device_commands() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-device-ops-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-device-ops-001"},
                              {"event_id", "room-app-device-ops-evt-001"},
                              {"source_id", "room-app-device-ops-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000110001},
                              {"name", "App device ops room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app device ops room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-device-ops-001/join",
               nlohmann::json{{"event_id", "room-app-device-ops-evt-002"},
                              {"source_id", "room-app-device-ops-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000110002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "app device ops join should succeed");

    auto register_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-device-ops-001/devices",
        nlohmann::json{{"event_id", "room-app-device-ops-evt-003"},
                       {"source_id", "room-app-device-ops-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000110003},
                       {"device_id", "device-head-red-01"},
                       {"device_kind", "headset_receiver"},
                       {"display_name", "Red Headset"},
                       {"battery_percent", 91},
                       {"signal_strength", 82}}
            .dump());
    expect(register_response.result() == http::status::ok, "app device register should return 200");
    auto register_body = nlohmann::json::parse(register_response.body());
    expect(register_body.at("devices").at("device-head-red-01").at("device_kind") == "headset_receiver",
           "app device register should return room detail with device");

    auto bind_response = http_request(
        server.port(), http::verb::post,
        "/api/v1/rooms/room-app-device-ops-001/devices/device-head-red-01/bind",
        nlohmann::json{{"event_id", "room-app-device-ops-evt-004"},
                       {"source_id", "room-app-device-ops-test"},
                       {"sequence", 4},
                       {"occurred_at_ms", 1730000110004},
                       {"player_id", "p-red-01"}}
            .dump());
    expect(bind_response.result() == http::status::ok, "app device bind should return 200");
    auto bind_body = nlohmann::json::parse(bind_response.body());
    expect(bind_body.at("devices").at("device-head-red-01").at("bound_player_id") == "p-red-01",
           "app device bind should include bound player");

    auto heartbeat_response = http_request(
        server.port(), http::verb::post,
        "/api/v1/rooms/room-app-device-ops-001/devices/device-head-red-01/heartbeat",
        nlohmann::json{{"event_id", "room-app-device-ops-evt-005"},
                       {"source_id", "room-app-device-ops-test"},
                       {"sequence", 5},
                       {"occurred_at_ms", 1730000110005},
                       {"battery_percent", 88},
                       {"signal_strength", 75},
                       {"online", false}}
            .dump());
    expect(heartbeat_response.result() == http::status::ok, "app device heartbeat should return 200");
    auto heartbeat_body = nlohmann::json::parse(heartbeat_response.body());
    expect(heartbeat_body.at("devices").at("device-head-red-01").at("online") == false,
           "app device heartbeat should update online state");

    auto unbind_response = http_request(
        server.port(), http::verb::post,
        "/api/v1/rooms/room-app-device-ops-001/devices/device-head-red-01/unbind",
        nlohmann::json{{"event_id", "room-app-device-ops-evt-006"},
                       {"source_id", "room-app-device-ops-test"},
                       {"sequence", 6},
                       {"occurred_at_ms", 1730000110006}}
            .dump());
    expect(unbind_response.result() == http::status::ok, "app device unbind should return 200");
    auto unbind_body = nlohmann::json::parse(unbind_response.body());
    expect(unbind_body.at("devices").at("device-head-red-01").at("bound_player_id").is_null(),
           "app device unbind should clear bound player");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_battle_operator_commands() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-app-battle-ops-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-battle-ops-001"},
                              {"event_id", "room-app-battle-ops-evt-001"},
                              {"source_id", "room-app-battle-ops-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000120001},
                              {"name", "App battle ops room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app battle ops room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-ops-001/join",
               nlohmann::json{{"event_id", "room-app-battle-ops-evt-002"},
                              {"source_id", "room-app-battle-ops-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000120002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "red player should join");
    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-ops-001/join",
               nlohmann::json{{"event_id", "room-app-battle-ops-evt-003"},
                              {"source_id", "room-app-battle-ops-test"},
                              {"sequence", 3},
                              {"occurred_at_ms", 1730000120003},
                              {"player_id", "p-blue-01"},
                              {"display_name", "Blue 01"},
                              {"team_id", "blue"},
                              {"module_id", "module-blue-01"}}
                   .dump())
               .result() == http::status::ok,
           "blue player should join");

    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-battle-ops-001/players/p-red-01/ready",
               nlohmann::json{{"event_id", "room-app-battle-ops-evt-004"},
                              {"source_id", "room-app-battle-ops-test"},
                              {"sequence", 4},
                              {"occurred_at_ms", 1730000120004},
                              {"ready", true}}
                   .dump())
               .result() == http::status::ok,
           "red player should become ready");
    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-battle-ops-001/players/p-blue-01/ready",
               nlohmann::json{{"event_id", "room-app-battle-ops-evt-005"},
                              {"source_id", "room-app-battle-ops-test"},
                              {"sequence", 5},
                              {"occurred_at_ms", 1730000120005},
                              {"ready", true}}
                   .dump())
               .result() == http::status::ok,
           "blue player should become ready");

    auto start_response = http_request(
        server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-ops-001/start",
        nlohmann::json{{"event_id", "room-app-battle-ops-evt-006"},
                       {"source_id", "room-app-battle-ops-test"},
                       {"sequence", 6},
                       {"occurred_at_ms", 1730000120006},
                       {"battle_id", "battle-app-battle-ops-001"},
                       {"duration_ms", 600000}}
            .dump());
    expect(start_response.result() == http::status::ok, "battle ops room should start");

    auto pause_response = http_request(
        server.port(), http::verb::post, "/api/v1/battles/battle-app-battle-ops-001/pause",
        nlohmann::json{{"event_id", "room-app-battle-ops-evt-007"},
                       {"source_id", "room-app-battle-ops-test"},
                       {"sequence", 7},
                       {"occurred_at_ms", 1730000120007},
                       {"reason", "operator"}}
            .dump());
    expect(pause_response.result() == http::status::ok, "app battle pause should return 200");
    auto pause_body = nlohmann::json::parse(pause_response.body());
    expect(pause_body.at("phase") == "paused", "pause should return paused battle");

    auto resume_response = http_request(
        server.port(), http::verb::post, "/api/v1/battles/battle-app-battle-ops-001/resume",
        nlohmann::json{{"event_id", "room-app-battle-ops-evt-008"},
                       {"source_id", "room-app-battle-ops-test"},
                       {"sequence", 8},
                       {"occurred_at_ms", 1730000120008}}
            .dump());
    expect(resume_response.result() == http::status::ok, "app battle resume should return 200");
    auto resume_body = nlohmann::json::parse(resume_response.body());
    expect(resume_body.at("phase") == "active", "resume should return active battle");

    auto end_response = http_request(
        server.port(), http::verb::post, "/api/v1/battles/battle-app-battle-ops-001/end",
        nlohmann::json{{"event_id", "room-app-battle-ops-evt-009"},
                       {"source_id", "room-app-battle-ops-test"},
                       {"sequence", 9},
                       {"occurred_at_ms", 1730000120009},
                       {"reason", "manual"}}
            .dump());
    expect(end_response.result() == http::status::ok, "app battle end should return 200");
    auto end_body = nlohmann::json::parse(end_response.body());
    expect(end_body.at("phase") == "ended", "end should return ended battle");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_app_battle_shot_and_hit_commands() {
    const auto root =
        std::filesystem::temp_directory_path() / "fenghuo-server-app-battle-debug-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    expect(http_request(
               server.port(), http::verb::post, "/api/v0/rooms",
               nlohmann::json{{"room_id", "room-app-battle-debug-001"},
                              {"event_id", "room-app-battle-debug-evt-001"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 1},
                              {"occurred_at_ms", 1730000140001},
                              {"name", "App battle debug room"},
                              {"mode", "team_deathmatch"},
                              {"max_players", 2},
                              {"teams",
                               nlohmann::json::array({{{"team_id", "red"},
                                                       {"display_name", "Red"},
                                                       {"max_players", 1}},
                                                      {{"team_id", "blue"},
                                                       {"display_name", "Blue"},
                                                       {"max_players", 1}}})}}
                   .dump())
               .result() == http::status::created,
           "app battle debug room should be created");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-debug-001/join",
               nlohmann::json{{"event_id", "room-app-battle-debug-evt-002"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 2},
                              {"occurred_at_ms", 1730000140002},
                              {"player_id", "p-red-01"},
                              {"display_name", "Red 01"},
                              {"team_id", "red"},
                              {"module_id", "module-red-01"}}
                   .dump())
               .result() == http::status::ok,
           "battle debug red player should join");
    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-debug-001/join",
               nlohmann::json{{"event_id", "room-app-battle-debug-evt-003"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 3},
                              {"occurred_at_ms", 1730000140003},
                              {"player_id", "p-blue-01"},
                              {"display_name", "Blue 01"},
                              {"team_id", "blue"},
                              {"module_id", "module-blue-01"}}
                   .dump())
               .result() == http::status::ok,
           "battle debug blue player should join");

    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-battle-debug-001/players/p-red-01/ready",
               nlohmann::json{{"event_id", "room-app-battle-debug-evt-004"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 4},
                              {"occurred_at_ms", 1730000140004},
                              {"ready", true}}
                   .dump())
               .result() == http::status::ok,
           "battle debug red ready should succeed");
    expect(http_request(
               server.port(), http::verb::post,
               "/api/v1/rooms/room-app-battle-debug-001/players/p-blue-01/ready",
               nlohmann::json{{"event_id", "room-app-battle-debug-evt-005"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 5},
                              {"occurred_at_ms", 1730000140005},
                              {"ready", true}}
                   .dump())
               .result() == http::status::ok,
           "battle debug blue ready should succeed");

    expect(http_request(
               server.port(), http::verb::post, "/api/v1/rooms/room-app-battle-debug-001/start",
               nlohmann::json{{"event_id", "room-app-battle-debug-evt-006"},
                              {"source_id", "room-app-battle-debug-test"},
                              {"sequence", 6},
                              {"occurred_at_ms", 1730000140006},
                              {"battle_id", "battle-app-debug-001"},
                              {"duration_ms", 600000}}
                   .dump())
               .result() == http::status::ok,
           "battle debug room should start");

    auto shot_response = http_request(
        server.port(), http::verb::post, "/api/v1/battles/battle-app-debug-001/shot",
        nlohmann::json{{"event_id", "room-app-battle-debug-evt-007"},
                       {"source_id", "room-app-battle-debug-test"},
                       {"sequence", 7},
                       {"occurred_at_ms", 1730000140007},
                       {"player_id", "p-red-01"},
                       {"weapon_id", "rifle-01"},
                       {"ammo_after", 29}}
            .dump());
    expect(shot_response.result() == http::status::ok, "app battle shot should return 200");
    auto shot_body = nlohmann::json::parse(shot_response.body());
    expect(shot_body.at("players").at("p-red-01").at("shot_count") == 1,
           "app battle shot should increase shot count");

    auto hit_response = http_request(
        server.port(), http::verb::post, "/api/v1/battles/battle-app-debug-001/hit",
        nlohmann::json{{"event_id", "room-app-battle-debug-evt-008"},
                       {"source_id", "room-app-battle-debug-test"},
                       {"sequence", 8},
                       {"occurred_at_ms", 1730000140008},
                       {"attacker_player_id", "p-red-01"},
                       {"target_player_id", "p-blue-01"},
                       {"weapon_id", "rifle-01"},
                       {"damage", 10},
                       {"hit_zone", "torso"}}
            .dump());
    expect(hit_response.result() == http::status::ok, "app battle hit should return 200");
    auto hit_body = nlohmann::json::parse(hit_response.body());
    expect(hit_body.at("players").at("p-red-01").at("hit_count") == 1,
           "app battle hit should increase hit count");
    expect(hit_body.at("players").at("p-blue-01").at("health") == 90,
           "app battle hit should reduce target health");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_console_assets() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-console-integration-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    auto index_response = http_request(server.port(), http::verb::get, "/console");
    expect(index_response.result() == http::status::ok, "GET /console should return 200");
    expect(index_response.body().find("Fenghuo Console") != std::string::npos,
           "console HTML should include title");

    auto css_response = http_request(server.port(), http::verb::get, "/console/styles.css");
    expect(css_response.result() == http::status::ok, "GET console CSS should return 200");
    expect(css_response.body().find(".layout") != std::string::npos,
           "console CSS should include layout styles");

    auto js_response = http_request(server.port(), http::verb::get, "/console/app.js");
    expect(js_response.result() == http::status::ok, "GET console JS should return 200");
    expect(js_response.body().find("connectWebSocket") != std::string::npos,
           "console JS should include websocket code");

    auto sim_response = http_request(server.port(), http::verb::get, "/sim");
    expect(sim_response.result() == http::status::ok, "GET /sim should return 200");
    expect(sim_response.body().find("Fenghuo Simulator") != std::string::npos,
           "sim HTML should include title");

    auto sim_js_response = http_request(server.port(), http::verb::get, "/sim/app.js");
    expect(sim_js_response.result() == http::status::ok, "GET sim JS should return 200");
    expect(sim_js_response.body().find("setupRoom") != std::string::npos,
           "sim JS should include setup code");

    auto app_response = http_request(server.port(), http::verb::get, "/app");
    expect(app_response.result() == http::status::ok, "GET /app should return 200");
    expect(app_response.body().find("Fenghuo App Debug") != std::string::npos,
           "app HTML should include title");

    auto app_css_response = http_request(server.port(), http::verb::get, "/app/styles.css");
    expect(app_css_response.result() == http::status::ok, "GET /app/styles.css should return 200");
    expect(app_css_response.body().find(".statusList") != std::string::npos,
           "app CSS should include status list styles");

    auto app_js_response = http_request(server.port(), http::verb::get, "/app/app.js");
    expect(app_js_response.result() == http::status::ok, "GET /app/app.js should return 200");
    expect(app_js_response.body().find("/api/v1/rooms") != std::string::npos,
           "app JS should consume api v1 rooms");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_http_hit_damage_rounds_fractional_number() {
    const auto root = std::filesystem::temp_directory_path() / "fenghuo-server-hit-rounding-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    for (std::size_t index = 0; index < 3; ++index) {
        auto response =
            http_request(server.port(), http::verb::post, "/api/v0/events", normal_sequence_json().at(index));
        expect(response.result() == http::status::accepted, "setup event should be accepted");
    }

    auto rounded_down_response = http_request(
        server.port(), http::verb::post, "/api/v0/events",
        event_json("http-evt-round-down", "hit", 20,
                   {{"attacker_player_id", "p-red-01"},
                    {"target_player_id", "p-blue-01"},
                    {"weapon_id", "rifle-01"},
                    {"damage", 10.4},
                    {"hit_zone", "torso"}}));
    expect(rounded_down_response.result() == http::status::accepted,
           "HTTP hit should accept fractional damage below .5");
    auto rounded_down_body = nlohmann::json::parse(rounded_down_response.body());
    expect(rounded_down_body.at("snapshot").at("players").at("p-blue-01").at("health") == 90,
           "HTTP hit should round 10.4 damage to 10");

    auto rounded_up_response = http_request(
        server.port(), http::verb::post, "/api/v0/events",
        event_json("http-evt-round-up", "hit", 21,
                   {{"attacker_player_id", "p-red-01"},
                    {"target_player_id", "p-blue-01"},
                    {"weapon_id", "rifle-01"},
                    {"damage", 10.5},
                    {"hit_zone", "torso"}}));
    expect(rounded_up_response.result() == http::status::accepted,
           "HTTP hit should accept fractional damage at .5");
    auto rounded_up_body = nlohmann::json::parse(rounded_up_response.body());
    expect(rounded_up_body.at("snapshot").at("players").at("p-blue-01").at("health") == 79,
           "HTTP hit should round 10.5 damage to 11");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

void test_websocket_disconnect_does_not_kill_server() {
    const auto root =
        std::filesystem::temp_directory_path() / "fenghuo-server-websocket-disconnect-test";
    std::filesystem::remove_all(root);

    RunningServer server(root);

    {
        boost::asio::io_context ws_io;
        Tcp::resolver resolver(ws_io);
        websocket::stream<Tcp::socket> ws(ws_io);
        auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server.port()));
        boost::asio::connect(ws.next_layer(), endpoints);
        ws.handshake("127.0.0.1", "/api/v0/live");

        beast::error_code ignored;
        ws.next_layer().shutdown(Tcp::socket::shutdown_both, ignored);
        ws.next_layer().close(ignored);
    }

    auto response =
        http_request(server.port(), http::verb::post, "/api/v0/events", normal_sequence_json().front());
    expect(response.result() == http::status::accepted,
           "server should keep accepting events after websocket disconnect");

    server.expect_clean_shutdown();
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_http_post_snapshot_and_websocket_live();
    test_http_room_endpoints();
    test_http_room_device_endpoints();
    test_http_room_map_endpoints();
    test_websocket_map_updated_message();
    test_websocket_app_aggregate_messages();
    test_websocket_player_status_updated_from_battle();
    test_http_app_aggregate_queries();
    test_http_app_join_and_leave_commands();
    test_http_app_create_room_command();
    test_http_app_position_command();
    test_http_app_ready_start_and_close_commands();
    test_http_app_device_commands();
    test_http_app_battle_operator_commands();
    test_http_app_battle_shot_and_hit_commands();
    test_console_assets();
    test_http_hit_damage_rounds_fractional_number();
    test_websocket_disconnect_does_not_kill_server();
    return 0;
}
