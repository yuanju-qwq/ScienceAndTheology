// Vulkan Frame implementation.

#include "vulkan_frame.h"
#include "vulkan_buffer.h"
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "vulkan_render_pass.h"
#include "vulkan_swapchain.h"

#include <volk.h>

#include <cstdio>

// VK_KHR_swapchain_maintenance1 extension name macro may be missing from
// older Vulkan headers, even when the struct is defined. Define it manually.
#ifndef VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
#define VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME "VK_KHR_swapchain_maintenance1"
#endif

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanFrame::~VulkanFrame() {
    destroy();
}

bool VulkanFrame::init(VulkanDevice& device, uint32_t swapchain_image_count) {
    device_ = &device;

    // --- Create command pool ---
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.graphics_family(),
    };

    if (vkCreateCommandPool(device_->logical(), &pool_info, nullptr,
                            &command_pool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateCommandPool failed\n");
        return false;
    }

    // --- Allocate command buffers (one per frame in flight) ---
    command_buffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(command_buffers_.size()),
    };

    if (vkAllocateCommandBuffers(device_->logical(), &alloc_info,
                                 command_buffers_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkAllocateCommandBuffers failed\n");
        return false;
    }

    // --- Create per-frame-in-flight semaphores ---
    // acquire uses image_available_[current_frame_]; submit waits on it.
    // submit signals render_finished_[current_frame_]; present waits on it.
    // Safe because in_flight_fences_ ensures the slot's previous submit is
    // done (and thus its semaphores are unsignaled) before reuse.
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkSemaphoreCreateInfo sem_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        if (vkCreateSemaphore(device_->logical(), &sem_info, nullptr,
                              &image_available_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_->logical(), &sem_info, nullptr,
                              &render_finished_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] Failed to create semaphores\n");
            return false;
        }
    }

    // --- Create per-swapchain-image present fences ---
    // Signaled by vkQueuePresentKHR via VK_KHR_swapchain_maintenance1.
    // These survive swapchain recreation and let us wait for present
    // completion before destroying an old swapchain.
    present_fences_.resize(swapchain_image_count);
    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // start signaled
        };
        if (vkCreateFence(device_->logical(), &fence_info, nullptr,
                          &present_fences_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] Failed to create present fences\n");
            return false;
        }
    }

    // --- Create per-frame-in-flight fences ---
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // start signaled so first
                                                    // vkWaitForFences doesn't hang
        };
        if (vkCreateFence(device_->logical(), &fence_info, nullptr,
                          &in_flight_fences_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] Failed to create fences\n");
            return false;
        }
    }

    std::printf("[snt::render_backend] Frame resources created "
                "(%u frames in flight, %u swapchain images)\n",
                kMaxFramesInFlight, swapchain_image_count);
    return true;
}

void VulkanFrame::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (in_flight_fences_[i]) {
            vkDestroyFence(device_->logical(), in_flight_fences_[i], nullptr);
            in_flight_fences_[i] = VK_NULL_HANDLE;
        }
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (image_available_[i]) vkDestroySemaphore(device_->logical(), image_available_[i], nullptr);
        if (render_finished_[i]) vkDestroySemaphore(device_->logical(), render_finished_[i], nullptr);
        image_available_[i] = VK_NULL_HANDLE;
        render_finished_[i] = VK_NULL_HANDLE;
    }

    for (auto fence : present_fences_) {
        if (fence) vkDestroyFence(device_->logical(), fence, nullptr);
    }
    present_fences_.clear();

    command_buffers_.clear();

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->logical(), command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Draw one frame
// ---------------------------------------------------------------------------

bool VulkanFrame::draw_frame(VulkanDevice& device,
                             VulkanSwapchain& swapchain,
                             VulkanRenderPass& render_pass,
                             VulkanPipeline& pipeline,
                             VulkanBuffer& vertex_buffer,
                             uint32_t vertex_count) {
    // --- Step 1: wait for previous frame using this slot ---
    vkWaitForFences(device.logical(), 1, &in_flight_fences_[current_frame_],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(device.logical(), 1, &in_flight_fences_[current_frame_]);

    // --- Step 2: acquire next swapchain image ---
    // Use per-frame-in-flight semaphore: safe because we just waited on
    // in_flight_fences_[current_frame_], ensuring the previous submit on
    // this slot is done and its acquire semaphore was consumed.
    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(
        device.logical(), swapchain.handle(), UINT64_MAX,
        image_available_[current_frame_], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;  // swapchain needs recreation
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[snt::render_backend] vkAcquireNextImageKHR failed: %d\n",
                     result);
        return false;
    }

    // --- Step 3: record command buffer ---
    VkCommandBuffer cmd = command_buffers_[current_frame_];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkBeginCommandBuffer failed\n");
        return false;
    }

    // Clear color: dark blue background.
    VkClearValue clear_color = {{{0.0f, 0.0f, 0.2f, 1.0f}}};

    VkRenderPassBeginInfo rp_begin{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass.handle(),
        .framebuffer = render_pass.framebuffers()[image_index],
        .renderArea = {
            .offset = {0, 0},
            .extent = swapchain.extent(),
        },
        .clearValueCount = 1,
        .pClearValues = &clear_color,
    };

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

    // Set viewport (dynamic state).
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapchain.extent().width),
        .height = static_cast<float>(swapchain.extent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Set scissor (dynamic state).
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = swapchain.extent(),
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind vertex buffer.
    VkBuffer vertex_buffers[] = {vertex_buffer.handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);

    // Draw the triangle (3 vertices).
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkEndCommandBuffer failed\n");
        return false;
    }

    // --- Step 4: submit command buffer ---
    // Wait on this frame's acquire semaphore; signal this frame's render semaphore.
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &image_available_[current_frame_],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished_[current_frame_],
    };

    if (vkQueueSubmit(device.graphics_queue(), 1, &submit_info,
                      in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkQueueSubmit failed\n");
        return false;
    }

    // --- Step 5: present ---
    // Wait on this frame's render semaphore; signal per-image present fence
    // via VK_KHR_swapchain_maintenance1.
    VkSwapchainPresentFenceInfoEXT present_fence_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1,
        .pFences = &present_fences_[image_index],
    };

    VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = &present_fence_info,  // chain: signal present fence
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished_[current_frame_],
        .swapchainCount = 1,
        .pSwapchains = &(const VkSwapchainKHR&)swapchain.handle(),
        .pImageIndices = &image_index,
    };

    result = vkQueuePresentKHR(device.present_queue(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;  // swapchain needs recreation
    }

    // Advance to next frame slot.
    current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
    return true;
}

}  // namespace snt::render_backend
