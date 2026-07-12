# Top-level CMake for ScienceAndTheology.
# New self-built engine lives under snt_engine/ (P0 architecture baseline).

cmake_minimum_required(VERSION 3.24)

project(ScienceAndTheology_Engine VERSION 0.1.0 LANGUAGES C CXX)

# ASM_MASM is enabled lazily inside fetch_third_party.cmake (only when
# MSVC x64 is the active toolchain). Declaring it here too would force
# every generator to probe for an assembler even on non-MSVC builds.

# ============================================================
# Global settings
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use folders in IDEs for clarity.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Output to a single bin directory for easy access.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Generate compile_commands.json for IDE/clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CTest starts at this top-level build tree. Subprojects may decide whether to
# add tests, but enabling registration here ensures host-owned test targets
# are discoverable alongside engine tests.
enable_testing()

# ============================================================
# Build configuration
# ============================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# MSVC: use UTF-8 source encoding. Conformance mode, modern exception
# handling and /Zc:__cplusplus are applied via `snt_engine_settings` so
# they only affect engine targets — third-party libs fetched via
# FetchContent (SDL3, AngelScript, etc.) keep their own CMake-configured
# flags and do not inherit these. This matters because some third-party
# targets include MASM sources (e.g. AngelScript's x64 native calling
# convention), and ml64.exe rejects C++ flags like /Zc:__cplusplus.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ============================================================
# Sub-projects
# ============================================================
# New self-built engine (P0 baseline).
add_subdirectory(snt_engine)
# The game is the engine host. It owns runtime paths and package assembly.
add_subdirectory(game)
