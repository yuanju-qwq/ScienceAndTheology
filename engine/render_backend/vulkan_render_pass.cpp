// Vulkan Render Pass implementation.

#include "vulkan_render_pass.h"
#include "vulkan_depth.h"
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

bool VulkanRenderPass::init(VulkanDevice& device, VulkanSwapchain& swapchain,
                            VulkanDepth& depth) {
    device_ = &device;

    // --- Create render pass with color + depth attachments ---
    VkAttachmentDescription attachments[2] = {};

    // Attachment 0: color (swapchain image).
    attachments[0].format = swapchain.image_format();
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Attachment 1: depth.
    attachments[1].format = depth.format();
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // don't need depth after
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref{
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    // Subpass dependency: ensure color + depth are ready before fragment writes.
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 2;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_->logical(), &create_info, nullptr,
                           &render_pass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateRenderPass failed\n");
        return false;
    }

    std::printf("[snt::render_backend] Render pass created (color + depth)\n");

    return recreate_framebuffers(swapchain, depth);
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

bool VulkanRenderPass::recreate_framebuffers(VulkanSwapchain& swapchain,
                                             VulkanDepth& depth) {
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->logical(), fb, nullptr);
        }
    }
    framebuffers_.clear();

    const auto& image_views = swapchain.image_views();
    framebuffers_.reserve(image_views.size());

    for (auto view : image_views) {
        VkImageView attachments[] = {view, depth.view()};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass_;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments = attachments;
        fb_info.width = swapchain.extent().width;
        fb_info.height = swapchain.extent().height;
        fb_info.layers = 1;

        VkFramebuffer fb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device_->logical(), &fb_info, nullptr, &fb)
            != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] vkCreateFramebuffer failed\n");
            return false;
        }
        framebuffers_.push_back(fb);
    }

    std::printf("[snt::render_backend] %zu framebuffer(s) created (color + depth)\n",
                framebuffers_.size());
    return true;
}

}  // namespace snt::render_backend
