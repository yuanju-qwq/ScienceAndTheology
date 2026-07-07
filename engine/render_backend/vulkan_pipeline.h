// Vulkan Graphics Pipeline — mesh vertex + fragment shader + depth state.
//
// P1.5: 3D mesh rendering with MVP UBO + depth testing.
// P1.4 was depth-less; P1.5 adds depth + descriptor set layout.

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace snt::render_backend {

class VulkanDevice;
class VulkanRenderPass;
class VulkanDescriptor;
class VulkanBuffer;

// Mesh vertex layout: 3D position + 3-component color (interleaved).
struct MeshVertex;

class VulkanPipeline {
public:
    VulkanPipeline() = default;
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // Create the pipeline + pipeline layout with descriptor set layout.
    bool init(VulkanDevice& device, VulkanRenderPass& render_pass,
              VulkanDescriptor& descriptor,
              const std::string& vert_spv_path,
              const std::string& frag_spv_path);

    void destroy();

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return pipeline_layout_; }

private:
    VkShaderModule create_shader_module(const std::string& path);

    VulkanDevice* device_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};

}  // namespace snt::render_backend
