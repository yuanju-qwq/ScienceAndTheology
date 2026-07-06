# Render backend: Vulkan 1.3 + VMA.
# P1.1: interface library placeholder (no sources yet).
# P1.3 will switch to STATIC and add: vulkan_device.cpp, vulkan_swapchain.cpp
# P1.4 will add: vulkan_pipeline.cpp, vulkan_command_buffers.cpp

add_library(snt_render_backend INTERFACE)
target_include_directories(snt_render_backend INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_render_backend INTERFACE snt_engine_settings snt_third_party)
