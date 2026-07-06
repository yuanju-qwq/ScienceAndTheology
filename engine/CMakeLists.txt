# Self-built engine root.
# See docs/自研引擎架构设计.md for architecture baseline.

# ============================================================
# Third-party dependencies
# ============================================================
# Heavy libs (SDL3, shaderc) come from git submodules under third_party/.
# Header-only libs (VMA, EnTT, stb, nlohmann_json, vulkan-headers) are
# fetched via FetchContent for simplicity.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch_third_party.cmake)

# ============================================================
# Engine modules
# ============================================================
# Each module is its own static library for clear layering.
# Layering rule (top -> bottom, never reverse):
#   engine_app -> {ui, voxel, physics, script, audio, network}
#             -> ecs -> data -> render_backend -> platform

add_subdirectory(platform)
add_subdirectory(render_backend)
add_subdirectory(engine_app)

# ============================================================
# Engine-wide interface target for shared settings
# ============================================================
add_library(snt_engine_settings INTERFACE)
target_compile_features(snt_engine_settings INTERFACE cxx_std_20)
target_include_directories(snt_engine_settings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
