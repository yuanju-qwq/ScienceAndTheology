// CommandContext implementation.

#include "render_backend/command_context.h"

#include "render_backend/vulkan_device.h"

#include <volk.h>

#include <cstdio>

namespace snt::render_backend {

CommandContext::~CommandContext() {
    reset();
}

bool CommandContext::begin_recording(VulkanDevice& device, VkCommandPool pool) {
    // If we already hold a buffer, recycle it (vkResetCommandBuffer rather
    // than free+allocate). This keeps the per-pass allocation cost at zero
    // after the first frame.
    if (command_buffer_ == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device.logical(), &alloc_info,
                                     &command_buffer_) != VK_SUCCESS) {
            std::fprintf(stderr,
                         "[snt::render_backend] CommandContext: "
                         "vkAllocateCommandBuffers failed\n");
            return false;
        }
        owns_buffer_ = true;
    }

    device_ = &device;
    command_pool_ = pool;

    // Reset to a clean state before re-recording.
    vkResetCommandBuffer(command_buffer_, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        // One-time submit is appropriate for per-frame passes recorded
        // fresh each frame by the render graph.
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
        std::fprintf(stderr,
                     "[snt::render_backend] CommandContext: "
                     "vkBeginCommandBuffer failed\n");
        return false;
    }

    recording_ = true;
    return true;
}

void CommandContext::end_recording() {
    if (!recording_) return;
    if (vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
        std::fprintf(stderr,
                     "[snt::render_backend] CommandContext: "
                     "vkEndCommandBuffer failed\n");
    }
    recording_ = false;
}

void CommandContext::reset() {
    // Free the allocated command buffer back to the pool. Safe to call
    // even if begin_recording was never called.
    if (owns_buffer_ && command_buffer_ != VK_NULL_HANDLE && device_ &&
        command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_->logical(), command_pool_, 1,
                             &command_buffer_);
    }
    command_buffer_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    device_ = nullptr;
    recording_ = false;
    owns_buffer_ = false;
}

}  // namespace snt::render_backend
