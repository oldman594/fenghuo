#include "storage/event_store.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace fenghuo::storage {

namespace {

Result<std::int64_t> required_i64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_number_integer()) {
        return Result<std::int64_t>::err(
            {ErrorCode::ParseError, std::string("invalid JSONL metadata field: ") + key});
    }
    return Result<std::int64_t>::ok(object.at(key).get<std::int64_t>());
}

Result<std::uint64_t> required_u64(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object.at(key).is_number_unsigned()) {
        return Result<std::uint64_t>::err(
            {ErrorCode::ParseError, std::string("invalid JSONL metadata field: ") + key});
    }
    return Result<std::uint64_t>::ok(object.at(key).get<std::uint64_t>());
}

Result<AcceptedEventRecord> parse_record_line(const std::string& line,
                                              const std::filesystem::path& path,
                                              std::uint64_t line_number) {
    nlohmann::json record;
    try {
        record = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error& error) {
        return Result<AcceptedEventRecord>::err(
            {ErrorCode::ParseError,
             "failed to parse JSONL record " + path.string() + ":" + std::to_string(line_number) +
                 ": " + error.what()});
    }

    if (!record.is_object() || !record.contains("event") || !record.contains("metadata")) {
        return Result<AcceptedEventRecord>::err(
            {ErrorCode::ParseError,
             "invalid JSONL record shape " + path.string() + ":" + std::to_string(line_number)});
    }

    auto envelope = protocol::parse_event_json(record.at("event").dump());
    if (!envelope) {
        return Result<AcceptedEventRecord>::err(envelope.error());
    }

    const auto& metadata_json = record.at("metadata");
    if (!metadata_json.is_object()) {
        return Result<AcceptedEventRecord>::err(
            {ErrorCode::ParseError,
             "invalid JSONL metadata shape " + path.string() + ":" + std::to_string(line_number)});
    }

    AcceptedEventMetadata metadata;
    auto received_at = required_i64(metadata_json, "received_at_ms");
    if (!received_at) {
        return Result<AcceptedEventRecord>::err(received_at.error());
    }
    metadata.received_at_ms = received_at.value();

    auto accepted_at = required_i64(metadata_json, "accepted_at_ms");
    if (!accepted_at) {
        return Result<AcceptedEventRecord>::err(accepted_at.error());
    }
    metadata.accepted_at_ms = accepted_at.value();

    auto sequence = required_u64(metadata_json, "acceptance_sequence");
    if (!sequence) {
        return Result<AcceptedEventRecord>::err(sequence.error());
    }
    metadata.acceptance_sequence = sequence.value();

    return Result<AcceptedEventRecord>::ok({std::move(envelope).value(), metadata});
}

Result<AcceptedRoomEventRecord> parse_room_record_line(const std::string& line,
                                                       const std::filesystem::path& path,
                                                       std::uint64_t line_number) {
    nlohmann::json record;
    try {
        record = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error& error) {
        return Result<AcceptedRoomEventRecord>::err(
            {ErrorCode::ParseError,
             "failed to parse room JSONL record " + path.string() + ":" +
                 std::to_string(line_number) + ": " + error.what()});
    }

    if (!record.is_object() || !record.contains("event") || !record.contains("metadata")) {
        return Result<AcceptedRoomEventRecord>::err(
            {ErrorCode::ParseError,
             "invalid room JSONL record shape " + path.string() + ":" +
                 std::to_string(line_number)});
    }

    auto envelope = room::parse_room_event_json(record.at("event").dump());
    if (!envelope) {
        return Result<AcceptedRoomEventRecord>::err(envelope.error());
    }

    const auto& metadata_json = record.at("metadata");
    if (!metadata_json.is_object()) {
        return Result<AcceptedRoomEventRecord>::err(
            {ErrorCode::ParseError,
             "invalid room JSONL metadata shape " + path.string() + ":" +
                 std::to_string(line_number)});
    }

    AcceptedEventMetadata metadata;
    auto received_at = required_i64(metadata_json, "received_at_ms");
    if (!received_at) {
        return Result<AcceptedRoomEventRecord>::err(received_at.error());
    }
    metadata.received_at_ms = received_at.value();

    auto accepted_at = required_i64(metadata_json, "accepted_at_ms");
    if (!accepted_at) {
        return Result<AcceptedRoomEventRecord>::err(accepted_at.error());
    }
    metadata.accepted_at_ms = accepted_at.value();

    auto sequence = required_u64(metadata_json, "acceptance_sequence");
    if (!sequence) {
        return Result<AcceptedRoomEventRecord>::err(sequence.error());
    }
    metadata.acceptance_sequence = sequence.value();

    return Result<AcceptedRoomEventRecord>::ok({std::move(envelope).value(), metadata});
}

} // namespace

JsonlBattleEventStore::JsonlBattleEventStore(std::filesystem::path root) : root_(std::move(root)) {}

Result<void> JsonlBattleEventStore::append(const protocol::EventEnvelope& envelope,
                                           const AcceptedEventMetadata& metadata) {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) {
        return Result<void>::err(
            {ErrorCode::StorageFailure, "failed to create event log directory: " + error.message()});
    }

    const auto path = root_ / (envelope.battle_id + ".jsonl");
    std::ofstream output(path, std::ios::app);
    if (!output) {
        return Result<void>::err({ErrorCode::StorageFailure, "failed to open event log: " + path.string()});
    }

    nlohmann::json record = {
        {"event", protocol::to_json(envelope)},
        {"metadata",
         {
             {"received_at_ms", metadata.received_at_ms},
             {"accepted_at_ms", metadata.accepted_at_ms},
             {"acceptance_sequence", metadata.acceptance_sequence},
         }},
    };

    output << record.dump() << '\n';
    output.flush();
    if (!output) {
        return Result<void>::err(
            {ErrorCode::StorageFailure, "failed to flush event log: " + path.string()});
    }

    return Result<void>::ok();
}

Result<std::vector<AcceptedEventRecord>> JsonlBattleEventStore::read_all() const {
    std::vector<AcceptedEventRecord> records;
    if (!std::filesystem::exists(root_)) {
        return Result<std::vector<AcceptedEventRecord>>::ok(std::move(records));
    }

    std::error_code error;
    if (!std::filesystem::is_directory(root_, error) || error) {
        return Result<std::vector<AcceptedEventRecord>>::err(
            {ErrorCode::StorageFailure, "event log root is not a directory: " + root_.string()});
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (error) {
            return Result<std::vector<AcceptedEventRecord>>::err(
                {ErrorCode::StorageFailure, "failed to read event log directory: " + error.message()});
        }
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        std::ifstream input(path);
        if (!input) {
            return Result<std::vector<AcceptedEventRecord>>::err(
                {ErrorCode::StorageFailure, "failed to open event log for replay: " + path.string()});
        }

        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            if (line.empty()) {
                continue;
            }
            auto record = parse_record_line(line, path, line_number);
            if (!record) {
                return Result<std::vector<AcceptedEventRecord>>::err(record.error());
            }
            records.push_back(std::move(record).value());
        }
    }

    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return left.metadata.acceptance_sequence < right.metadata.acceptance_sequence;
    });
    return Result<std::vector<AcceptedEventRecord>>::ok(std::move(records));
}

JsonlRoomEventStore::JsonlRoomEventStore(std::filesystem::path root) : root_(std::move(root)) {}

Result<void> JsonlRoomEventStore::append(const room::RoomEventEnvelope& envelope,
                                         const AcceptedEventMetadata& metadata) {
    const auto room_root = root_ / "rooms";
    std::error_code error;
    std::filesystem::create_directories(room_root, error);
    if (error) {
        return Result<void>::err({ErrorCode::StorageFailure,
                                  "failed to create room event log directory: " + error.message()});
    }

    const auto path = room_root / (envelope.room_id + ".jsonl");
    std::ofstream output(path, std::ios::app);
    if (!output) {
        return Result<void>::err(
            {ErrorCode::StorageFailure, "failed to open room event log: " + path.string()});
    }

    nlohmann::json record = {
        {"event", room::to_json(envelope)},
        {"metadata",
         {
             {"received_at_ms", metadata.received_at_ms},
             {"accepted_at_ms", metadata.accepted_at_ms},
             {"acceptance_sequence", metadata.acceptance_sequence},
         }},
    };

    output << record.dump() << '\n';
    output.flush();
    if (!output) {
        return Result<void>::err(
            {ErrorCode::StorageFailure, "failed to flush room event log: " + path.string()});
    }

    return Result<void>::ok();
}

Result<std::vector<AcceptedRoomEventRecord>> JsonlRoomEventStore::read_all() const {
    std::vector<AcceptedRoomEventRecord> records;
    const auto room_root = root_ / "rooms";
    if (!std::filesystem::exists(room_root)) {
        return Result<std::vector<AcceptedRoomEventRecord>>::ok(std::move(records));
    }

    std::error_code error;
    if (!std::filesystem::is_directory(room_root, error) || error) {
        return Result<std::vector<AcceptedRoomEventRecord>>::err(
            {ErrorCode::StorageFailure, "room event log root is not a directory: " + room_root.string()});
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(room_root, error)) {
        if (error) {
            return Result<std::vector<AcceptedRoomEventRecord>>::err(
                {ErrorCode::StorageFailure, "failed to read room event log directory: " + error.message()});
        }
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        std::ifstream input(path);
        if (!input) {
            return Result<std::vector<AcceptedRoomEventRecord>>::err(
                {ErrorCode::StorageFailure, "failed to open room event log for replay: " + path.string()});
        }

        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            if (line.empty()) {
                continue;
            }
            auto record = parse_room_record_line(line, path, line_number);
            if (!record) {
                return Result<std::vector<AcceptedRoomEventRecord>>::err(record.error());
            }
            records.push_back(std::move(record).value());
        }
    }

    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return left.metadata.acceptance_sequence < right.metadata.acceptance_sequence;
    });
    return Result<std::vector<AcceptedRoomEventRecord>>::ok(std::move(records));
}

} // namespace fenghuo::storage
