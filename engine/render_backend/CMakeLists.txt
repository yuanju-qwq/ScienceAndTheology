# Render backend: Vulkan 1.3 + VMA.
# P1.4: full rendering pipeline (render pass, pipeline, buffer, frame).

add_library(snt_render_backend STATIC
    vma_impl.cpp
    vulkan_instance.cpp
    vulkan_device.cpp
    vulkan_swapchain.cpp
    vulkan_render_pass.cpp
    vulkan_pipeline.cpp
    vulkan_buffer.cpp
    vulkan_frame.cpp
)
target_include_directories(snt_render_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# VMA include path: FetchContent puts source in _deps/vulkanmemoryallocator-src/.
# PUBLIC because vulkan_buffer.h (included by engine_app) includes <vk_mem_alloc.h>.
target_include_directories(snt_render_backend PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/vulkanmemoryallocator-src/include
)
target_link_libraries(snt_render_backend PUBLIC snt_engine_settings snt_third_party)

# ============================================================
# Shader compilation: GLSL -> SPIR-V via glslangValidator
# ============================================================
# Look for glslangValidator (ships with Vulkan SDK or standalone).
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "D:/vulkansdk/Bin"
        "C:/VulkanSDK"
)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Compile each .vert/.frag shader into a .spv file.
set(SHADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/triangle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/triangle.frag
)

set(SHADER_SPV_FILES "")
foreach(SHADER ${SHADERS})
    # Derive output path: foo/bar/baz.vert -> build/shaders/baz.vert.spv
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})

    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling GLSL shader: ${SHADER_NAME}"
        )
    else()
        message(FATAL_ERROR "glslangValidator not found. "
            "Install Vulkan SDK or set VULKAN_SDK env var.")
    endif()
endforeach()

# Custom target so other targets can depend on shaders being compiled.
add_custom_target(snt_shaders ALL DEPENDS ${SHADER_SPV_FILES})

# Copy shaders to the engine_app output directory after build.
# Uses a custom target with DEPENDS so it runs before the engine app links.
add_custom_target(snt_copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SHADER_OUTPUT_DIR}
    ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders
    COMMENT "Copying shaders to output directory"
    DEPENDS snt_shaders
)
