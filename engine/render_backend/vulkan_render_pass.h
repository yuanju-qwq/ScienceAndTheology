// Vulkan Render Pass — describes the layout and format of render targets.
//
// P1.4: single color attachment (swapchain image), one subpass, no depth.
// P1.5+ will add: depth attachment, MSAA, multiple subpasses (deferred).
//
// Also owns framebuffers (one per swapchain image).

#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace snt::render_backend {

class VulkanDevice;
class VulkanSwapchain;

class VulkanRenderPass {
public:
    VulkanRenderPass() = default;
    ~VulkanRenderPass();

    // Non-copyable; RAII.
    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;

    // Create render pass + framebuffers for the given swapchain.
    bool init(VulkanDevice& device, VulkanSwapchain& swapchain);

    // Destroy render pass + framebuffers. Called automatically by destructor.
    void destroy();

    // Recreate framebuffers when swapchain is recreated.
    bool recreate_framebuffers(VulkanSwapchain& swapchain);

    VkRenderPass handle() const { return render_pass_; }
    const std::vector<VkFramebuffer>& framebuffers() const { return framebuffers_; }

private:
    VulkanDevice* device_ = nullptr;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
};

}  // namespace snt::render_backend
