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
#include <vector>

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
    // P2.4: init the mesh cache with the same Vulkan device.
    if (!mesh_cache_.init(*device_)) {
        std::fprintf(stderr, "[snt::render] MeshCache init failed\n");
        return false;
    }
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
    // P2.4: MeshCache owns VulkanMesh objects; release them here so
    // they are freed before the VulkanDevice they depend on.
    mesh_cache_.destroy();
}

void RenderSystem::update(snt::ecs::World& world, float /*dt*/) {
    if (!device_ || !swapchain_ || !render_pass_ || !pipeline_ ||
        !descriptor_ || !frame_ || !graph_initialized_) {
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

    // --- Collect mesh entities (single ECS pass) ---
    // P2.4: iterate ALL entities with Transform + MeshRef. For each:
    //   - resolve mesh handle via MeshCache
    //   - precompute model matrix (view/proj are per-frame, same for all)
    //   - stash a MeshDraw entry; UBO write happens AFTER begin_frame
    //     (which fence-waits + selects the frame-in-flight slot).
    struct MeshDraw {
        snt::render_backend::VulkanMesh* mesh;
        uint32_t ubo_offset;            // dynamic offset for this entity's MVP slot
        snt::render_backend::UniformBufferObject ubo;  // precomputed MVP
    };
    std::vector<MeshDraw> draws;
    draws.reserve(32);

    uint32_t entity_index = 0;
    auto view_group = registry.view<snt::ecs::Transform, snt::ecs::MeshRef>();
    for (auto e : view_group) {
        if (entity_index >= snt::render_backend::VulkanDescriptor::kMaxEntities) {
            std::fprintf(stderr, "[snt::render] too many mesh entities, truncating\n");
            break;
        }

        auto& transform = registry.get<snt::ecs::Transform>(e);
        auto& mesh_ref  = registry.get<snt::ecs::MeshRef>(e);

        // Resolve the mesh handle to a VulkanMesh via the cache.
        auto* mesh = mesh_cache_.get(mesh_ref.handle.id);
        if (!mesh) {
            std::fprintf(stderr, "[snt::render] entity %u: invalid mesh handle\n",
                         static_cast<unsigned>(e));
            continue;
        }

        // Precompute MVP. view/proj are constant for this frame; only
        // model varies per entity.
        glm::mat4 model = build_model_matrix(transform);
        snt::render_backend::UniformBufferObject ubo{};
        std::memcpy(ubo.model, glm::value_ptr(model), sizeof(ubo.model));
        std::memcpy(ubo.view,  glm::value_ptr(view),  sizeof(ubo.view));
        std::memcpy(ubo.proj,  glm::value_ptr(proj),  sizeof(ubo.proj));

        draws.push_back({mesh, entity_index * descriptor_->ubo_stride(), ubo});
        ++entity_index;
    }

    if (draws.empty()) return;

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

    // Now that we know the frame-in-flight slot, write each entity's MVP
    // into its slot in the dynamic UBO.
    uint32_t frame_idx = frame_->current_frame();
    for (uint32_t i = 0; i < draws.size(); ++i) {
        descriptor_->update_ubo(frame_idx, i, draws[i].ubo);
    }

    // --- Register the forward pass (P2.4: per-entity bind+draw inside) ---
    auto* rp        = render_pass_;
    auto* pipeline  = pipeline_;
    auto* descriptor = descriptor_;
    VkExtent2D extent  = swapchain_->extent();

    graph_.reset();
    auto* pass = graph_.add_pass("forward");
    if (!pass) {
        std::fprintf(stderr, "[snt::render] add_pass failed\n");
        return;
    }
    // Capture draws by value (vector copy) — the callback runs synchronously
    // inside execute_record_only, so this is safe.
    pass->execute = [rp, pipeline, descriptor, frame_idx, extent, image_index, draws]
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

        // Draw each mesh entity with its own dynamic UBO offset.
        VkDescriptorSet desc_set = descriptor->descriptor_set(frame_idx);
        for (const auto& d : draws) {
            uint32_t dyn_offset = d.ubo_offset;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline->layout(), 0, 1, &desc_set,
                                    1, &dyn_offset);
            d.mesh->draw(cmd);
        }

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

    // Collect all recorded command buffers (one per pass) + submit them
    // together in one vkQueueSubmit. P2.3.4's topological sort guarantees
    // they are in dependency order.
    std::vector<VkCommandBuffer> recorded_cbs;
    uint32_t cb_count = graph_.recorded_command_buffers(frame_index, &recorded_cbs);
    if (cb_count == 0) {
        std::fprintf(stderr, "[snt::render] no recorded command buffers\n");
        return;
    }

    // --- Submit + present ---
    auto end_result = frame_->end_frame(*device_, *swapchain_, image_index,
                                        recorded_cbs.data(), cb_count);
    if (end_result == snt::render_backend::VulkanFrame::FrameResult::kResized) {
        needs_resize_ = true;
    } else if (end_result == snt::render_backend::VulkanFrame::FrameResult::kError) {
        std::fprintf(stderr, "[snt::render] end_frame failed\n");
    } else {
        needs_resize_ = false;
    }
}

}  // namespace snt::render
