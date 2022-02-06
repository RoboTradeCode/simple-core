cmake_minimum_required(VERSION 3.22)
project(trade_core)

set(CMAKE_CXX_STANDARD 20)

# Boost.Log
find_package(Boost 1.78.0 REQUIRED COMPONENTS log_setup log)

# Aeron Client
find_package(Threads REQUIRED)
add_definitions(-DAERON_VERSION_TXT="1.38.0-SNAPSHOT")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron-client EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/aeron-client)

# Toml++
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/toml++)

# Simdjson
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson)

# Sentry
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native EXCLUDE_FROM_ALL)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libs/sentry-native/include)

SET(SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson/simdjson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Subscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Publisher.cpp)

SET(HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/simdjson/simdjson.h
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
    sentry)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.toml ${CMAKE_CURRENT_BINARY_DIR}/config.toml COPYONLY)
