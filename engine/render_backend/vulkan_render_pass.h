// Vulkan Render Pass — describes the layout and format of render targets.
//
// P1.5: color attachment (swapchain image) + depth attachment, one subpass.
// P1.4 was color-only; P1.5 adds depth testing for 3D mesh rendering.

#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanSwapchain;
class VulkanDepth;

class VulkanRenderPass {
public:
    VulkanRenderPass() = default;
    ~VulkanRenderPass();

    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    // Create render pass + framebuffers for the given swapchain + depth buffer.
    bool init(VulkanDevice& device, VulkanSwapchain& swapchain, VulkanDepth& depth);

    void destroy();

    // Recreate framebuffers when swapchain/depth is recreated.
    bool recreate_framebuffers(VulkanSwapchain& swapchain, VulkanDepth& depth);

    VkRenderPass handle() const { return render_pass_; }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }

private:
    VulkanDevice* device_ = nullptr;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
};

}  // namespace snt::render_backend
