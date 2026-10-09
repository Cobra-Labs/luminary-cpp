cmake_minimum_required(VERSION 3.20)
project(luminary CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/open-fixture-language/cpp)

# Fixture definitions are runtime assets of the engine. Keep them next to the
# executable in normal build trees so startup does not depend on the cwd.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures DESTINATION ${CMAKE_BINARY_DIR})

# Core Library
add_library(luminary_core STATIC
        src/core/engine/engine.cpp
        src/core/universe/universe.cpp
        src/core/engine/engine.h
)
target_include_directories(luminary_core PUBLIC src)
target_link_libraries(luminary_core PRIVATE ofl-cpp)

# Main Executable
add_executable(luminary src/main.cpp
        src/config/config.cpp
        src/core/patch/patch.cpp
        src/core/patch/patch.h
        src/core/dmx_types.h
        src/core/engine/engine.h
        src/protocol/artnet.cpp
        src/protocol/artnet.h
        src/protocol/artnet_poll.cpp
        src/protocol/artnet_poll.h
        src/protocol/udp.cpp
        src/protocol/udp.h
        src/net/socket_server.cpp
        src/net/socket_server.h
        src/net/http_server.cpp
        src/net/http_server.h
        src/net/pin_auth.cpp
        src/net/pin_auth.h
        src/net/node_discovery.cpp
        src/net/node_discovery.h
        src/net/remote_bridge.cpp
        src/net/remote_bridge.h
        src/util/log.h
)
target_include_directories(luminary PRIVATE
        src
        third_party/tomlplusplus/include
        third_party/open-fixture-language/cpp/include
)
target_link_libraries(luminary PRIVATE
        luminary_core
        ofl-cpp
)

if (WIN32)
    target_link_libraries(luminary PRIVATE ws2_32)
endif()