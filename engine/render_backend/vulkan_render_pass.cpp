// Vulkan Render Pass implementation.

#include "vulkan_render_pass.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#include <volk.h>

#include <cstdio>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanRenderPass::~VulkanRenderPass() {
    destroy();
}

bool VulkanRenderPass::init(VulkanDevice& device, VulkanSwapchain& swapchain) {
    device_ = &device;

    // --- Create render pass ---
    // Single color attachment, cleared at start, stored at end.
    VkAttachmentDescription color_attachment{
        .format = swapchain.image_format(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,    // clear to clearColor
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,  // keep for presentation
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };

    // Subpass dependency: ensure the color attachment is ready before the
    // fragment shader writes to it.
    // Note: field order matches VkSubpassDependency declaration order.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    if (vkCreateRenderPass(device_->logical(), &create_info, nullptr,
                           &render_pass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateRenderPass failed\n");
        return false;
    }

    std::printf("[snt::render_backend] Render pass created\n");

    // --- Create framebuffers ---
    return recreate_framebuffers(swapchain);
}

void VulkanRenderPass::destroy() {
    if (!device_ || device_->logical() == VK_NULL_HANDLE) return;

    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->logical(), fb, nullptr);
        }
    }
    framebuffers_.clear();

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_->logical(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Recreate framebuffers (on swapchain recreation)
// ---------------------------------------------------------------------------

bool VulkanRenderPass::recreate_framebuffers(VulkanSwapchain& swapchain) {
    // Destroy old framebuffers first.
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->logical(), fb, nullptr);
        }
    }
    framebuffers_.clear();

    const auto& image_views = swapchain.image_views();
    framebuffers_.reserve(image_views.size());

    for (auto view : image_views) {
        VkFramebufferCreateInfo fb_info{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass_,
            .attachmentCount = 1,
            .pAttachments = &view,
            .width = swapchain.extent().width,
            .height = swapchain.extent().height,
            .layers = 1,
        };

        VkFramebuffer fb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device_->logical(), &fb_info, nullptr, &fb)
            != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] vkCreateFramebuffer failed\n");
            return false;
        }
        framebuffers_.push_back(fb);
    }

    std::printf("[snt::render_backend] %zu framebuffer(s) created\n",
                framebuffers_.size());
    return true;
}

}  // namespace snt::render_backend
