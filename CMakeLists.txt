cmake_minimum_required(VERSION 3.22)

project(fenghuo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FENGHUO_BUILD_TESTS "Build Fenghuo tests" ON)

add_library(fenghuo_core INTERFACE)
target_include_directories(fenghuo_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

add_library(fenghuo_ap
    src/ap_runtime/runtime.cpp
    src/core/battle.cpp
    src/protocol/event.cpp
    src/room/protocol.cpp
    src/room/room.cpp
    src/room_runtime/runtime.cpp
    src/server/server.cpp
    src/storage/jsonl_event_store.cpp
)
target_compile_definitions(fenghuo_ap
    PRIVATE
        FENGHUO_CONSOLE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/apps/console"
        FENGHUO_SIM_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/apps/sim"
)
target_link_libraries(fenghuo_ap
    PUBLIC
        fenghuo_core
        nlohmann_json::nlohmann_json
        Boost::system
        Threads::Threads
)

add_executable(fenghuo-apd apps/apd/main.cpp)
target_link_libraries(fenghuo-apd PRIVATE fenghuo_ap)

if(FENGHUO_BUILD_TESTS)
    enable_testing()
    add_executable(fenghuo-result-test tests/result_test.cpp)
    target_link_libraries(fenghuo-result-test PRIVATE fenghuo_core)
    add_test(NAME fenghuo.result COMMAND fenghuo-result-test)

    add_executable(fenghuo-ap-loop-test tests/ap_loop_test.cpp)
    target_link_libraries(fenghuo-ap-loop-test PRIVATE fenghuo_ap)
    add_test(NAME fenghuo.ap_loop COMMAND fenghuo-ap-loop-test)

    add_executable(fenghuo-room-test tests/room_test.cpp)
    target_link_libraries(fenghuo-room-test PRIVATE fenghuo_ap)
    add_test(NAME fenghuo.room COMMAND fenghuo-room-test)

    add_executable(fenghuo-server-integration-test tests/server_integration_test.cpp)
    target_link_libraries(fenghuo-server-integration-test PRIVATE fenghuo_ap)
    add_test(NAME fenghuo.server_integration COMMAND fenghuo-server-integration-test)
endif()
