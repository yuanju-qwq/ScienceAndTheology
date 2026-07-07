// Vulkan Graphics Pipeline — vertex + fragment shader stages + fixed-function
// state for rendering the triangle.
//
// P1.4: hardcoded triangle vertex layout (pos2D + color3D), no uniforms.
// P1.5+ will add: descriptor sets (uniforms, textures), push constants.

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace snt::render_backend {

class VulkanDevice;
class VulkanRenderPass;
class VulkanBuffer;

// Vertex layout: 2D position + 3-component color (interleaved).
struct Vertex {
    float position[2];
    float color[3];
};

class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    // Non-copyable; RAII.
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // Create the pipeline + pipeline layout.
    // `vert_spv_path` / `frag_spv_path`: compiled SPIR-V files.
    bool init(VulkanDevice& device, VulkanRenderPass& render_pass,
              const std::string& vert_spv_path,
              const std::string& frag_spv_path);

    void destroy();

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return pipeline_layout_; }

private:
    // Load a SPIR-V file into a VkShaderModule.
    VkShaderModule create_shader_module(const std::string& path);

    VulkanDevice* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};

}  // namespace snt::render_backend
