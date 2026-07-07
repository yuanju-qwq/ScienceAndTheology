// Engine implementation.
//
// P2.B1: moves the per-frame loop + resource setup from main.cpp into
// the Engine class. Logic is preserved 1:1 from P1.5 + P2.A1 (input
// decoupling); the only structural change is ownership moves from local
// variables in main() into Engine::Impl.

#include "engine/engine.h"

#include "core/job_system.h"
#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "input/input_system.h"
#include "platform/window.h"
#include "render/render_system.h"
#include "render_backend/vulkan_depth.h"
#include "render_backend/vulkan_descriptor.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_instance.h"
#include "render_backend/vulkan_mesh.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_render_pass.h"
#include "render_backend/vulkan_swapchain.h"
#include "ui/debug_panel.h"
#include "ui/mui.h"

#include <volk.h>

#include <chrono>
#include <cstdio>

namespace snt::engine {

// ---------------------------------------------------------------------------
// FPS tracker: rolling window of frame times for averaging + plotting.
// Kept file-local (not in engine.h) since it is an implementation detail
// of Engine's frame loop.
// ---------------------------------------------------------------------------
namespace {

using Clock = std::chrono::high_resolution_clock;
using DurationMs = std::chrono::duration<float, std::milli>;

struct FpsTracker {
    static constexpr int kSamples = 120;
    float frame_times_ms[kSamples] = {};
    int offset = 0;
    float last_frame_ms = 0.0f;

    void tick(float ms) {
        frame_times_ms[offset] = ms;
        offset = (offset + 1) % kSamples;
        last_frame_ms = ms;
    }

    float fps() const {
        float sum = 0.0f;
        int n = kSamples < 60 ? kSamples : 60;
        for (int i = 0; i < n; ++i) {
            int idx = (offset - 1 - i + kSamples) % kSamples;
            sum += frame_times_ms[idx];
        }
        float avg_ms = sum / n;
        return avg_ms > 0.0f ? 1000.0f / avg_ms : 0.0f;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Impl: holds all subsystems. Members are pointer-free owned objects;
// order of declaration controls destruction order (reverse of init).
// ---------------------------------------------------------------------------
struct Engine::Impl {
    // Platform + input.
    snt::platform::Window window;
    snt::input::InputSystem input_system;

    // Vulkan backend (raw stack objects — initialized in init()).
    snt::render_backend::VulkanInstance       vk_instance;
    VkSurfaceKHR                              vk_surface = VK_NULL_HANDLE;
    snt::render_backend::VulkanDevice         vk_device;
    snt::render_backend::VulkanSwapchain      vk_swapchain;
    snt::render_backend::VulkanDepth          vk_depth;
    snt::render_backend::VulkanRenderPass     vk_render_pass;
    snt::render_backend::VulkanDescriptor     vk_descriptor;
    snt::render_backend::VulkanPipeline       vk_pipeline;
    snt::render_backend::VulkanMesh           vk_mesh;
    snt::render_backend::VulkanFrame          vk_frame;

    // ECS.
    snt::ecs::World world;
    entt::entity camera_entity = entt::null;
    entt::entity cube_entity   = entt::null;

    // Render system (ECS-driven). Holds raw pointers to the vk_* objects
    // above; set in init() after the vk_* objects are created.
    snt::render::RenderSystem render_system;

    // Per-frame state.
    FpsTracker fps_tracker;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() { shutdown(); }

bool Engine::init() {
    using namespace snt::platform;
    using namespace snt::render_backend;
    using namespace snt::ecs;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P2.B1)\n");

    // --- Window ---
    if (!impl_->window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P2.B1)",
            .width = 1280,
            .height = 720,
            .resizable = true,
            .vulkan_enabled = true,
        })) {
        std::fprintf(stderr, "[snt_engine] Failed to create window\n");
        return false;
    }
    const auto sz = impl_->window.size();
    std::printf("[snt_engine] Window created: %dx%d\n", sz.width, sz.height);

    // --- Input system: Window forwards SDL events via callback ---
    impl_->window.set_event_callback(
        [this](const void* sdl_event) {
            impl_->input_system.process_event(sdl_event);
        });

    // --- Vulkan instance ---
    if (!impl_->vk_instance.init(impl_->window)) {
        std::fprintf(stderr, "[snt_engine] VulkanInstance init failed\n");
        return false;
    }

    // --- Vulkan surface (created via platform layer) ---
    uint64_t surface_u64 = 0;
    if (!impl_->window.create_vulkan_surface(
            reinterpret_cast<void*>(impl_->vk_instance.handle()), &surface_u64)) {
        std::fprintf(stderr, "[snt_engine] create_vulkan_surface failed\n");
        return false;
    }
    impl_->vk_surface = reinterpret_cast<VkSurfaceKHR>(surface_u64);

    // --- Device + swapchain + depth + render pass ---
    if (!impl_->vk_device.init(impl_->vk_instance.handle(), impl_->vk_surface)) {
        std::fprintf(stderr, "[snt_engine] VulkanDevice init failed\n");
        return false;
    }
    if (!impl_->vk_swapchain.init(impl_->vk_device,
                                  static_cast<uint32_t>(sz.width),
                                  static_cast<uint32_t>(sz.height))) {
        std::fprintf(stderr, "[snt_engine] VulkanSwapchain init failed\n");
        return false;
    }
    if (!impl_->vk_depth.init(impl_->vk_device, impl_->vk_swapchain)) {
        std::fprintf(stderr, "[snt_engine] VulkanDepth init failed\n");
        return false;
    }
    if (!impl_->vk_render_pass.init(impl_->vk_device, impl_->vk_swapchain, impl_->vk_depth)) {
        std::fprintf(stderr, "[snt_engine] VulkanRenderPass init failed\n");
        return false;
    }

    // --- Descriptor + pipeline + mesh ---
    if (!impl_->vk_descriptor.init(impl_->vk_device)) {
        std::fprintf(stderr, "[snt_engine] VulkanDescriptor init failed\n");
        return false;
    }
    if (!impl_->vk_pipeline.init(impl_->vk_device, impl_->vk_render_pass, impl_->vk_descriptor,
                                 "shaders/mesh.vert.spv", "shaders/mesh.frag.spv")) {
        std::fprintf(stderr, "[snt_engine] VulkanPipeline init failed\n");
        return false;
    }
    float cube_color[3] = {0.8f, 0.6f, 0.2f};  // orange
    if (!impl_->vk_mesh.load_obj(impl_->vk_device, "assets/cube.obj", cube_color)) {
        std::fprintf(stderr, "[snt_engine] Mesh load failed\n");
        return false;
    }

    // --- Frame resources ---
    if (!impl_->vk_frame.init(impl_->vk_device,
                              static_cast<uint32_t>(impl_->vk_swapchain.image_views().size()))) {
        std::fprintf(stderr, "[snt_engine] VulkanFrame init failed\n");
        return false;
    }

    // --- ECS: camera entity + cube entity ---
    // NOTE: do NOT cache component references — EnTT may reallocate its
    // internal sparse set storage on subsequent add_component() calls.
    impl_->camera_entity = impl_->world.create_entity();
    {
        auto& cam_transform = impl_->world.add_component<Transform>(impl_->camera_entity);
        cam_transform.position[2] = 3.0f;  // camera on +Z, looks toward -Z
    }
    {
        auto& cam_comp = impl_->world.add_component<Camera>(impl_->camera_entity);
        cam_comp.aspect = static_cast<float>(sz.width) / static_cast<float>(sz.height);
    }

    impl_->cube_entity = impl_->world.create_entity();
    {
        auto& cube_transform = impl_->world.add_component<Transform>(impl_->cube_entity);
        cube_transform.rotation[0] = -25.0f;  // pitch down
        cube_transform.rotation[1] = 35.0f;   // yaw right
    }
    impl_->world.add_component<MeshRef>(impl_->cube_entity, MeshRef{"assets/cube.obj"});

    // --- Camera system ---
    auto& camera_system = impl_->world.add_system<CameraSystem>();
    camera_system.set_input(&impl_->input_system);
    camera_system.set_active_camera(impl_->camera_entity);

    // --- Render system (P2.D) ---
    // ECS-driven rendering via RenderGraph. RenderSystem owns its own
    // RenderGraph instance; Engine wires up the backend dependencies.
    impl_->render_system.set_device(&impl_->vk_device);
    impl_->render_system.set_swapchain(&impl_->vk_swapchain);
    impl_->render_system.set_render_pass(&impl_->vk_render_pass);
    impl_->render_system.set_pipeline(&impl_->vk_pipeline);
    impl_->render_system.set_descriptor(&impl_->vk_descriptor);
    impl_->render_system.set_mesh(&impl_->vk_mesh);
    impl_->render_system.set_frame(&impl_->vk_frame);
    impl_->render_system.set_active_camera(impl_->camera_entity);
    if (!impl_->render_system.init_render_graph()) {
        std::fprintf(stderr, "[snt_engine] RenderSystem::init_render_graph failed\n");
        return false;
    }

    // --- Debug panel metrics ---
    auto& panel = snt::ui::default_debug_panel();
    panel.register_metric("FPS",
                          [this]() { return impl_->fps_tracker.fps(); });
    panel.register_metric("Frame Time (ms)",
                          [this]() { return impl_->fps_tracker.last_frame_ms; });
    panel.register_metric("Job Workers", []() {
        return static_cast<float>(snt::core::default_job_system().worker_count());
    });

    std::printf("[snt_engine] Controls: WASD=move, QE=up/down, "
                "Right-drag=look, Shift=boost\n");
    return true;
}

void Engine::run() {
    using namespace snt::render_backend;

    auto last_time = Clock::now();
    while (impl_->window.poll_events()) {
        auto now = Clock::now();
        float frame_ms = DurationMs(now - last_time).count();
        float dt = frame_ms / 1000.0f;
        last_time = now;
        impl_->fps_tracker.tick(frame_ms);

        // Finalize input for this frame, then let ECS read it.
        impl_->input_system.end_frame();
        impl_->world.update(dt);
        impl_->input_system.new_frame();

        // Sample metrics + draw debug panel (P1 stub: no visible output).
        auto& panel = snt::ui::default_debug_panel();
        panel.sample();
        snt::ui::MuiContext& mui = snt::ui::default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

        // Render: RenderSystem reads ECS state and draws via VulkanFrame.
        impl_->render_system.update(impl_->world, dt);

        // Handle resize: RenderSystem sets needs_resize_ when draw_frame
        // signals swapchain-out-of-date.
        if (impl_->render_system.needs_resize()) {
            impl_->vk_device.wait_idle();
            const auto new_sz = impl_->window.size();
            if (impl_->vk_swapchain.recreate(static_cast<uint32_t>(new_sz.width),
                                             static_cast<uint32_t>(new_sz.height))) {
                impl_->vk_depth.recreate(impl_->vk_swapchain);
                impl_->vk_render_pass.recreate_framebuffers(impl_->vk_swapchain, impl_->vk_depth);
                auto& cam_comp_resize = impl_->world.get_component<snt::ecs::Camera>(impl_->camera_entity);
                cam_comp_resize.aspect = static_cast<float>(new_sz.width) /
                                         static_cast<float>(new_sz.height);
                std::printf("[snt_engine] Swapchain recreated: %dx%d\n",
                            new_sz.width, new_sz.height);
            }
        }
    }

    // Drain GPU before teardown.
    impl_->vk_device.wait_idle();
}

void Engine::shutdown() {
    if (!impl_) return;
    // vk_device.wait_idle() already called at end of run(); call again
    // in case shutdown() is invoked without run() returning normally.
    impl_->vk_device.wait_idle();

    // Render system (releases its RenderGraph).
    impl_->render_system.destroy_render_graph();

    // Frame + mesh + pipeline + descriptor.
    impl_->vk_frame.destroy();
    impl_->vk_mesh.destroy();
    impl_->vk_pipeline.destroy();
    impl_->vk_descriptor.destroy();

    // Render pass + depth + swapchain.
    impl_->vk_render_pass.destroy();
    impl_->vk_depth.destroy();
    impl_->vk_swapchain.destroy();

    // Device + surface + instance.
    impl_->vk_device.destroy();
    if (impl_->vk_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->vk_instance.handle(), impl_->vk_surface, nullptr);
        impl_->vk_surface = VK_NULL_HANDLE;
    }
    impl_->vk_instance.destroy();

    // Window.
    impl_->window.destroy();

    std::printf("[snt_engine] Shutdown complete\n");
}

}  // namespace snt::engine
