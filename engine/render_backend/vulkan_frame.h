// Vulkan Frame — per-frame command buffer + sync primitives + draw logic.
//
// P1.4: implements the render loop:
//   1. vkAcquireNextImageKHR (wait on image_available_ semaphore)
//   2. vkResetCommandBuffer + record (begin render pass, bind pipeline,
//      bind vertex buffer, set viewport/scissor, draw 3 vertices, end pass)
//   3. vkQueueSubmit (wait on image_available_, signal render_finished_)
//   4. vkQueuePresentKHR (wait on render_finished_)
//
// MAX_FRAMES_IN_FLIGHT = 2: allows CPU to record frame N+1 while GPU
// renders frame N. Each frame has its own command buffer + semaphores +
// fence to avoid synchronization across frames.

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanSwapchain;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanBuffer;

class VulkanFrame {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VulkanFrame() = default;
    ~VulkanFrame();

    // Non-copyable; RAII.
    VulkanFrame(const VulkanFrame&) = delete;
    VulkanFrame& operator=(const VulkanFrame&) = delete;

    // Create command pool + command buffers + semaphores + fences.
    // `swapchain_image_count` is the number of swapchain images (for
    // per-image semaphores).
    bool init(VulkanDevice& device, uint32_t swapchain_image_count);

    void destroy();

    // Draw one frame: acquire -> record -> submit -> present.
    // Returns false if the window was resized (swapchain needs recreation).
    bool draw_frame(VulkanDevice& device,
                    VulkanSwapchain& swapchain,
                    VulkanRenderPass& render_pass,
                    VulkanPipeline& pipeline,
                    VulkanBuffer& vertex_buffer,
                    uint32_t vertex_count);

private:
    VulkanDevice* device_ = nullptr;

    // Per-frame-in-flight resources.
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    // Per-frame-in-flight semaphores (current_frame_ keyed).
    // acquire uses image_available_[current_frame_]; submit waits on it.
    // submit signals render_finished_[current_frame_]; present waits on it.
    // Safe because in_flight_fences_ ensures the slot's previous submit is
    // done before we reuse its semaphores.
    VkSemaphore image_available_[kMaxFramesInFlight]{};
    VkSemaphore render_finished_[kMaxFramesInFlight]{};

    // Per-swapchain-image present fences (image_index keyed).
    // Signaled by vkQueuePresentKHR via VK_KHR_swapchain_maintenance1.
    // Fences survive swapchain recreation (unlike semaphores), so we can
    // wait on them before destroying an old swapchain.
    std::vector<VkFence> present_fences_;  // size = swapchain image count

    // Per-frame-in-flight fences (current_frame_ keyed).
    VkFence in_flight_fences_[kMaxFramesInFlight]{};

    uint32_t current_frame_ = 0;
};

}  // namespace snt::render_backend
