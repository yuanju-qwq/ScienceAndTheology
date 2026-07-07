// RenderSystem: ECS system that drives rendering from entity data.
//
// P2.D scope:
//   - Iterates View<Transform, MeshRef> to find the mesh entity.
//   - Reads active Camera entity to build view/proj.
//   - Registers a "forward" pass with RenderGraph; the pass callback
//     issues the actual vkCmd* calls (begin render pass, bind pipeline,
//     bind descriptor set, draw mesh, end render pass).
//   - Calls RenderGraph::execute_record_only() to record into a
//     CommandContext, then hands the recorded command buffer to
//     VulkanFrame::end_frame() for submit + present.
//
// Layering: RenderSystem owns no Vulkan sync primitives. VulkanFrame owns
// fences/semaphores/acquire/present; RenderGraph owns command buffers +
// recording; RenderSystem owns the "what to draw" decision (ECS query).

#pragma once

#include "ecs/system.h"
#include "renderer/render_graph.h"
#include "render_backend/vulkan_descriptor.h"

#include <entt/entt.hpp>

namespace snt::render_backend {
class VulkanDevice;
class VulkanSwapchain;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanDescriptor;
class VulkanMesh;
class VulkanFrame;
}

namespace snt::render {

class RenderSystem : public snt::ecs::System {
public:
    RenderSystem() = default;
    ~RenderSystem() override = default;

    // Wire up rendering dependencies. All pointers must outlive RenderSystem.
    void set_device(snt::render_backend::VulkanDevice* p)         { device_ = p; }
    void set_swapchain(snt::render_backend::VulkanSwapchain* p)   { swapchain_ = p; }
    void set_render_pass(snt::render_backend::VulkanRenderPass* p){ render_pass_ = p; }
    void set_pipeline(snt::render_backend::VulkanPipeline* p)     { pipeline_ = p; }
    void set_descriptor(snt::render_backend::VulkanDescriptor* p) { descriptor_ = p; }
    void set_mesh(snt::render_backend::VulkanMesh* p)             { mesh_ = p; }
    void set_frame(snt::render_backend::VulkanFrame* p)           { frame_ = p; }

    // Set the entity to use as the active camera.
    void set_active_camera(entt::entity e) { active_camera_ = e; }

    // Initialize the RenderGraph (creates its command pool). Must be called
    // after set_device() and before update().
    bool init_render_graph();

    // Release RenderGraph resources.
    void destroy_render_graph();

    // ECS update: build MVP, register forward pass, record + submit.
    void update(snt::ecs::World& world, float dt) override;

    // Returns true if the last frame signaled swapchain-out-of-date.
    bool needs_resize() const { return needs_resize_; }

private:
    snt::render_backend::VulkanDevice*     device_      = nullptr;
    snt::render_backend::VulkanSwapchain*  swapchain_   = nullptr;
    snt::render_backend::VulkanRenderPass* render_pass_ = nullptr;
    snt::render_backend::VulkanPipeline*   pipeline_    = nullptr;
    snt::render_backend::VulkanDescriptor* descriptor_  = nullptr;
    snt::render_backend::VulkanMesh*       mesh_        = nullptr;
    snt::render_backend::VulkanFrame*      frame_       = nullptr;
    entt::entity active_camera_ = entt::null;

    snt::renderer::RenderGraph graph_;
    bool graph_initialized_ = false;

    bool needs_resize_ = false;
};

}  // namespace snt::render
