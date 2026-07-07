# Self-built engine root.
# See docs/自研引擎架构设计.md for architecture baseline.

# ============================================================
# Engine-wide interface target for shared settings
# ============================================================
# Defined BEFORE add_subdirectory so all modules can link to it.
add_library(snt_engine_settings INTERFACE)
target_compile_features(snt_engine_settings INTERFACE cxx_std_20)
target_include_directories(snt_engine_settings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================
# Third-party dependencies
# ============================================================
# Heavy libs (SDL3) come from extracted source under third_party/.
# Header-only libs (VMA, EnTT, stb, nlohmann_json, vulkan-headers) are
# fetched via FetchContent from pre-downloaded archives.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch_third_party.cmake)

# ============================================================
# Engine modules
# ============================================================
# Each module is its own static library for clear layering.
# Layering rule (top -> bottom, never reverse):
#   engine_app -> engine -> {ui, render, voxel, physics, script, audio, network}
#                         -> renderer -> render_backend -> platform
#                         -> ecs -> input -> core
#                         -> data -> core
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(input)
add_subdirectory(render_backend)
add_subdirectory(renderer)
add_subdirectory(ecs)
add_subdirectory(render)
add_subdirectory(ui)
add_subdirectory(engine)
add_subdirectory(engine_app)

# Tests are optional: gated by SNT_BUILD_TESTS (default ON). CI can
# disable with -DSNT_BUILD_TESTS=OFF to speed up Release builds.
if(SNT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
