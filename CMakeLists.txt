cmake_minimum_required(VERSION 3.20)
project(luminary CXX)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/open-fixture-language/cpp)

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
        src/protocol/udp.cpp
        src/protocol/udp.h
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