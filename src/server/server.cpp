#include "server/server.hpp"

#include <boost/beast/websocket.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fenghuo::server {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using Tcp = boost::asio::ip::tcp;

namespace {

#ifndef FENGHUO_CONSOLE_ROOT
#define FENGHUO_CONSOLE_ROOT "apps/console"
#endif
#ifndef FENGHUO_SIM_ROOT
#define FENGHUO_SIM_ROOT "apps/sim"
#endif
#ifndef FENGHUO_APP_ROOT
#define FENGHUO_APP_ROOT "apps/app"
#endif

std::string error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
            return "invalid_argument";
        case ErrorCode::ParseError:
            return "parse_error";
        case ErrorCode::UnsupportedSchema:
            return "unsupported_schema";
        case ErrorCode::UnknownEventType:
            return "unknown_event_type";
        case ErrorCode::MissingField:
            return "missing_field";
        case ErrorCode::InvalidPayload:
            return "invalid_payload";
        case ErrorCode::DuplicateEvent:
            return "duplicate_event";
        case ErrorCode::Conflict:
            return "conflict";
        case ErrorCode::NotFound:
            return "not_found";
        case ErrorCode::StorageFailure:
            return "storage_failure";
        case ErrorCode::TransportFailure:
            return "transport_failure";
        case ErrorCode::Internal:
            return "internal";
    }
    return "unknown";
}

nlohmann::json error_body(const Error& error) {
    return {
        {"ok", false},
        {"error", {{"code", error_code_to_string(error.code)}, {"message", error.message}}},
    };
}

http::status status_for(const Error& error) {
    switch (error.code) {
        case ErrorCode::ParseError:
        case ErrorCode::UnsupportedSchema:
        case ErrorCode::UnknownEventType:
        case ErrorCode::MissingField:
        case ErrorCode::InvalidPayload:
        case ErrorCode::InvalidArgument:
            return http::status::bad_request;
        case ErrorCode::NotFound:
            return http::status::not_found;
        case ErrorCode::Conflict:
        case ErrorCode::DuplicateEvent:
            return http::status::conflict;
        case ErrorCode::StorageFailure:
        case ErrorCode::TransportFailure:
        case ErrorCode::Internal:
            return http::status::internal_server_error;
    }
    return http::status::internal_server_error;
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> split_path(std::string_view target) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < target.size()) {
        while (start < target.size() && target[start] == '/') {
            ++start;
        }
        if (start >= target.size()) {
            break;
        }
        auto end = target.find('/', start);
        if (end == std::string_view::npos) {
            end = target.size();
        }
        parts.emplace_back(target.substr(start, end - start));
        start = end;
    }
    return parts;
}

Result<nlohmann::json> parse_json_body(std::string_view body) {
    if (body.empty()) {
        return Result<nlohmann::json>::ok(nlohmann::json::object());
    }
    try {
        auto parsed = nlohmann::json::parse(body.begin(), body.end());
        if (!parsed.is_object()) {
            return Result<nlohmann::json>::err({ErrorCode::InvalidPayload, "request body must be an object"});
        }
        return Result<nlohmann::json>::ok(std::move(parsed));
    } catch (const nlohmann::json::parse_error& error) {
        return Result<nlohmann::json>::err({ErrorCode::ParseError, error.what()});
    }
}

std::string json_string_or(const nlohmann::json& object, const char* key, std::string fallback) {
    if (object.contains(key) && object.at(key).is_string()) {
        return object.at(key).get<std::string>();
    }
    return fallback;
}

std::uint64_t json_u64_or(const nlohmann::json& object, const char* key, std::uint64_t fallback) {
    if (object.contains(key) && object.at(key).is_number_unsigned()) {
        return object.at(key).get<std::uint64_t>();
    }
    return fallback;
}

std::int64_t json_i64_or(const nlohmann::json& object, const char* key, std::int64_t fallback) {
    if (object.contains(key) && object.at(key).is_number_integer()) {
        return object.at(key).get<std::int64_t>();
    }
    return fallback;
}

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool contains_player(const room::RoomSnapshot& snapshot, const std::string& player_id) {
    return snapshot.players.contains(player_id);
}

std::string generated_id(std::string_view prefix, std::int64_t timestamp) {
    std::ostringstream output;
    output << prefix << '-' << timestamp;
    return output.str();
}

room::RoomEventEnvelope make_room_envelope(const nlohmann::json& body,
                                           std::string room_id,
                                           std::string event_type,
                                           nlohmann::json payload) {
    const auto timestamp = json_i64_or(body, "occurred_at_ms", now_ms());
    room::RoomEventEnvelope envelope;
    envelope.schema_version = room::kRoomSchemaVersion;
    envelope.event_id =
        json_string_or(body, "event_id", generated_id("room-evt", timestamp));
    envelope.event_type = std::move(event_type);
    envelope.room_id = std::move(room_id);
    envelope.source_id = json_string_or(body, "source_id", "ap-http");
    envelope.sequence = json_u64_or(body, "sequence", static_cast<std::uint64_t>(timestamp));
    envelope.occurred_at_ms = timestamp;
    envelope.payload = std::move(payload);
    envelope.raw = room::to_json(envelope);
    return envelope;
}

protocol::EventEnvelope make_battle_envelope(std::string event_id,
                                             std::string event_type,
                                             std::string battle_id,
                                             std::uint64_t sequence,
                                             std::int64_t occurred_at_ms,
                                             nlohmann::json payload) {
    protocol::EventEnvelope envelope;
    envelope.schema_version = protocol::kSchemaVersion;
    envelope.event_id = std::move(event_id);
    envelope.event_type = std::move(event_type);
    envelope.battle_id = std::move(battle_id);
    envelope.source_id = "ap-room-start";
    envelope.sequence = sequence;
    envelope.occurred_at_ms = occurred_at_ms;
    envelope.payload = std::move(payload);
    envelope.raw = protocol::to_json(envelope);
    return envelope;
}

Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::err({ErrorCode::NotFound, "console asset not found"});
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return Result<std::string>::err({ErrorCode::Internal, "failed to read console asset"});
    }
    return Result<std::string>::ok(output.str());
}

std::filesystem::path console_asset_path(const std::string& asset) {
    const auto configured = std::filesystem::path(FENGHUO_CONSOLE_ROOT) / asset;
    if (std::filesystem::exists(configured)) {
        return configured;
    }
    return std::filesystem::path("apps/console") / asset;
}

std::filesystem::path sim_asset_path(const std::string& asset) {
    const auto configured = std::filesystem::path(FENGHUO_SIM_ROOT) / asset;
    if (std::filesystem::exists(configured)) {
        return configured;
    }
    return std::filesystem::path("apps/sim") / asset;
}

std::filesystem::path app_asset_path(const std::string& asset) {
    const auto configured = std::filesystem::path(FENGHUO_APP_ROOT) / asset;
    if (std::filesystem::exists(configured)) {
        return configured;
    }
    return std::filesystem::path("apps/app") / asset;
}

const room::RoomDeviceState* find_bound_device(const room::RoomSnapshot& room_snapshot,
                                               const std::string& player_id) {
    for (const auto& [_, device] : room_snapshot.devices) {
        if (device.bound_player_id && *device.bound_player_id == player_id) {
            return &device;
        }
    }
    return nullptr;
}

nlohmann::json make_room_summary_json(const room::RoomSnapshot& snapshot) {
    nlohmann::json room_item = {
        {"room_id", snapshot.room_id},
        {"name", snapshot.name},
        {"mode", snapshot.mode},
        {"phase", room::to_string(snapshot.phase)},
        {"player_count", snapshot.players.size()},
        {"max_players", snapshot.max_players},
        {"battle_id", snapshot.battle_id ? nlohmann::json(*snapshot.battle_id) : nlohmann::json(nullptr)},
    };
    nlohmann::json team_summaries = nlohmann::json::array();
    for (const auto& [team_id, team] : snapshot.teams) {
        team_summaries.push_back({
            {"team_id", team_id},
            {"display_name", team.display_name},
            {"player_count", std::count_if(snapshot.players.begin(), snapshot.players.end(),
                                           [&team_id](const auto& entry) {
                                               return entry.second.team_id == team_id;
                                           })},
            {"max_players", team.max_players},
        });
    }
    room_item["team_summaries"] = std::move(team_summaries);
    return room_item;
}

nlohmann::json make_room_detail_json(const room::RoomSnapshot& snapshot) {
    const auto room_json = room::to_json(snapshot);
    return {
        {"room", {
            {"room_id", snapshot.room_id},
            {"name", snapshot.name},
            {"mode", snapshot.mode},
            {"phase", room::to_string(snapshot.phase)},
            {"max_players", snapshot.max_players},
            {"battle_id", snapshot.battle_id ? nlohmann::json(*snapshot.battle_id) : nlohmann::json(nullptr)},
        }},
        {"players", room_json.at("players")},
        {"devices", room_json.at("devices")},
        {"positions", room_json.at("positions")},
    };
}

std::optional<nlohmann::json> make_player_status_json(
    const room::RoomSnapshot& room_snapshot, const std::string& player_id,
    const core::BattleSnapshot* battle_snapshot = nullptr,
    const ap_runtime::ApRuntime* runtime = nullptr) {
    if (!contains_player(room_snapshot, player_id)) {
        return std::nullopt;
    }

    const auto& room_player = room_snapshot.players.at(player_id);
    const auto* bound_device = find_bound_device(room_snapshot, player_id);
    const auto room_json = room::to_json(room_snapshot);

    nlohmann::json response = {
        {"player_id", room_player.player_id},
        {"display_name", room_player.display_name},
        {"room_id", room_snapshot.room_id},
        {"room_phase", room::to_string(room_snapshot.phase)},
        {"battle_id", room_snapshot.battle_id ? nlohmann::json(*room_snapshot.battle_id) : nlohmann::json(nullptr)},
        {"team_id", room_player.team_id},
        {"ready", room_player.ready},
        {"device_id", bound_device ? nlohmann::json(bound_device->device_id) : nlohmann::json(nullptr)},
        {"device_online", bound_device ? nlohmann::json(bound_device->online) : nlohmann::json(nullptr)},
        {"position", room_snapshot.positions.contains(player_id) ? room_json.at("positions").at(player_id)
                                                                 : nlohmann::json(nullptr)},
    };

    if (battle_snapshot) {
        if (battle_snapshot->players.contains(player_id)) {
            const auto& battle_player = battle_snapshot->players.at(player_id);
            response["alive"] = battle_player.alive;
            response["health"] = battle_player.health;
        } else {
            response["alive"] = nullptr;
            response["health"] = nullptr;
        }
    } else if (runtime && room_snapshot.battle_id) {
        auto current_battle_snapshot = runtime->snapshot(*room_snapshot.battle_id);
        if (current_battle_snapshot && current_battle_snapshot.value().players.contains(player_id)) {
            const auto& battle_player = current_battle_snapshot.value().players.at(player_id);
            response["alive"] = battle_player.alive;
            response["health"] = battle_player.health;
        } else {
            response["alive"] = nullptr;
            response["health"] = nullptr;
        }
    } else {
        response["alive"] = nullptr;
        response["health"] = nullptr;
    }

    return response;
}

std::optional<room::RoomSnapshot> find_room_by_battle_id(const room_runtime::RoomRuntime* room_runtime,
                                                         const std::string& battle_id) {
    if (!room_runtime) {
        return std::nullopt;
    }
    for (const auto& snapshot : room_runtime->snapshots()) {
        if (snapshot.battle_id && *snapshot.battle_id == battle_id) {
            return snapshot;
        }
    }
    return std::nullopt;
}

std::vector<std::string> room_player_ids_for_event(const room::RoomEventEnvelope& envelope,
                                                   const room::RoomSnapshot& snapshot) {
    std::vector<std::string> player_ids;

    if (envelope.payload.contains("player_id") && envelope.payload.at("player_id").is_string()) {
        player_ids.push_back(envelope.payload.at("player_id").get<std::string>());
    }

    if ((envelope.event_type == "room_device_registered" ||
         envelope.event_type == "room_device_heartbeat_updated" ||
         envelope.event_type == "room_device_bound" ||
         envelope.event_type == "room_device_unbound") &&
        envelope.payload.contains("device_id") && envelope.payload.at("device_id").is_string()) {
        const auto device_id = envelope.payload.at("device_id").get<std::string>();
        if (const auto device_it = snapshot.devices.find(device_id); device_it != snapshot.devices.end() &&
            device_it->second.bound_player_id) {
            player_ids.push_back(*device_it->second.bound_player_id);
        }
    }

    if (envelope.event_type == "room_started" || envelope.event_type == "room_ended" ||
        envelope.event_type == "room_closed") {
        for (const auto& [player_id, _] : snapshot.players) {
            player_ids.push_back(player_id);
        }
    }

    std::sort(player_ids.begin(), player_ids.end());
    player_ids.erase(std::unique(player_ids.begin(), player_ids.end()), player_ids.end());
    return player_ids;
}

std::vector<std::string> battle_player_ids_for_event(const protocol::EventEnvelope& envelope,
                                                     const core::BattleSnapshot& snapshot) {
    std::vector<std::string> player_ids;

    const auto append_if_string = [&](const char* key) {
        if (envelope.payload.contains(key) && envelope.payload.at(key).is_string()) {
            player_ids.push_back(envelope.payload.at(key).get<std::string>());
        }
    };

    append_if_string("player_id");
    append_if_string("attacker_player_id");
    append_if_string("target_player_id");

    if (envelope.event_type == "battle_started" || envelope.event_type == "battle_paused" ||
        envelope.event_type == "battle_resumed" || envelope.event_type == "battle_ended") {
        for (const auto& [player_id, _] : snapshot.players) {
            player_ids.push_back(player_id);
        }
    }

    std::sort(player_ids.begin(), player_ids.end());
    player_ids.erase(std::unique(player_ids.begin(), player_ids.end()), player_ids.end());
    return player_ids;
}

} // namespace

class WebSocketBroadcaster::Session : public std::enable_shared_from_this<Session> {
public:
    Session(TcpSocket socket, HttpRequest request)
        : ws_(std::move(socket)), request_(std::move(request)) {}

    ~Session() {
        if (thread_.joinable()) {
            thread_.detach();
        }
    }

    void run() {
        beast::error_code error;
        ws_.accept(request_, error);
        if (error) {
            return;
        }

        beast::flat_buffer buffer;
        while (true) {
            ws_.read(buffer, error);
            if (error) {
                break;
            }
            buffer.clear();
        }
    }

    void stop() {
        std::lock_guard lock(mutex_);
        beast::error_code ignored;
        ws_.next_layer().shutdown(TcpSocket::shutdown_both, ignored);
        ws_.next_layer().close(ignored);
    }

    bool send(const nlohmann::json& message) {
        std::lock_guard lock(mutex_);
        beast::error_code error;
        ws_.text(true);
        ws_.write(boost::asio::buffer(message.dump()), error);
        return !error;
    }

private:
    websocket::stream<TcpSocket> ws_;
    HttpRequest request_;
    std::mutex mutex_;
    std::thread thread_;

public:
    void start() {
        auto self = shared_from_this();
        thread_ = std::thread([self] { self->run(); });
    }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
};

void WebSocketBroadcaster::bind_runtimes(const ap_runtime::ApRuntime* runtime,
                                         const room_runtime::RoomRuntime* room_runtime) {
    std::lock_guard lock(mutex_);
    runtime_ = runtime;
    room_runtime_ = room_runtime;
}

void WebSocketBroadcaster::add_session(TcpSocket socket, HttpRequest request) {
    auto session = std::make_shared<Session>(std::move(socket), std::move(request));
    {
        std::lock_guard lock(mutex_);
        sessions_.push_back(session);
    }
    session->start();
}

void WebSocketBroadcaster::stop() {
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard lock(mutex_);
        sessions = sessions_;
        sessions_.clear();
    }
    for (const auto& session : sessions) {
        if (session) {
            session->stop();
            session->join();
        }
    }
}

void WebSocketBroadcaster::publish_accepted_event(const protocol::EventEnvelope& envelope,
                                                  const core::BattleSnapshot& snapshot) {
    const nlohmann::json message = {
        {"type", "accepted_event"},
        {"event", protocol::to_json(envelope)},
        {"snapshot", protocol::to_json(snapshot)},
    };

    publish_message(message);

    const room_runtime::RoomRuntime* room_runtime = nullptr;
    const ap_runtime::ApRuntime* runtime = nullptr;
    {
        std::lock_guard lock(mutex_);
        room_runtime = room_runtime_;
        runtime = runtime_;
    }
    const auto room_snapshot = find_room_by_battle_id(room_runtime, snapshot.battle_id);
    if (!room_snapshot) {
        return;
    }

    for (const auto& player_id : battle_player_ids_for_event(envelope, snapshot)) {
        const auto status = make_player_status_json(*room_snapshot, player_id, &snapshot, nullptr);
        if (!status) {
            continue;
        }
        publish_message({
            {"type", "player_status_updated"},
            {"event", protocol::to_json(envelope)},
            {"player_id", player_id},
            {"status", *status},
        });
    }
}

void WebSocketBroadcaster::publish_message(const nlohmann::json& message) {
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<Session>> live;
    for (auto& session : sessions_) {
        if (session && session->send(message)) {
            live.push_back(session);
        }
    }
    sessions_ = std::move(live);
}

void WebSocketBroadcaster::publish_room_updated(const room::RoomEventEnvelope& envelope,
                                                const room::RoomSnapshot& snapshot) {
    const nlohmann::json message = {
        {"type", "room_updated"},
        {"event", room::to_json(envelope)},
        {"room", room::to_json(snapshot)},
    };

    publish_message(message);
    publish_message({
        {"type", "room_summary_updated"},
        {"event", room::to_json(envelope)},
        {"room", make_room_summary_json(snapshot)},
    });
    {
        auto detail = make_room_detail_json(snapshot);
        detail["type"] = "room_detail_updated";
        detail["event"] = room::to_json(envelope);
        publish_message(detail);
    }

    if (envelope.event_type == "room_player_position_updated") {
        const auto room_json = room::to_json(snapshot);
        publish_message({
            {"type", "map_updated"},
            {"event", room::to_json(envelope)},
            {"room_id", snapshot.room_id},
            {"phase", room_json.at("phase")},
            {"positions", room_json.at("positions")},
        });
    }

    const ap_runtime::ApRuntime* runtime = nullptr;
    {
        std::lock_guard lock(mutex_);
        runtime = runtime_;
    }
    for (const auto& player_id : room_player_ids_for_event(envelope, snapshot)) {
        const auto status = make_player_status_json(snapshot, player_id, nullptr, runtime);
        publish_message({
            {"type", "player_status_updated"},
            {"event", room::to_json(envelope)},
            {"player_id", player_id},
            {"status", status ? *status : nlohmann::json(nullptr)},
        });
    }
}

ApServer::ApServer(boost::asio::io_context& io, ap_runtime::ApRuntime& runtime,
                   room_runtime::RoomRuntime& room_runtime,
                   std::shared_ptr<WebSocketBroadcaster> broadcaster, std::uint16_t port)
    : io_(io),
      runtime_(runtime),
      room_runtime_(room_runtime),
      broadcaster_(std::move(broadcaster)),
      acceptor_(io, Tcp::endpoint(Tcp::v4(), port)) {}

Result<void> ApServer::run() {
    stopping_ = false;
    start_accept();
    io_.run();
    cleanup_workers();
    return Result<void>::ok();
}

void ApServer::start_accept() {
    auto socket = std::make_shared<Tcp::socket>(io_);
    acceptor_.async_accept(*socket, [this, socket](const beast::error_code& error) {
        if (stopping_) {
            return;
        }
        if (error) {
            return;
        }
        {
            std::lock_guard lock(active_socket_mutex_);
            active_sockets_.push_back(socket);
        }

        {
            std::lock_guard lock(worker_mutex_);
            workers_.emplace_back([this, socket] {
                handle_connection(socket);
            });
        }
        start_accept();
    });
}

void ApServer::stop() {
    stopping_ = true;
    boost::asio::post(io_, [this] {
        beast::error_code ignored;
        acceptor_.cancel(ignored);
        acceptor_.close(ignored);
        broadcaster_->stop();
        std::vector<std::shared_ptr<Tcp::socket>> sockets;
        {
            std::lock_guard lock(active_socket_mutex_);
            for (const auto& weak : active_sockets_) {
                if (auto socket = weak.lock()) {
                    sockets.push_back(socket);
                }
            }
            active_sockets_.clear();
        }
        for (const auto& socket : sockets) {
            socket->shutdown(Tcp::socket::shutdown_both, ignored);
            socket->close(ignored);
        }
        io_.stop();
    });
}

std::uint16_t ApServer::port() const {
    return acceptor_.local_endpoint().port();
}

void ApServer::handle_connection(std::shared_ptr<Tcp::socket> socket) {
    beast::flat_buffer buffer;
    HttpRequest request;
    beast::error_code error;
    http::read(*socket, buffer, request, error);
    if (error) {
        remove_active_socket(socket);
        return;
    }

    if (websocket::is_upgrade(request) && request.target() == "/api/v0/live") {
        broadcaster_->add_session(std::move(*socket), std::move(request));
        remove_active_socket(socket);
        return;
    }

    auto response = handle_request(request);
    http::write(*socket, response, error);
    socket->shutdown(Tcp::socket::shutdown_send, error);
    remove_active_socket(socket);
}

void ApServer::cleanup_workers() {
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(worker_mutex_);
        workers = std::move(workers_);
    }
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ApServer::remove_active_socket(const std::shared_ptr<Tcp::socket>& socket) {
    std::lock_guard lock(active_socket_mutex_);
    std::vector<std::weak_ptr<Tcp::socket>> live;
    for (const auto& weak : active_sockets_) {
        if (auto active = weak.lock(); active && active != socket) {
            live.push_back(active);
        }
    }
    active_sockets_ = std::move(live);
}

Result<void> ApServer::start_battle_from_room(const room::RoomSnapshot& room_snapshot,
                                              std::int64_t occurred_at_ms,
                                              std::int64_t duration_ms) {
    if (!room_snapshot.battle_id) {
        return Result<void>::err({ErrorCode::InvalidArgument, "room has no linked battle_id"});
    }

    std::uint64_t sequence = 1;
    for (const auto& [_, player] : room_snapshot.players) {
        auto envelope = make_battle_envelope(
            room_snapshot.room_id + "-battle-join-" + player.player_id,
            "player_joined",
            *room_snapshot.battle_id,
            sequence++,
            occurred_at_ms,
            {
                {"player_id", player.player_id},
                {"display_name", player.display_name},
                {"team_id", player.team_id},
                {"module_id", player.module_id},
            });
        auto result = runtime_.submit_event(envelope);
        if (result.status != ap_runtime::SubmitEventResult::Status::Accepted &&
            result.status != ap_runtime::SubmitEventResult::Status::Duplicate) {
            return Result<void>::err(
                result.error.value_or(Error{ErrorCode::Internal, "failed to submit player_joined"}));
        }
    }

    auto start = make_battle_envelope(
        room_snapshot.room_id + "-battle-started",
        "battle_started",
        *room_snapshot.battle_id,
        sequence,
        occurred_at_ms,
        {
            {"mode", room_snapshot.mode},
            {"duration_ms", duration_ms},
        });
    auto result = runtime_.submit_event(start);
    if (result.status != ap_runtime::SubmitEventResult::Status::Accepted &&
        result.status != ap_runtime::SubmitEventResult::Status::Duplicate) {
        return Result<void>::err(
            result.error.value_or(Error{ErrorCode::Internal, "failed to submit battle_started"}));
    }

    return Result<void>::ok();
}

ApServer::HttpResponse ApServer::handle_request(const HttpRequest& request) {
    const auto target = std::string(request.target());

    if (target == "/" || target == "/console" || target == "/console/" ||
        starts_with(target, "/console/")) {
        return handle_console_request(request, target);
    }
    if (target == "/sim" || target == "/sim/" || starts_with(target, "/sim/")) {
        return handle_sim_request(request, target);
    }
    if (target == "/app" || target == "/app/" || starts_with(target, "/app/")) {
        return handle_app_page_request(request, target);
    }

    if (request.method() == http::verb::post && target == "/api/v0/events") {
        auto envelope = protocol::parse_event_json(request.body());
        if (!envelope) {
            return json_response(status_for(envelope.error()), error_body(envelope.error()),
                                 request.version(), request.keep_alive());
        }

        const auto result = runtime_.submit_event(envelope.value());
        if (result.status == ap_runtime::SubmitEventResult::Status::Accepted) {
            return json_response(http::status::accepted,
                                 {{"ok", true},
                                  {"status", "accepted"},
                                  {"snapshot", protocol::to_json(result.snapshot)}},
                                 request.version(), request.keep_alive());
        }
        if (result.status == ap_runtime::SubmitEventResult::Status::Duplicate) {
            return json_response(http::status::ok,
                                 {{"ok", true},
                                  {"status", "duplicate"},
                                  {"snapshot", protocol::to_json(result.snapshot)}},
                                 request.version(), request.keep_alive());
        }

        const auto error = result.error.value_or(Error{ErrorCode::Internal, "unknown runtime error"});
        return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
    }

    if (target == "/api/v0/rooms" || starts_with(target, "/api/v0/rooms/")) {
        return handle_room_request(request, std::move(target));
    }
    if (target == "/api/v1/rooms" || starts_with(target, "/api/v1/rooms/") ||
        target == "/api/v1/players" || starts_with(target, "/api/v1/players/") ||
        target == "/api/v1/battles" || starts_with(target, "/api/v1/battles/")) {
        return handle_app_query_request(request, std::move(target));
    }

    constexpr std::string_view snapshot_prefix = "/api/v0/battles/";
    constexpr std::string_view snapshot_suffix = "/snapshot";
    if (request.method() == http::verb::get && starts_with(target, snapshot_prefix) &&
        target.size() > snapshot_prefix.size() + snapshot_suffix.size() &&
        target.substr(target.size() - snapshot_suffix.size()) == snapshot_suffix) {
        const auto battle_id =
            target.substr(snapshot_prefix.size(),
                          target.size() - snapshot_prefix.size() - snapshot_suffix.size());
        auto snapshot = runtime_.snapshot(battle_id);
        if (!snapshot) {
            return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                 request.version(), request.keep_alive());
        }
        return json_response(http::status::ok,
                             {{"ok", true}, {"snapshot", protocol::to_json(snapshot.value())}},
                             request.version(), request.keep_alive());
    }

    return json_response(http::status::not_found,
                         error_body({ErrorCode::NotFound, "endpoint not found"}),
                         request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::handle_console_request(const HttpRequest& request, std::string target) {
    if (request.method() != http::verb::get) {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    std::string asset = "index.html";
    std::string content_type = "text/html; charset=utf-8";
    if (target == "/console/styles.css") {
        asset = "styles.css";
        content_type = "text/css; charset=utf-8";
    } else if (target == "/console/app.js") {
        asset = "app.js";
        content_type = "application/javascript; charset=utf-8";
    } else if (target != "/" && target != "/console" && target != "/console/") {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "console asset not found"}),
                             request.version(), request.keep_alive());
    }

    const auto path = console_asset_path(asset);
    auto body = read_text_file(path);
    if (!body) {
        return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                             request.keep_alive());
    }
    return text_response(http::status::ok, std::move(body).value(), std::move(content_type),
                         request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::handle_sim_request(const HttpRequest& request, std::string target) {
    if (request.method() != http::verb::get) {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    std::string asset = "index.html";
    std::string content_type = "text/html; charset=utf-8";
    if (target == "/sim/styles.css") {
        asset = "styles.css";
        content_type = "text/css; charset=utf-8";
    } else if (target == "/sim/app.js") {
        asset = "app.js";
        content_type = "application/javascript; charset=utf-8";
    } else if (target != "/sim" && target != "/sim/") {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "sim asset not found"}),
                             request.version(), request.keep_alive());
    }

    auto body = read_text_file(sim_asset_path(asset));
    if (!body) {
        return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                             request.keep_alive());
    }
    return text_response(http::status::ok, std::move(body).value(), std::move(content_type),
                         request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::handle_app_page_request(const HttpRequest& request, std::string target) {
    if (request.method() != http::verb::get) {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    std::string asset = "index.html";
    std::string content_type = "text/html; charset=utf-8";
    if (target == "/app/styles.css") {
        asset = "styles.css";
        content_type = "text/css; charset=utf-8";
    } else if (target == "/app/app.js") {
        asset = "app.js";
        content_type = "application/javascript; charset=utf-8";
    } else if (target != "/app" && target != "/app/") {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "app asset not found"}),
                             request.version(), request.keep_alive());
    }

    auto body = read_text_file(app_asset_path(asset));
    if (!body) {
        return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                             request.keep_alive());
    }
    return text_response(http::status::ok, std::move(body).value(), std::move(content_type),
                         request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::handle_room_request(const HttpRequest& request, std::string target) {
    const auto parts = split_path(target);

    if (request.method() == http::verb::get && parts.size() == 3 && target == "/api/v0/rooms") {
        nlohmann::json rooms = nlohmann::json::array();
        for (const auto& snapshot : room_runtime_.snapshots()) {
            rooms.push_back(room::to_json(snapshot));
        }
        return json_response(http::status::ok, {{"ok", true}, {"rooms", std::move(rooms)}},
                             request.version(), request.keep_alive());
    }

    if (request.method() == http::verb::post && parts.size() == 3 && target == "/api/v0/rooms") {
        auto body = parse_json_body(request.body());
        if (!body) {
            return json_response(status_for(body.error()), error_body(body.error()),
                                 request.version(), request.keep_alive());
        }
        const auto timestamp = json_i64_or(body.value(), "occurred_at_ms", now_ms());
        const auto room_id = json_string_or(body.value(), "room_id", generated_id("room", timestamp));
        nlohmann::json payload = body.value();
        payload.erase("event_id");
        payload.erase("event_type");
        payload.erase("room_id");
        payload.erase("source_id");
        payload.erase("sequence");
        payload.erase("occurred_at_ms");
        if (!payload.contains("room_code")) {
            payload["room_code"] = room_id;
        }
        auto envelope = make_room_envelope(body.value(), room_id, "room_created", std::move(payload));
        auto result = room_runtime_.submit_event(envelope);
        if (result.status == room_runtime::SubmitRoomEventResult::Status::Accepted) {
            return json_response(http::status::created,
                                 {{"ok", true},
                                  {"status", "accepted"},
                                  {"room", room::to_json(result.snapshot)}},
                                 request.version(), request.keep_alive());
        }
        const auto error = result.error.value_or(Error{ErrorCode::Internal, "unknown room runtime error"});
        return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
    }

    if (parts.size() < 4 || parts[0] != "api" || parts[1] != "v0" || parts[2] != "rooms") {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    const auto& room_id = parts[3];
    if (request.method() == http::verb::get && parts.size() == 4) {
        auto snapshot = room_runtime_.snapshot(room_id);
        if (!snapshot) {
            return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                 request.version(), request.keep_alive());
        }
        return json_response(http::status::ok,
                             {{"ok", true}, {"room", room::to_json(snapshot.value())}},
                             request.version(), request.keep_alive());
    }

    auto body = parse_json_body(request.body());
    if (!body) {
        return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                             request.keep_alive());
    }

    std::optional<room::RoomEventEnvelope> envelope;
    bool start_room_command = false;
    std::int64_t start_duration_ms = 0;
    std::int64_t start_occurred_at_ms = 0;
    if (request.method() == http::verb::post && parts.size() == 5 && parts[4] == "players") {
        nlohmann::json payload = body.value();
        envelope = make_room_envelope(body.value(), room_id, "room_player_joined", std::move(payload));
    } else if (request.method() == http::verb::get && parts.size() == 5 && parts[4] == "map") {
        auto snapshot = room_runtime_.snapshot(room_id);
        if (!snapshot) {
            return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                 request.version(), request.keep_alive());
        }
        auto room_json = room::to_json(snapshot.value());
        return json_response(http::status::ok,
                             {{"ok", true},
                              {"room_id", room_id},
                              {"phase", room_json.at("phase")},
                              {"positions", room_json.at("positions")}},
                             request.version(), request.keep_alive());
    } else if (request.method() == http::verb::post && parts.size() == 5 && parts[4] == "positions") {
        nlohmann::json payload = body.value();
        envelope =
            make_room_envelope(body.value(), room_id, "room_player_position_updated", std::move(payload));
    } else if (request.method() == http::verb::get && parts.size() == 5 && parts[4] == "devices") {
        auto snapshot = room_runtime_.snapshot(room_id);
        if (!snapshot) {
            return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                 request.version(), request.keep_alive());
        }
        return json_response(http::status::ok,
                             {{"ok", true}, {"devices", room::to_json(snapshot.value()).at("devices")}},
                             request.version(), request.keep_alive());
    } else if (request.method() == http::verb::post && parts.size() == 5 && parts[4] == "devices") {
        nlohmann::json payload = body.value();
        envelope = make_room_envelope(body.value(), room_id, "room_device_registered", std::move(payload));
    } else if (request.method() == http::verb::delete_ && parts.size() == 6 && parts[4] == "players") {
        nlohmann::json payload = {{"player_id", parts[5]}};
        envelope = make_room_envelope(body.value(), room_id, "room_player_left", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 7 && parts[4] == "devices" &&
               parts[6] == "heartbeat") {
        nlohmann::json payload = body.value();
        payload["device_id"] = parts[5];
        envelope = make_room_envelope(body.value(), room_id, "room_device_heartbeat_updated",
                                      std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 7 && parts[4] == "devices" &&
               parts[6] == "bind") {
        nlohmann::json payload = body.value();
        payload["device_id"] = parts[5];
        envelope = make_room_envelope(body.value(), room_id, "room_device_bound", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 7 && parts[4] == "devices" &&
               parts[6] == "unbind") {
        nlohmann::json payload = {{"device_id", parts[5]}};
        envelope = make_room_envelope(body.value(), room_id, "room_device_unbound", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 7 && parts[4] == "players" &&
               parts[6] == "team") {
        nlohmann::json payload = body.value();
        payload["player_id"] = parts[5];
        envelope =
            make_room_envelope(body.value(), room_id, "room_player_team_changed", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 7 && parts[4] == "players" &&
               parts[6] == "ready") {
        nlohmann::json payload = body.value();
        payload["player_id"] = parts[5];
        envelope =
            make_room_envelope(body.value(), room_id, "room_player_ready_changed", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 5 && parts[4] == "start") {
        nlohmann::json payload = body.value();
        if (!payload.contains("battle_id")) {
            payload["battle_id"] = generated_id("battle", now_ms());
        }
        start_room_command = true;
        start_duration_ms = json_i64_or(payload, "duration_ms", 600000);
        start_occurred_at_ms = json_i64_or(body.value(), "occurred_at_ms", now_ms());
        envelope = make_room_envelope(body.value(), room_id, "room_started", std::move(payload));
    } else if (request.method() == http::verb::post && parts.size() == 5 && parts[4] == "close") {
        envelope = make_room_envelope(body.value(), room_id, "room_closed", nlohmann::json::object());
    }

    if (!envelope) {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    auto result = room_runtime_.submit_event(*envelope);
    if (result.status == room_runtime::SubmitRoomEventResult::Status::Accepted) {
        nlohmann::json body_json = {
            {"ok", true},
            {"status", "accepted"},
            {"room", room::to_json(result.snapshot)},
        };
        if (start_room_command) {
            auto started = start_battle_from_room(result.snapshot, start_occurred_at_ms, start_duration_ms);
            if (!started) {
                return json_response(status_for(started.error()), error_body(started.error()),
                                     request.version(), request.keep_alive());
            }
            if (result.snapshot.battle_id) {
                auto battle_snapshot = runtime_.snapshot(*result.snapshot.battle_id);
                if (battle_snapshot) {
                    body_json["battle_snapshot"] = protocol::to_json(battle_snapshot.value());
                }
            }
        }
        return json_response(http::status::accepted, std::move(body_json), request.version(),
                             request.keep_alive());
    }
    if (result.status == room_runtime::SubmitRoomEventResult::Status::Duplicate) {
        return json_response(http::status::ok,
                             {{"ok", true},
                              {"status", "duplicate"},
                              {"room", room::to_json(result.snapshot)}},
                             request.version(), request.keep_alive());
    }

    const auto error = result.error.value_or(Error{ErrorCode::Internal, "unknown room runtime error"});
    return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::handle_app_query_request(const HttpRequest& request, std::string target) {
    const auto parts = split_path(target);
    if (parts.size() < 3 || parts[0] != "api" || parts[1] != "v1") {
        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "endpoint not found"}),
                             request.version(), request.keep_alive());
    }

    if (request.method() == http::verb::post && parts.size() == 3 && parts[2] == "rooms") {
        auto body = parse_json_body(request.body());
        if (!body) {
            return json_response(status_for(body.error()), error_body(body.error()),
                                 request.version(), request.keep_alive());
        }
        const auto timestamp = json_i64_or(body.value(), "occurred_at_ms", now_ms());
        const auto room_id = json_string_or(body.value(), "room_id", generated_id("room", timestamp));
        nlohmann::json payload = body.value();
        payload.erase("event_id");
        payload.erase("event_type");
        payload.erase("room_id");
        payload.erase("source_id");
        payload.erase("sequence");
        payload.erase("occurred_at_ms");
        if (!payload.contains("room_code")) {
            payload["room_code"] = room_id;
        }

        auto envelope = make_room_envelope(body.value(), room_id, "room_created", std::move(payload));
        auto result = room_runtime_.submit_event(envelope);
        if (result.status == room_runtime::SubmitRoomEventResult::Status::Accepted ||
            result.status == room_runtime::SubmitRoomEventResult::Status::Duplicate) {
            auto detail = make_room_detail_json(result.snapshot);
            detail["ok"] = true;
            return json_response(http::status::ok, std::move(detail), request.version(),
                                 request.keep_alive());
        }

        const auto error =
            result.error.value_or(Error{ErrorCode::Internal, "unknown room runtime error"});
        return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
    }

    if (request.method() == http::verb::get && parts.size() == 3 && parts[2] == "rooms") {
        nlohmann::json rooms = nlohmann::json::array();
        for (const auto& snapshot : room_runtime_.snapshots()) {
            rooms.push_back(make_room_summary_json(snapshot));
        }
        return json_response(http::status::ok, {{"ok", true}, {"rooms", std::move(rooms)}},
                             request.version(), request.keep_alive());
    }

    if (parts[2] == "rooms" && parts.size() >= 4) {
        const auto& room_id = parts[3];
        auto room_detail_response = [&](const room::RoomSnapshot& snapshot) {
            auto detail = make_room_detail_json(snapshot);
            detail["ok"] = true;
            return json_response(http::status::ok, std::move(detail), request.version(),
                                 request.keep_alive());
        };

        if (request.method() == http::verb::get) {
            auto snapshot = room_runtime_.snapshot(room_id);
            if (!snapshot) {
                return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                     request.version(), request.keep_alive());
            }

            if (parts.size() == 4) {
                return room_detail_response(snapshot.value());
            }

            if (parts.size() == 5 && parts[4] == "map") {
                const auto room_json = room::to_json(snapshot.value());
                return json_response(http::status::ok,
                                     {{"ok", true},
                                      {"room_id", snapshot.value().room_id},
                                      {"phase", room::to_string(snapshot.value().phase)},
                                      {"positions", room_json.at("positions")}},
                                     request.version(), request.keep_alive());
            }
        }

        if (request.method() == http::verb::post &&
            ((parts.size() == 5 && (parts[4] == "join" || parts[4] == "leave" || parts[4] == "start" ||
                                    parts[4] == "close" || parts[4] == "devices" ||
                                    parts[4] == "positions")) ||
             (parts.size() == 7 &&
              ((parts[4] == "players" && parts[6] == "ready") ||
               (parts[4] == "devices" &&
                (parts[6] == "bind" || parts[6] == "unbind" || parts[6] == "heartbeat")))))) {
            auto body = parse_json_body(request.body());
            if (!body) {
                return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                                     request.keep_alive());
            }

            std::optional<room::RoomEventEnvelope> envelope;
            bool start_room_command = false;
            std::int64_t start_duration_ms = 0;
            std::int64_t start_occurred_at_ms = 0;
            if (parts[4] == "join") {
                nlohmann::json payload = body.value();
                envelope =
                    make_room_envelope(body.value(), room_id, "room_player_joined", std::move(payload));
            } else if (parts[4] == "leave") {
                nlohmann::json payload = body.value();
                const auto player_id = json_string_or(payload, "player_id", "");
                if (player_id.empty()) {
                    return json_response(http::status::bad_request,
                                         error_body({ErrorCode::MissingField, "missing field: player_id"}),
                                         request.version(), request.keep_alive());
                }
                payload["player_id"] = player_id;
                envelope =
                    make_room_envelope(body.value(), room_id, "room_player_left", std::move(payload));
            } else if (parts[4] == "start") {
                nlohmann::json payload = body.value();
                if (!payload.contains("battle_id")) {
                    payload["battle_id"] = generated_id("battle", now_ms());
                }
                start_room_command = true;
                start_duration_ms = json_i64_or(payload, "duration_ms", 600000);
                start_occurred_at_ms = json_i64_or(body.value(), "occurred_at_ms", now_ms());
                envelope = make_room_envelope(body.value(), room_id, "room_started", std::move(payload));
            } else if (parts[4] == "close") {
                envelope = make_room_envelope(body.value(), room_id, "room_closed", nlohmann::json::object());
            } else if (parts[4] == "devices" && parts.size() == 5) {
                nlohmann::json payload = body.value();
                envelope = make_room_envelope(body.value(), room_id, "room_device_registered", std::move(payload));
            } else if (parts[4] == "positions" && parts.size() == 5) {
                nlohmann::json payload = body.value();
                envelope =
                    make_room_envelope(body.value(), room_id, "room_player_position_updated", std::move(payload));
            } else if (parts[4] == "players" && parts[6] == "ready") {
                nlohmann::json payload = body.value();
                payload["player_id"] = parts[5];
                envelope =
                    make_room_envelope(body.value(), room_id, "room_player_ready_changed", std::move(payload));
            } else if (parts[4] == "devices" && parts[6] == "bind") {
                nlohmann::json payload = body.value();
                payload["device_id"] = parts[5];
                envelope = make_room_envelope(body.value(), room_id, "room_device_bound", std::move(payload));
            } else if (parts[4] == "devices" && parts[6] == "unbind") {
                nlohmann::json payload = {{"device_id", parts[5]}};
                envelope = make_room_envelope(body.value(), room_id, "room_device_unbound", std::move(payload));
            } else if (parts[4] == "devices" && parts[6] == "heartbeat") {
                nlohmann::json payload = body.value();
                payload["device_id"] = parts[5];
                envelope =
                    make_room_envelope(body.value(), room_id, "room_device_heartbeat_updated", std::move(payload));
            }

            auto result = room_runtime_.submit_event(*envelope);
            if (result.status == room_runtime::SubmitRoomEventResult::Status::Accepted ||
                result.status == room_runtime::SubmitRoomEventResult::Status::Duplicate) {
                auto detail = make_room_detail_json(result.snapshot);
                detail["ok"] = true;
                if (start_room_command && result.status == room_runtime::SubmitRoomEventResult::Status::Accepted) {
                    auto started =
                        start_battle_from_room(result.snapshot, start_occurred_at_ms, start_duration_ms);
                    if (!started) {
                        return json_response(status_for(started.error()), error_body(started.error()),
                                             request.version(), request.keep_alive());
                    }
                    if (result.snapshot.battle_id) {
                        auto battle_snapshot = runtime_.snapshot(*result.snapshot.battle_id);
                        if (battle_snapshot) {
                            detail["battle_snapshot"] = protocol::to_json(battle_snapshot.value());
                        }
                    }
                }
                return json_response(http::status::ok, std::move(detail), request.version(),
                                     request.keep_alive());
            }

            const auto error =
                result.error.value_or(Error{ErrorCode::Internal, "unknown room runtime error"});
            return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
        }
    }

    if (request.method() == http::verb::get && parts.size() == 5 && parts[2] == "players" && parts[4] == "status") {
        const auto& player_id = parts[3];
        for (const auto& room_snapshot : room_runtime_.snapshots()) {
            if (!contains_player(room_snapshot, player_id)) {
                continue;
            }
            auto response = make_player_status_json(room_snapshot, player_id, nullptr, &runtime_);
            if (!response) {
                break;
            }
            response->operator[]("ok") = true;
            return json_response(http::status::ok, std::move(*response), request.version(),
                                 request.keep_alive());
        }

        return json_response(http::status::not_found,
                             error_body({ErrorCode::NotFound, "player not found"}),
                             request.version(), request.keep_alive());
    }

    if (request.method() == http::verb::get && parts.size() == 4 && parts[2] == "battles") {
        const auto& battle_id = parts[3];
        auto snapshot = runtime_.snapshot(battle_id);
        if (!snapshot) {
            return json_response(status_for(snapshot.error()), error_body(snapshot.error()),
                                 request.version(), request.keep_alive());
        }
        auto battle_json = protocol::to_json(snapshot.value());
        battle_json["ok"] = true;
        return json_response(http::status::ok, std::move(battle_json), request.version(),
                             request.keep_alive());
    }

    if (request.method() == http::verb::post && parts.size() == 5 && parts[2] == "battles" &&
        (parts[4] == "pause" || parts[4] == "resume" || parts[4] == "end" || parts[4] == "shot" ||
         parts[4] == "hit")) {
        const auto& battle_id = parts[3];
        auto body = parse_json_body(request.body());
        if (!body) {
            return json_response(status_for(body.error()), error_body(body.error()), request.version(),
                                 request.keep_alive());
        }

        std::string event_type;
        nlohmann::json payload = nlohmann::json::object();
        if (parts[4] == "pause") {
            event_type = "battle_paused";
            payload["reason"] = json_string_or(body.value(), "reason", "operator");
        } else if (parts[4] == "resume") {
            event_type = "battle_resumed";
        } else if (parts[4] == "end") {
            event_type = "battle_ended";
            payload["reason"] = json_string_or(body.value(), "reason", "manual");
        } else if (parts[4] == "shot") {
            event_type = "shot";
            payload["player_id"] = json_string_or(body.value(), "player_id", "");
            payload["weapon_id"] = json_string_or(body.value(), "weapon_id", "rifle-01");
            payload["ammo_after"] = body.value().contains("ammo_after") ? body.value().at("ammo_after")
                                                                         : nlohmann::json(0);
        } else {
            event_type = "hit";
            payload["attacker_player_id"] = json_string_or(body.value(), "attacker_player_id", "");
            payload["target_player_id"] = json_string_or(body.value(), "target_player_id", "");
            payload["weapon_id"] = json_string_or(body.value(), "weapon_id", "rifle-01");
            payload["damage"] = body.value().contains("damage") ? body.value().at("damage")
                                                                : nlohmann::json(10);
            payload["hit_zone"] = json_string_or(body.value(), "hit_zone", "torso");
        }

        const auto timestamp = json_i64_or(body.value(), "occurred_at_ms", now_ms());
        protocol::EventEnvelope envelope;
        envelope.schema_version = protocol::kSchemaVersion;
        envelope.event_id = json_string_or(body.value(), "event_id", generated_id("battle-evt", timestamp));
        envelope.event_type = std::move(event_type);
        envelope.battle_id = battle_id;
        envelope.source_id = json_string_or(body.value(), "source_id", "app-http");
        envelope.sequence = json_u64_or(body.value(), "sequence", static_cast<std::uint64_t>(timestamp));
        envelope.occurred_at_ms = timestamp;
        envelope.payload = std::move(payload);
        envelope.raw = protocol::to_json(envelope);

        auto result = runtime_.submit_event(envelope);
        if (result.status == ap_runtime::SubmitEventResult::Status::Accepted ||
            result.status == ap_runtime::SubmitEventResult::Status::Duplicate) {
            auto battle_json = protocol::to_json(result.snapshot);
            battle_json["ok"] = true;
            return json_response(http::status::ok, std::move(battle_json), request.version(),
                                 request.keep_alive());
        }

        const auto error =
            result.error.value_or(Error{ErrorCode::Internal, "unknown battle runtime error"});
        return json_response(status_for(error), error_body(error), request.version(), request.keep_alive());
    }

    return json_response(http::status::not_found,
                         error_body({ErrorCode::NotFound, "endpoint not found"}),
                         request.version(), request.keep_alive());
}

ApServer::HttpResponse ApServer::json_response(http::status status, nlohmann::json body,
                                               unsigned version, bool keep_alive) const {
    HttpResponse response{status, version};
    response.set(http::field::server, "fenghuo-apd");
    response.set(http::field::content_type, "application/json");
    response.keep_alive(keep_alive);
    response.body() = body.dump();
    response.prepare_payload();
    return response;
}

ApServer::HttpResponse ApServer::text_response(http::status status, std::string body,
                                               std::string content_type, unsigned version,
                                               bool keep_alive) const {
    HttpResponse response{status, version};
    response.set(http::field::server, "fenghuo-apd");
    response.set(http::field::content_type, std::move(content_type));
    response.keep_alive(keep_alive);
    response.body() = std::move(body);
    response.prepare_payload();
    return response;
}

} // namespace fenghuo::server
