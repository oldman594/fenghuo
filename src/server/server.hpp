#pragma once

#include "ap_runtime/runtime.hpp"
#include "room_runtime/runtime.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace fenghuo::server {

class WebSocketBroadcaster final : public ap_runtime::BattleUpdateSink,
                                   public room_runtime::RoomUpdateSink {
public:
    using TcpSocket = boost::asio::ip::tcp::socket;
    using HttpRequest =
        boost::beast::http::request<boost::beast::http::string_body>;

    void bind_runtimes(const ap_runtime::ApRuntime* runtime,
                       const room_runtime::RoomRuntime* room_runtime);
    void add_session(TcpSocket socket, HttpRequest request);
    void stop();
    void publish_accepted_event(const protocol::EventEnvelope& envelope,
                                const core::BattleSnapshot& snapshot) override;
    void publish_room_updated(const room::RoomEventEnvelope& envelope,
                              const room::RoomSnapshot& snapshot) override;
    void publish_message(const nlohmann::json& message);

private:
    class Session;

    const ap_runtime::ApRuntime* runtime_{nullptr};
    const room_runtime::RoomRuntime* room_runtime_{nullptr};
    std::mutex mutex_;
    std::vector<std::shared_ptr<Session>> sessions_;
};

class ApServer {
public:
    ApServer(boost::asio::io_context& io, ap_runtime::ApRuntime& runtime,
             room_runtime::RoomRuntime& room_runtime,
             std::shared_ptr<WebSocketBroadcaster> broadcaster, std::uint16_t port);

    Result<void> run();
    void stop();
    std::uint16_t port() const;

private:
    using Tcp = boost::asio::ip::tcp;
    using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
    using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;

    void start_accept();
    void handle_connection(std::shared_ptr<Tcp::socket> socket);
    void cleanup_workers();
    void remove_active_socket(const std::shared_ptr<Tcp::socket>& socket);
    Result<void> start_battle_from_room(const room::RoomSnapshot& room_snapshot,
                                        std::int64_t occurred_at_ms,
                                        std::int64_t duration_ms);
    HttpResponse handle_request(const HttpRequest& request);
    HttpResponse handle_room_request(const HttpRequest& request, std::string target);
    HttpResponse handle_app_query_request(const HttpRequest& request, std::string target);
    HttpResponse handle_app_page_request(const HttpRequest& request, std::string target);
    HttpResponse handle_console_request(const HttpRequest& request, std::string target);
    HttpResponse handle_sim_request(const HttpRequest& request, std::string target);
    HttpResponse json_response(boost::beast::http::status status, nlohmann::json body,
                               unsigned version, bool keep_alive) const;
    HttpResponse text_response(boost::beast::http::status status, std::string body,
                               std::string content_type, unsigned version, bool keep_alive) const;

    boost::asio::io_context& io_;
    ap_runtime::ApRuntime& runtime_;
    room_runtime::RoomRuntime& room_runtime_;
    std::shared_ptr<WebSocketBroadcaster> broadcaster_;
    Tcp::acceptor acceptor_;
    std::atomic_bool stopping_{false};
    std::mutex worker_mutex_;
    std::vector<std::thread> workers_;
    std::mutex active_socket_mutex_;
    std::vector<std::weak_ptr<Tcp::socket>> active_sockets_;
};

} // namespace fenghuo::server
