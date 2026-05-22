#include "fenghuo/result.hpp"

#include <memory>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        throw fenghuo::BadResultAccess(message);
    }
}

void test_value_result_success() {
    auto result = fenghuo::Result<int>::ok(42);

    expect(result.has_value(), "success result should have value");
    expect(static_cast<bool>(result), "success result should convert to true");
    expect(result.value() == 42, "success result should return stored value");

    result.value() = 43;
    expect(result.value() == 43, "success result should expose mutable value");
}

void test_value_result_error() {
    auto result = fenghuo::Result<int>::err(
        {fenghuo::ErrorCode::InvalidArgument, "invalid test input"});

    expect(!result.has_value(), "error result should not have value");
    expect(!static_cast<bool>(result), "error result should convert to false");
    expect(result.error().code == fenghuo::ErrorCode::InvalidArgument,
           "error result should expose error code");
    expect(result.error().message == "invalid test input",
           "error result should expose error message");
}

void test_move_only_value() {
    auto result = fenghuo::Result<std::unique_ptr<int>>::ok(std::make_unique<int>(7));

    auto value = std::move(result).value();
    expect(value != nullptr, "moved result should return move-only value");
    expect(*value == 7, "moved result should preserve move-only value");
}

void test_void_result_success() {
    auto result = fenghuo::Result<void>::ok();

    expect(result.has_value(), "void success result should have value");
    expect(static_cast<bool>(result), "void success result should convert to true");
    result.value();
}

void test_void_result_error() {
    auto result = fenghuo::Result<void>::err({fenghuo::ErrorCode::StorageFailure, "disk full"});

    expect(!result.has_value(), "void error result should not have value");
    expect(result.error().code == fenghuo::ErrorCode::StorageFailure,
           "void error result should expose error code");
    expect(result.error().message == "disk full", "void error result should expose message");
}

void test_bad_access_throws() {
    auto error_result = fenghuo::Result<std::string>::err({fenghuo::ErrorCode::Internal, "boom"});
    bool value_threw = false;
    try {
        static_cast<void>(error_result.value());
    } catch (const fenghuo::BadResultAccess&) {
        value_threw = true;
    }
    expect(value_threw, "value() on error result should throw BadResultAccess");

    auto ok_result = fenghuo::Result<std::string>::ok("ok");
    bool error_threw = false;
    try {
        static_cast<void>(ok_result.error());
    } catch (const fenghuo::BadResultAccess&) {
        error_threw = true;
    }
    expect(error_threw, "error() on success result should throw BadResultAccess");
}

} // namespace

int main() {
    test_value_result_success();
    test_value_result_error();
    test_move_only_value();
    test_void_result_success();
    test_void_result_error();
    test_bad_access_throws();
    return 0;
}
