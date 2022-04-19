cmake_minimum_required(VERSION 3.21)
project(trade_core)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Debug)

find_package(Threads REQUIRED)
find_package(Boost 1.78.0 REQUIRED)

# AeronCpp
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron_cpp EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron_cpp/src)

# Sentry
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native/include)

# Simdjson
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson/include)

# Spdlog
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/spdlog EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/spdlog/include)

# Toml++
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/tomlplusplus EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/tomlplusplus/include)

SET(SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.cpp)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.h)

add_executable(trade_core ${SOURCE} ${HEADERS})

target_link_libraries(trade_core
    Threads::Threads
    aeron_cpp::aeron_cpp
    aeron_client
    sentry::sentry
    simdjson::simdjson
    spdlog::spdlog
    tomlplusplus::tomlplusplus)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.toml.example ${CMAKE_CURRENT_BINARY_DIR}/config.toml COPYONLY)
