// RenderSystem implementation.
//
// P2.D: ECS-driven rendering via RenderGraph.
// Per-frame flow:
//   1. Query ECS for active Camera (Transform + Camera) + first mesh entity
//      (Transform + MeshRef). Build MVP matrix.
//   2. Update the per-frame UBO via VulkanDescriptor.
//   3. VulkanFrame::begin_frame() — wait fence + acquire swapchain image.
//   4. Register a "forward" pass with RenderGraph. The pass callback:
//        vkCmdBeginRenderPass / vkCmdBindPipeline / vkCmdSetViewport /
//        vkCmdSetScissor / vkCmdBindDescriptorSets / mesh.draw() /
//        vkCmdEndRenderPass
//   5. RenderGraph::execute_record_only() — records the pass callback into
//      its CommandContext.
//   6. VulkanFrame::end_frame(image_index, recorded_cb) — submit + present.
//
// The pass callback captures raw Vulkan calls. This is intentional for
// P2.D: we keep the same recording logic as the old draw_frame to avoid
// behavior changes, but it now runs through the RenderGraph's
// CommandContext rather than VulkanFrame's internal command buffer.

#include "render/render_system.h"

#include "ecs/components.h"
#include "ecs/world.h"
#include "render_backend/command_context.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_render_pass.h"
#include "render_backend/vulkan_swapchain.h"

#include <volk.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstring>

namespace snt::render {

namespace {

// Build a model matrix from a Transform component.
glm::mat4 build_model_matrix(const snt::ecs::Transform& t) {
    using namespace glm;
    mat4 m = translate(mat4(1.0f), vec3(t.position[0], t.position[1], t.position[2]));
    m = rotate(m, radians(t.rotation[2]), vec3(0, 0, 1));  // roll
    m = rotate(m, radians(t.rotation[0]), vec3(1, 0, 0));  // pitch
    m = rotate(m, radians(t.rotation[1]), vec3(0, 1, 0));  // yaw
    m = scale(m, vec3(t.scale[0], t.scale[1], t.scale[2]));
    return m;
}

// Build a view matrix from a camera Transform (position + yaw/pitch).
glm::mat4 build_view_matrix(const snt::ecs::Transform& cam) {
    using namespace glm;
    float yaw_rad = radians(cam.rotation[1]);
    float pitch_rad = radians(cam.rotation[0]);
    vec3 pos(cam.position[0], cam.position[1], cam.position[2]);
    vec3 forward(
        cos(pitch_rad) * cos(yaw_rad),
        sin(pitch_rad),
        cos(pitch_rad) * sin(yaw_rad));
    return lookAt(pos, pos + forward, vec3(0, 1, 0));
}

}  // namespace

bool RenderSystem::init_render_graph() {
    if (!device_ || !frame_) return false;
    if (graph_initialized_) return true;
    // Match VulkanFrame's frames-in-flight so each frame slot has its own
    // command buffer, avoiding pending-state conflicts across frames.
    if (!graph_.init(*device_, snt::render_backend::VulkanFrame::frames_in_flight())) {
        return false;
    }
    graph_initialized_ = true;
    return true;
}

void RenderSystem::destroy_render_graph() {
    if (graph_initialized_) {
        graph_.destroy();
        graph_initialized_ = false;
    }
}

void RenderSystem::update(snt::ecs::World& world, float /*dt*/) {
    if (!device_ || !swapchain_ || !render_pass_ || !pipeline_ ||
        !descriptor_ || !mesh_ || !frame_ || !graph_initialized_) {
        return;
    }
    if (active_camera_ == entt::null) return;

    auto& registry = world.registry();
    if (!registry.all_of<snt::ecs::Transform, snt::ecs::Camera>(active_camera_)) {
        return;
    }

    auto& cam_transform = registry.get<snt::ecs::Transform>(active_camera_);
    auto& cam_comp      = registry.get<snt::ecs::Camera>(active_camera_);

    // Build view + projection from camera.
    glm::mat4 view = build_view_matrix(cam_transform);
    glm::mat4 proj = glm::perspective(glm::radians(cam_comp.fov),
                                      cam_comp.aspect,
                                      cam_comp.near_plane,
                                      cam_comp.far_plane);
    // GLM defaults to OpenGL clip space (Y up); Vulkan uses Y down.
    proj[1][1] *= -1.0f;

    // Find the first entity with Transform + MeshRef and build its MVP.
    entt::entity mesh_entity = entt::null;
    auto view_group = registry.view<snt::ecs::Transform, snt::ecs::MeshRef>();
    for (auto e : view_group) {
        mesh_entity = e;
        break;
    }
    if (mesh_entity == entt::null) return;

    auto& mesh_transform = registry.get<snt::ecs::Transform>(mesh_entity);
    glm::mat4 model = build_model_matrix(mesh_transform);

    snt::render_backend::UniformBufferObject ubo{};
    std::memcpy(ubo.model, glm::value_ptr(model), sizeof(ubo.model));
    std::memcpy(ubo.view,  glm::value_ptr(view),  sizeof(ubo.view));
    std::memcpy(ubo.proj,  glm::value_ptr(proj),  sizeof(ubo.proj));

    // --- Acquire swapchain image ---
    uint32_t image_index = 0;
    auto acquire_result = frame_->begin_frame(*device_, *swapchain_, &image_index);
    if (acquire_result == snt::render_backend::VulkanFrame::FrameResult::kResized) {
        needs_resize_ = true;
        return;
    }
    if (acquire_result == snt::render_backend::VulkanFrame::FrameResult::kError) {
        std::fprintf(stderr, "[snt::render] begin_frame failed\n");
        return;
    }

    // Update UBO for the current frame-in-flight BEFORE recording.
    descriptor_->update_ubo(frame_->current_frame(), ubo);

    // --- Register the forward pass ---
    // Captures raw pointers by value to avoid dangling references inside
    // the std::function callback (which may be stored + deferred).
    auto* rp        = render_pass_;
    auto* pipeline  = pipeline_;
    auto* descriptor = descriptor_;
    auto* mesh      = mesh_;
    uint32_t frame_idx = frame_->current_frame();
    VkExtent2D extent  = swapchain_->extent();

    graph_.reset();
    auto* pass = graph_.add_pass("forward");
    if (!pass) {
        std::fprintf(stderr, "[snt::render] add_pass failed\n");
        return;
    }
    pass->execute = [rp, pipeline, descriptor, mesh, frame_idx, extent, image_index]
                    (snt::render_backend::CommandContext& ctx) {
        VkCommandBuffer cmd = ctx.handle();

        // Clear values: color (dark blue) + depth (1.0 = far).
        VkClearValue clear_values[2] = {};
        clear_values[0].color = {{0.0f, 0.0f, 0.2f, 1.0f}};
        clear_values[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp_begin{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = rp->handle(),
            .framebuffer = rp->framebuffers()[image_index],
            .renderArea = {
                .offset = {0, 0},
                .extent = extent,
            },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };
        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());

        VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = extent,
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDescriptorSet desc_set = descriptor->descriptor_set(frame_idx);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->layout(), 0, 1, &desc_set, 0, nullptr);

        mesh->draw(cmd);

        vkCmdEndRenderPass(cmd);
    };

    // --- Record (no submit) ---
    // Use the same frame_index as VulkanFrame's current_frame() so the
    // recorded command buffer belongs to the same frame slot whose fence
    // we will wait on next time around. This prevents resetting a command
    // buffer that is still pending on the GPU.
    uint32_t frame_index = frame_->current_frame();
    if (!graph_.execute_record_only(frame_index)) {
        std::fprintf(stderr, "[snt::render] execute_record_only failed\n");
        return;
    }

    VkCommandBuffer recorded_cb = graph_.recorded_command_buffer(frame_index, 0);
    if (recorded_cb == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[snt::render] recorded_command_buffer returned null\n");
        return;
    }

    // --- Submit + present ---
    auto end_result = frame_->end_frame(*device_, *swapchain_, image_index, recorded_cb);
    if (end_result == snt::render_backend::VulkanFrame::FrameResult::kResized) {
        needs_resize_ = true;
    } else if (end_result == snt::render_backend::VulkanFrame::FrameResult::kError) {
        std::fprintf(stderr, "[snt::render] end_frame failed\n");
    } else {
        needs_resize_ = false;
    }
}

}  // namespace snt::render
