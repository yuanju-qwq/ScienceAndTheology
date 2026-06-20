cmake_minimum_required(VERSION 3.20)
project(ScienceAndTheology VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch godot-cpp for GDExtension bindings
include(FetchContent)
FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG 10.0.0-rc1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(godot-cpp)

# Core library (pure C++, no Godot dependency)
add_subdirectory(core)

# snt_server — authoritative server module (engine-agnostic, sockets + protocol)
# Design: docs/多人游戏系统设计.md §4 方案B
add_subdirectory(server)

# Engine adapter interfaces (header-only)
add_subdirectory(adapters)

# Godot-specific adapter implementations
add_subdirectory(godot_adapters)

# GDExtension bindings (shared library)
add_subdirectory(bindings)

if(BUILD_TESTING)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../tests"
        "${CMAKE_CURRENT_BINARY_DIR}/tests"
    )
endif()
