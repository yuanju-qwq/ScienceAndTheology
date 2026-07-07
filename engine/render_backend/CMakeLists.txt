# Render backend: Vulkan 1.3 + VMA.
# P1.3: Vulkan instance + device + swapchain (via Volk for dynamic loading).
# P1.4 will add: render pass, pipeline, command buffers, triangle rendering.

add_library(snt_render_backend STATIC
    vulkan_instance.cpp
    vulkan_device.cpp
    vulkan_swapchain.cpp
)
target_include_directories(snt_render_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_render_backend PUBLIC snt_engine_settings snt_third_party)
