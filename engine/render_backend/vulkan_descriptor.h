// Vulkan Descriptor — descriptor set layout + pool + sets for UBO binding.
//
// P1.5: one UBO per frame-in-flight, containing the MVP matrix.
// The vertex shader reads it via layout(set=0, binding=0) uniform.
//
// Layout:
//   binding 0: uniform buffer (MVP matrix)
//
// P2+ will add: storage buffers (entity data), combined image samplers (textures).

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanBuffer;

// UBO data structure — must match the vertex shader's uniform layout.
struct UniformBufferObject {
    float model[16];      // 4x4 model matrix
    float view[16];       // 4x4 view matrix
    float proj[16];       // 4x4 projection matrix
};

class VulkanDescriptor {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VulkanDescriptor() = default;
    ~VulkanDescriptor();

    VulkanDescriptor(const VulkanDescriptor&) = delete;
    VulkanDescriptor& operator=(const VulkanDescriptor&) = delete;

    // Create descriptor set layout + pool + sets + UBO buffers.
    bool init(VulkanDevice& device);

    void destroy();

    // Update the UBO for the given frame-in-flight index.
    void update_ubo(uint32_t frame_index, const UniformBufferObject& ubo);

    VkDescriptorSetLayout layout() const { return descriptor_set_layout_; }
    VkDescriptorSet descriptor_set(uint32_t frame_index) const {
        return descriptor_sets_[frame_index];
    }

private:
    VulkanDevice* device_ = nullptr;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;  // size = kMaxFramesInFlight

    // UBO buffers (one per frame in flight), VMA-backed.
    std::vector<VulkanBuffer*> ubo_buffers_;  // owned pointers
};

}  // namespace snt::render_backend
