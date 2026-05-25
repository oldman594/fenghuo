#include "ap_runtime/runtime.hpp"
#include "room_runtime/runtime.hpp"
#include "server/server.hpp"
#include "storage/event_store.hpp"

#include <boost/asio.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>

namespace {

struct Config {
    std::uint16_t port{8080};
    std::string event_log_root{"data/events"};
};

void print_usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " [--port PORT] [--event-log-root PATH]\n";
}

fenghuo::Result<Config> parse_args(int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--port" && index + 1 < argc) {
            int port = 0;
            try {
                port = std::stoi(argv[++index]);
            } catch (const std::exception&) {
                return fenghuo::Result<Config>::err(
                    {fenghuo::ErrorCode::InvalidArgument, "port must be an integer"});
            }
            if (port <= 0 || port > 65535) {
                return fenghuo::Result<Config>::err(
                    {fenghuo::ErrorCode::InvalidArgument, "port must be in 1..65535"});
            }
            config.port = static_cast<std::uint16_t>(port);
            continue;
        }
        if (arg == "--event-log-root" && index + 1 < argc) {
            config.event_log_root = argv[++index];
            continue;
        }
        return fenghuo::Result<Config>::err(
            {fenghuo::ErrorCode::InvalidArgument, "unknown or incomplete argument: " + arg});
    }
    return fenghuo::Result<Config>::ok(std::move(config));
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto config = parse_args(argc, argv);
        if (!config) {
            std::cerr << config.error().message << '\n';
            print_usage(argv[0]);
            return 2;
        }

        auto store = std::make_shared<fenghuo::storage::JsonlBattleEventStore>(
            config.value().event_log_root);
        auto room_store =
            std::make_shared<fenghuo::storage::JsonlRoomEventStore>(config.value().event_log_root);
        auto composite_sink = std::make_shared<fenghuo::ap_runtime::CompositeBattleUpdateSink>();
        auto room_composite_sink =
            std::make_shared<fenghuo::room_runtime::CompositeRoomUpdateSink>();
        auto websocket_broadcaster = std::make_shared<fenghuo::server::WebSocketBroadcaster>();

        fenghuo::ap_runtime::ApRuntime runtime(store, composite_sink);
        fenghuo::room_runtime::RoomRuntime room_runtime(room_store, room_composite_sink);
        websocket_broadcaster->bind_runtimes(&runtime, &room_runtime);
        composite_sink->add(websocket_broadcaster);
        room_composite_sink->add(websocket_broadcaster);
        auto replay_records = store->read_all();
        if (!replay_records) {
            std::cerr << "failed to read event log for replay: " << replay_records.error().message
                      << '\n';
            return 1;
        }
        auto replay = runtime.replay(replay_records.value());
        if (!replay) {
            std::cerr << "failed to replay event log: " << replay.error().message << '\n';
            return 1;
        }
        auto room_replay_records = room_store->read_all();
        if (!room_replay_records) {
            std::cerr << "failed to read room event log for replay: "
                      << room_replay_records.error().message << '\n';
            return 1;
        }
        auto room_replay = room_runtime.replay(room_replay_records.value());
        if (!room_replay) {
            std::cerr << "failed to replay room event log: " << room_replay.error().message << '\n';
            return 1;
        }

        boost::asio::io_context io;
        fenghuo::server::ApServer server(
            io, runtime, room_runtime, websocket_broadcaster, config.value().port);
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&server](const boost::system::error_code& error, int) {
            if (!error) {
                server.stop();
            }
        });
        std::cout << "fenghuo-apd listening on 0.0.0.0:" << config.value().port
                  << ", event log root: " << config.value().event_log_root
                  << ", replayed events: " << replay_records.value().size()
                  << ", replayed room events: " << room_replay_records.value().size() << '\n';
        auto run = server.run();
        if (!run) {
            std::cerr << "server failed: " << run.error().message << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
