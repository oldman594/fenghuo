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
    ws.close(websocket::close_code::normal, ignored);
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
    ws.read(create_buffer);
    auto create_message = nlohmann::json::parse(beast::buffers_to_string(create_buffer.data()));
    expect(create_message.at("type") == "room_updated", "websocket should publish room_updated");
    expect(create_message.at("room").at("room_id") == "room-http-001",
           "room_updated should include room");

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

    auto ready_response = http_request(
        server.port(), http::verb::post, "/api/v0/rooms/room-http-001/players/p-red-01/ready",
        nlohmann::json{{"event_id", "room-http-evt-003"},
                       {"source_id", "room-http-test"},
                       {"sequence", 3},
                       {"occurred_at_ms", 1730000020003},
                       {"ready", true}}
            .dump());
    expect(ready_response.result() == http::status::accepted, "POST room ready should update player");

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
    ws.close(websocket::close_code::normal, ignored);
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
    test_console_assets();
    test_websocket_disconnect_does_not_kill_server();
    return 0;
}
