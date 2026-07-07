// Vulkan Descriptor implementation.

#include "vulkan_descriptor.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"

#include <volk.h>

#include <cstdio>
#include <cstring>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanDescriptor::~VulkanDescriptor() {
    destroy();
}

bool VulkanDescriptor::init(VulkanDevice& device) {
    device_ = &device;

    // --- Step 1: descriptor set layout ---
    VkDescriptorSetLayoutBinding ubo_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = nullptr,
    };

    VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &ubo_binding,
    };

    if (vkCreateDescriptorSetLayout(device_->logical(), &layout_info, nullptr,
                                    &descriptor_set_layout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateDescriptorSetLayout failed\n");
        return false;
    }

    // --- Step 2: descriptor pool ---
    VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = kMaxFramesInFlight,
    };

    VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = kMaxFramesInFlight,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    if (vkCreateDescriptorPool(device_->logical(), &pool_info, nullptr,
                               &descriptor_pool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateDescriptorPool failed\n");
        return false;
    }

    // --- Step 3: allocate descriptor sets ---
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, descriptor_set_layout_);
    VkDescriptorSetAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = kMaxFramesInFlight,
        .pSetLayouts = layouts.data(),
    };

    descriptor_sets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_->logical(), &alloc_info,
                                 descriptor_sets_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkAllocateDescriptorSets failed\n");
        return false;
    }

    // --- Step 4: create UBO buffers + write descriptor sets ---
    ubo_buffers_.resize(kMaxFramesInFlight);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        ubo_buffers_[i] = new VulkanBuffer();
        if (!ubo_buffers_[i]->init(*device_, sizeof(UniformBufferObject),
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   true /* cpu_visible */)) {
            std::fprintf(stderr, "[snt::render_backend] UBO buffer init failed\n");
            return false;
        }

        // Write descriptor: point binding 0 to this UBO buffer.
        VkDescriptorBufferInfo buf_info{
            .buffer = ubo_buffers_[i]->handle(),
            .offset = 0,
            .range = sizeof(UniformBufferObject),
        };

        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets_[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buf_info,
        };

        vkUpdateDescriptorSets(device_->logical(), 1, &write, 0, nullptr);
    }

    std::printf("[snt::render_backend] Descriptor sets created (%u UBOs)\n",
                kMaxFramesInFlight);
    return true;
}

void VulkanDescriptor::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    for (auto* buf : ubo_buffers_) {
        delete buf;  // VulkanBuffer destructor calls destroy()
    }
    ubo_buffers_.clear();

    descriptor_sets_.clear();

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_->logical(), descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_->logical(), descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Update UBO
// ---------------------------------------------------------------------------

void VulkanDescriptor::update_ubo(uint32_t frame_index,
                                  const UniformBufferObject& ubo) {
    if (frame_index >= ubo_buffers_.size()) return;
    ubo_buffers_[frame_index]->write(&ubo, sizeof(ubo));
}

}  // namespace snt::render_backend
