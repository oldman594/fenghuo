#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace fenghuo {

enum class ErrorCode {
    InvalidArgument,
    ParseError,
    UnsupportedSchema,
    UnknownEventType,
    MissingField,
    InvalidPayload,
    DuplicateEvent,
    Conflict,
    NotFound,
    StorageFailure,
    TransportFailure,
    Internal,
};

struct Error {
    ErrorCode code;
    std::string message;
};

class BadResultAccess final : public std::logic_error {
public:
    explicit BadResultAccess(const char* message) : std::logic_error(message) {}
};

template <typename T>
class Result {
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results");
    static_assert(!std::is_reference_v<T>, "Result<T> does not support reference value types");

public:
    using value_type = T;

    static Result ok(T value) { return Result(std::move(value)); }

    static Result err(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(state_); }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    T& value() & {
        if (!has_value()) {
            throw BadResultAccess("Result::value() called on error result");
        }
        return std::get<T>(state_);
    }

    const T& value() const& {
        if (!has_value()) {
            throw BadResultAccess("Result::value() called on error result");
        }
        return std::get<T>(state_);
    }

    T&& value() && {
        if (!has_value()) {
            throw BadResultAccess("Result::value() called on error result");
        }
        return std::move(std::get<T>(state_));
    }

    const Error& error() const& {
        if (has_value()) {
            throw BadResultAccess("Result::error() called on success result");
        }
        return std::get<Error>(state_);
    }

    Error&& error() && {
        if (has_value()) {
            throw BadResultAccess("Result::error() called on success result");
        }
        return std::move(std::get<Error>(state_));
    }

private:
    explicit Result(T value) : state_(std::move(value)) {}
    explicit Result(Error error) : state_(std::move(error)) {}

    std::variant<T, Error> state_;
};

template <>
class Result<void> {
public:
    static Result ok() { return Result(); }

    static Result err(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value; }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    void value() const {
        if (!has_value()) {
            throw BadResultAccess("Result<void>::value() called on error result");
        }
    }

    const Error& error() const& {
        if (has_value()) {
            throw BadResultAccess("Result<void>::error() called on success result");
        }
        return error_.error;
    }

    Error&& error() && {
        if (has_value()) {
            throw BadResultAccess("Result<void>::error() called on success result");
        }
        return std::move(error_.error);
    }

private:
    struct ErrorState {
        bool has_value;
        Error error;
    };

    Result() = default;
    explicit Result(Error error) : error_{true, std::move(error)} {}

    ErrorState error_{false, {ErrorCode::Internal, {}}};
};

} // namespace fenghuo
