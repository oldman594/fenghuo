#include "server/server.hpp"

#include <boost/beast/websocket.hpp>
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

    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<Session>> live;
    for (auto& session : sessions_) {
        if (session && session->send(message)) {
            live.push_back(session);
        }
    }
    sessions_ = std::move(live);
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
    } else if (request.method() == http::verb::delete_ && parts.size() == 6 && parts[4] == "players") {
        nlohmann::json payload = {{"player_id", parts[5]}};
        envelope = make_room_envelope(body.value(), room_id, "room_player_left", std::move(payload));
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
