cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(MiniRedis VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings for clean systems code
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Core static library
add_library(miniredis_core
    src/store.cpp
    src/resp.cpp
    src/poller.cpp
    src/command.cpp
    src/server.cpp
    src/aof.cpp
)
target_include_directories(miniredis_core PUBLIC include)

# Server executable
add_executable(miniredis-server
    src/main.cpp
)
target_link_libraries(miniredis-server PRIVATE miniredis_core)

# GoogleTest setup via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Unit tests: Store & Expiry
add_executable(store_test
    tests/store_test.cpp
)
target_link_libraries(store_test PRIVATE miniredis_core GTest::gtest_main)

# Unit tests: RESP Parser
add_executable(resp_test
    tests/resp_test.cpp
)
target_link_libraries(resp_test PRIVATE miniredis_core GTest::gtest_main)

# Unit tests: Eviction (LRU & LFU)
add_executable(eviction_test
    tests/eviction_test.cpp
)
target_link_libraries(eviction_test PRIVATE miniredis_core GTest::gtest_main)

# Unit tests: AOF Persistence
add_executable(aof_test
    tests/aof_test.cpp
)
target_link_libraries(aof_test PRIVATE miniredis_core GTest::gtest_main)

# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN)
    message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    message(STATUS "Enabling ThreadSanitizer")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# Unit tests: Observability & Metrics
add_executable(observability_test
    tests/observability_test.cpp
)
target_link_libraries(observability_test PRIVATE miniredis_core GTest::gtest_main)

# Unit tests: RESP Parser Fuzzing & Mutation
add_executable(fuzz_test
    tests/fuzz_test.cpp
)
target_link_libraries(fuzz_test PRIVATE miniredis_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(store_test)
gtest_discover_tests(resp_test)
gtest_discover_tests(eviction_test)
gtest_discover_tests(aof_test)
gtest_discover_tests(observability_test)
gtest_discover_tests(fuzz_test)
