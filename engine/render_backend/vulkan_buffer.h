// Vulkan Buffer — VMA-backed generic buffer for vertex/index/uniform data.
//
// Wraps VkBuffer + VMA allocation. Provides:
//   - create(): allocate buffer with given size + usage + memory flags
//   - map() / unmap(): CPU write access (for staging or direct upload)
//   - destroy(): release buffer + allocation
//
// P1.4: used for vertex buffer (triangle vertices).
// P1.5+: will be reused for index buffer, uniform buffer, staging buffer.

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace snt::render_backend {

class VulkanDevice;

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer();

    // Non-copyable; RAII.
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    // Create a buffer of `size` bytes with `usage` flags.
    // `cpu_visible` = true: host-visible + host-coherent (for direct CPU writes).
    // `cpu_visible` = false: device-local (GPU-only, faster; needs staging).
    bool init(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage,
              bool cpu_visible);

    void destroy();

    // Map the whole buffer to CPU memory. Returns pointer for writing.
    void* map();

    // Unmap the buffer.
    void unmap();

    // Copy `data_size` bytes from `data` into the mapped buffer.
    // Convenience wrapper for map() + memcpy + unmap().
    void write(const void* data, VkDeviceSize data_size);

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    bool is_valid() const { return buffer_ != VK_NULL_HANDLE; }

private:
    VulkanDevice* device_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
};

}  // namespace snt::render_backend
