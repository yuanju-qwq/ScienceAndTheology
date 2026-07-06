# Fetch third-party libraries from pre-downloaded archives.
# Run engine/cmake/download_third_party.ps1 BEFORE cmake configure.
# This avoids cmake's curl/schannel issues with GitHub redirects.

include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "")
set(_SNT_DOWNLOADS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/_downloads")

# ============================================================
# Vulkan-Headers (header + loader API)
# ============================================================
# Use pre-downloaded zip to avoid git clone issues.
FetchContent_Declare(
    VulkanHeaders
    URL ${_SNT_DOWNLOADS_DIR}/VulkanHeaders-v1.3.295.zip
)
FetchContent_MakeAvailable(VulkanHeaders)
# Vulkan-Headers' own CMake defines Vulkan::Headers target.

# ============================================================
# Vulkan Memory Allocator (VMA) — header-only
# ============================================================
FetchContent_Declare(
    VulkanMemoryAllocator
    URL ${_SNT_DOWNLOADS_DIR}/VulkanMemoryAllocator-v3.1.0.zip
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# VMA is header-only; expose a thin interface target with include path
# and our config defines (recording + stats for GPU memory analysis).
add_library(vma_config INTERFACE)
target_include_directories(vma_config INTERFACE ${VulkanMemoryAllocator_SOURCE_DIR}/include)
target_compile_definitions(vma_config INTERFACE
    VMA_RECORDING_ENABLED=1
    VMA_STATS_STRING_ENABLED=1
)

# ============================================================
# EnTT (ECS, header-only)
# ============================================================
FetchContent_Declare(
    EnTT
    URL ${_SNT_DOWNLOADS_DIR}/EnTT-v3.13.2.zip
)
FetchContent_MakeAvailable(EnTT)
# EnTT's CMake defines EnTT::EnTT target.

# ============================================================
# stb (image loading, header-only)
# ============================================================
FetchContent_Declare(
    stb
    URL ${_SNT_DOWNLOADS_DIR}/stb-master.zip
)
FetchContent_MakeAvailable(stb)
# stb has no CMake target; create one.
if(NOT TARGET stb::stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
    add_library(stb::stb ALIAS stb)
endif()

# ============================================================
# nlohmann_json (JSON parsing, header-only)
# ============================================================
FetchContent_Declare(
    nlohmann_json
    URL ${_SNT_DOWNLOADS_DIR}/nlohmann_json-v3.11.3.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================
# SDL3 (extracted source under third_party/)
# ============================================================
# SDL3 is built from source for full control and debug symbols.
# Source is downloaded by download_third_party.ps1 and extracted to
# third_party/SDL-release-3.4.10/.
set(_SDL3_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL-release-3.4.10)
if(EXISTS ${_SDL3_SOURCE_DIR}/CMakeLists.txt)
    set(SDL_SHARED ON CACHE BOOL "Build SDL as shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Don't build SDL static" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Don't build SDL test lib" FORCE)
    add_subdirectory(${_SDL3_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL3-build EXCLUDE_FROM_ALL)
endif()

# ============================================================
# Helper: target link to all engine third-party
# ============================================================
add_library(snt_third_party INTERFACE)
target_link_libraries(snt_third_party INTERFACE
    Vulkan::Headers
    vma_config
    EnTT::EnTT
    stb::stb
    nlohmann_json::nlohmann_json
)
if(TARGET SDL3-shared)
    target_link_libraries(snt_third_party INTERFACE SDL3-shared)
endif()
