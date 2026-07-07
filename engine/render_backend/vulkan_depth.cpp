// Vulkan Depth Buffer implementation.

#include "vulkan_depth.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#include <volk.h>

#include <cstdio>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Helper: find supported depth format
// ---------------------------------------------------------------------------

VkFormat VulkanDepth::find_depth_format() {
    // Candidate formats: prefer 32-bit float depth, fallback to 24-bit.
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (auto fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device_->physical(), fmt, &props);

        if (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return fmt;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanDepth::~VulkanDepth() {
    destroy();
}

bool VulkanDepth::init(VulkanDevice& device, VulkanSwapchain& swapchain) {
    device_ = &device;
    depth_format_ = find_depth_format();
    if (depth_format_ == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "[snt::render_backend] No supported depth format found\n");
        return false;
    }
    return recreate(swapchain);
}

void VulkanDepth::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    if (depth_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_->logical(), depth_view_, nullptr);
        depth_view_ = VK_NULL_HANDLE;
    }
    if (depth_image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(device_->vma_allocator(), depth_image_, allocation_);
        depth_image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Recreate (on swapchain resize)
// ---------------------------------------------------------------------------

bool VulkanDepth::recreate(VulkanSwapchain& swapchain) {
    // Destroy old resources first.
    destroy();

    // --- Create depth image (device-local, optimal tiling) ---
    VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format_,
        .extent = {
            .width = swapchain.extent().width,
            .height = swapchain.extent().height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(device_->vma_allocator(), &image_info, &alloc_info,
                       &depth_image_, &allocation_, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vmaCreateImage (depth) failed\n");
        return false;
    }

    // --- Create depth image view ---
    VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depth_format_,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    if (vkCreateImageView(device_->logical(), &view_info, nullptr, &depth_view_)
        != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateImageView (depth) failed\n");
        return false;
    }

    std::printf("[snt::render_backend] Depth buffer created: %ux%u (format=%d)\n",
                swapchain.extent().width, swapchain.extent().height, depth_format_);
    return true;
}

}  // namespace snt::render_backend
