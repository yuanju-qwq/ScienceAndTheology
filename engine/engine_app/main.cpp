// SNT engine main entry.
// P1.5: ECS + mesh rendering with MVP UBO + depth + WASD camera.
//
// Pipeline: window -> Vulkan instance -> surface -> device(+VMA) -> swapchain
//           -> depth buffer -> render pass(color+depth) -> descriptor(UBO)
//           -> pipeline(mesh shaders) -> mesh(.obj) -> draw frame
//
// ECS: World with Camera entity + Mesh entity. CameraSystem handles WASD.
// Render: builds MVP from camera + mesh transforms, writes to UBO each frame.

#include "core/job_system.h"
#include "ecs/camera_system.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "platform/window.h"
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

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>

// Anonymous namespace for file-local state.
namespace {

using Clock = std::chrono::high_resolution_clock;
using DurationMs = std::chrono::duration<float, std::milli>;

// FPS tracker: keeps last N frame times for averaging + plotting.
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

int main(int argc, char* argv[]) {
    using namespace snt::platform;
    using namespace snt::core;
    using namespace snt::render_backend;
    using namespace snt::ecs;
    using namespace snt::ui;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P1.5)\n");
    std::printf("[snt_engine] ECS + mesh rendering + WASD camera\n");

    // --- Init window ---
    Window window;
    if (!window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P1.5)",
            .width = 1280,
            .height = 720,
            .resizable = true,
            .vulkan_enabled = true,
        })) {
        std::fprintf(stderr, "[snt_engine] Failed to create window\n");
        return 1;
    }
    const auto sz = window.size();
    std::printf("[snt_engine] Window created: %dx%d\n", sz.width, sz.height);

    // --- Init Vulkan instance ---
    VulkanInstance vk_instance;
    if (!vk_instance.init(window)) {
        std::fprintf(stderr, "[snt_engine] VulkanInstance init failed\n");
        return 1;
    }

    // --- Create Vulkan surface ---
    uint64_t surface_u64 = 0;
    if (!window.create_vulkan_surface(
            reinterpret_cast<void*>(vk_instance.handle()), &surface_u64)) {
        std::fprintf(stderr, "[snt_engine] create_vulkan_surface failed\n");
        return 1;
    }
    VkSurfaceKHR surface = reinterpret_cast<VkSurfaceKHR>(surface_u64);

    // --- Init Vulkan device (with VMA allocator) ---
    VulkanDevice vk_device;
    if (!vk_device.init(vk_instance.handle(), surface)) {
        std::fprintf(stderr, "[snt_engine] VulkanDevice init failed\n");
        return 1;
    }

    // --- Init swapchain ---
    VulkanSwapchain vk_swapchain;
    if (!vk_swapchain.init(vk_device,
                           static_cast<uint32_t>(sz.width),
                           static_cast<uint32_t>(sz.height))) {
        std::fprintf(stderr, "[snt_engine] VulkanSwapchain init failed\n");
        return 1;
    }

    // --- Init depth buffer ---
    VulkanDepth vk_depth;
    if (!vk_depth.init(vk_device, vk_swapchain)) {
        std::fprintf(stderr, "[snt_engine] VulkanDepth init failed\n");
        return 1;
    }

    // --- Init render pass (color + depth) ---
    VulkanRenderPass vk_render_pass;
    if (!vk_render_pass.init(vk_device, vk_swapchain, vk_depth)) {
        std::fprintf(stderr, "[snt_engine] VulkanRenderPass init failed\n");
        return 1;
    }

    // --- Init descriptor sets (UBO) ---
    VulkanDescriptor vk_descriptor;
    if (!vk_descriptor.init(vk_device)) {
        std::fprintf(stderr, "[snt_engine] VulkanDescriptor init failed\n");
        return 1;
    }

    // --- Init graphics pipeline (mesh shaders + depth + descriptor) ---
    VulkanPipeline vk_pipeline;
    if (!vk_pipeline.init(vk_device, vk_render_pass, vk_descriptor,
                          "shaders/mesh.vert.spv", "shaders/mesh.frag.spv")) {
        std::fprintf(stderr, "[snt_engine] VulkanPipeline init failed\n");
        return 1;
    }

    // --- Load mesh from .obj ---
    VulkanMesh vk_mesh;
    float cube_color[3] = {0.8f, 0.6f, 0.2f};  // orange
    if (!vk_mesh.load_obj(vk_device, "assets/cube.obj", cube_color)) {
        std::fprintf(stderr, "[snt_engine] Mesh load failed\n");
        return 1;
    }

    // --- Init frame resources ---
    VulkanFrame vk_frame;
    if (!vk_frame.init(vk_device,
                       static_cast<uint32_t>(vk_swapchain.image_views().size()))) {
        std::fprintf(stderr, "[snt_engine] VulkanFrame init failed\n");
        return 1;
    }

    // --- Init ECS World ---
    World world;

    // Create camera entity.
    entt::entity camera_entity = world.create_entity();
    auto& cam_transform = world.add_component<Transform>(camera_entity);
    cam_transform.position[2] = -3.0f;  // move camera back
    auto& cam_comp = world.add_component<Camera>(camera_entity);
    cam_comp.aspect = static_cast<float>(sz.width) / static_cast<float>(sz.height);

    // Create mesh entity (the cube).
    entt::entity cube_entity = world.create_entity();
    auto& cube_transform = world.add_component<Transform>(cube_entity);
    world.add_component<MeshRef>(cube_entity, MeshRef{"assets/cube.obj"});

    // Register camera system.
    auto& camera_system = world.add_system<CameraSystem>();
    camera_system.set_window(&window);
    camera_system.set_active_camera(camera_entity);

    // --- Init Job System + debug panel ---
    JobSystem& js = default_job_system();
    FpsTracker fps_tracker;
    auto& panel = default_debug_panel();
    panel.register_metric("FPS", [&]() { return fps_tracker.fps(); });
    panel.register_metric("Frame Time (ms)",
                          [&]() { return fps_tracker.last_frame_ms; });
    panel.register_metric("Job Workers",
                          [&]() { return static_cast<float>(js.worker_count()); });

    std::printf("[snt_engine] Controls: WASD=move, QE=up/down, "
                "Right-drag=look, Shift=boost\n");
    std::printf("[snt_engine] Rendering cube...\n");

    // --- Main loop ---
    auto last_time = Clock::now();
    while (window.poll_events()) {
        auto now = Clock::now();
        float frame_ms = DurationMs(now - last_time).count();
        float dt = frame_ms / 1000.0f;
        last_time = now;
        fps_tracker.tick(frame_ms);

        // Update ECS (camera system reads input + updates camera transform).
        world.update(dt);

        // Build MVP matrices.
        glm::mat4 model = build_model_matrix(cube_transform);
        glm::mat4 view = build_view_matrix(cam_transform);
        glm::mat4 proj = glm::perspective(glm::radians(cam_comp.fov),
                                           cam_comp.aspect,
                                           cam_comp.near_plane,
                                           cam_comp.far_plane);
        // GLM uses OpenGL clip space (Y up); Vulkan uses Y down. Flip Y.
        proj[1][1] *= -1.0f;

        UniformBufferObject ubo{};
        std::memcpy(ubo.model, glm::value_ptr(model), sizeof(ubo.model));
        std::memcpy(ubo.view, glm::value_ptr(view), sizeof(ubo.view));
        std::memcpy(ubo.proj, glm::value_ptr(proj), sizeof(ubo.proj));

        // Sample metrics + draw debug panel (P1 stub: no visible output).
        panel.sample();
        MuiContext& mui = default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

        // Draw the mesh.
        bool ok = vk_frame.draw_frame(vk_device, vk_swapchain, vk_render_pass,
                                      vk_pipeline, vk_descriptor, vk_mesh, ubo);
        if (!ok) {
            // Window resized: recreate swapchain + depth + framebuffers.
            vk_device.wait_idle();
            const auto new_sz = window.size();
            if (vk_swapchain.recreate(static_cast<uint32_t>(new_sz.width),
                                      static_cast<uint32_t>(new_sz.height))) {
                vk_depth.recreate(vk_swapchain);
                vk_render_pass.recreate_framebuffers(vk_swapchain, vk_depth);
                cam_comp.aspect = static_cast<float>(new_sz.width) /
                                  static_cast<float>(new_sz.height);
                std::printf("[snt_engine] Swapchain recreated: %dx%d\n",
                            new_sz.width, new_sz.height);
            }
        }
    }

    // --- Cleanup ---
    vk_device.wait_idle();

    std::printf("[snt_engine] Shutdown\n");
    return 0;
}
