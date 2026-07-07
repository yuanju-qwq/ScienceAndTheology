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
# Volk — Vulkan loader (dynamic, no SDK required at runtime).
# Loads vulkan-1.dll via LoadLibrary; no need to link vulkan-1.lib.
# ============================================================
FetchContent_Declare(
    Volk
    URL ${_SNT_DOWNLOADS_DIR}/volk-master.zip
)
FetchContent_MakeAvailable(Volk)

# Volk's CMake defines `volk` target (static lib) when VULKAN_HPP_CMAKE is
# OFF (default). It needs Vulkan headers; Vulkan-Headers is fetched above
# so `Vulkan::Headers` target exists.
if(TARGET volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    # Define VK_NO_PROTOTYPES so Volk provides the function pointers.
    target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
endif()

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
# GLM (math library, header-only)
# ============================================================
# Used for MVP matrices (mat4, vec3, perspective, lookAt).
# Pulled via FetchContent; headers live in _deps/glm-src/glm/.
# To modify GLM later: copy _deps/glm-src/glm/ to engine/core/math/glm/
# and switch the include path below.
FetchContent_Declare(
    GLM
    URL ${_SNT_DOWNLOADS_DIR}/glm-1.0.1.zip
)
FetchContent_MakeAvailable(GLM)
# GLM's CMake defines glm::glm target.

# ============================================================
# tinyobjloader (.obj mesh loading, header-only)
# ============================================================
FetchContent_Declare(
    tinyobjloader
    URL ${_SNT_DOWNLOADS_DIR}/tinyobjloader-release.zip
)
FetchContent_MakeAvailable(tinyobjloader)
# tinyobjloader's CMake defines a `tinyobjloader` target (no namespace).
# The `tinyobjloader::` namespace is only applied on install/export, which
# does not happen in a FetchContent build. Create an ALIAS so downstream
# code can use the `tinyobjloader::tinyobjloader` name consistently
# (matches the pattern used for stb::stb above).
if(NOT TARGET tinyobjloader::tinyobjloader)
    add_library(tinyobjloader::tinyobjloader ALIAS tinyobjloader)
endif()

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
    volk
    vma_config
    EnTT::EnTT
    stb::stb
    nlohmann_json::nlohmann_json
    glm::glm
    tinyobjloader::tinyobjloader
)
if(TARGET SDL3-shared)
    target_link_libraries(snt_third_party INTERFACE SDL3-shared)
endif()
