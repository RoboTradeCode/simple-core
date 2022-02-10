cmake_minimum_required(VERSION 3.22)
project(trade_core)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(Boost 1.78.0 REQUIRED COMPONENTS log_setup log)

# Aeron Client
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron/aeron-client/src/main/cpp)

# Sentry
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native/include)

# Simdjson
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson/include)

# Toml++
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/tomlplusplus EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/tomlplusplus/include)

SET(SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Subscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Publisher.cpp)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Subscriber.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Publisher.h)

add_executable(trade_core ${SOURCE} ${HEADERS})

target_link_libraries(trade_core
    Threads::Threads
    Boost::log_setup
    Boost::log
    aeron_client
    sentry::sentry
    simdjson::simdjson
    tomlplusplus::tomlplusplus)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.toml ${CMAKE_CURRENT_BINARY_DIR}/config.toml COPYONLY)
