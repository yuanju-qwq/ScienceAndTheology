# Top-level CMake for ScienceAndTheology.
# Old GDExtension C++ lives under src/ (legacy, will be removed in P7).
# New self-built engine lives under engine/ (P0 architecture baseline).

cmake_minimum_required(VERSION 3.24)

project(ScienceAndTheology_Engine VERSION 0.1.0 LANGUAGES C CXX)

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

# ============================================================
# Build configuration
# ============================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# MSVC: use UTF-8, conformance mode, and modern exception handling.
if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ============================================================
# Sub-projects
# ============================================================
# New self-built engine (P0 baseline).
add_subdirectory(engine)

# Legacy GDExtension C++ — kept for reference until P7 migration completes.
# To build legacy, configure from src/ as its own CMake project.
