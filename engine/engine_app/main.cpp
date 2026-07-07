// SNT engine main entry.
// P1.4: full rendering pipeline — renders a colored triangle.
//
// Pipeline: window -> Vulkan instance -> surface -> device -> swapchain
//           -> render pass -> pipeline -> vertex buffer -> draw frame

#include "core/job_system.h"
#include "platform/window.h"
#include "render_backend/vulkan_buffer.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_frame.h"
#include "render_backend/vulkan_instance.h"
#include "render_backend/vulkan_pipeline.h"
#include "render_backend/vulkan_render_pass.h"
#include "render_backend/vulkan_swapchain.h"
#include "ui/debug_panel.h"
#include "ui/mui.h"

#include <chrono>
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

// Triangle vertices: position (2D) + color (3D), interleaved.
// Forms a classic RGB triangle in the center of the screen.
constexpr snt::render_backend::Vertex kTriangleVertices[] = {
    {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},  // top, red
    {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},  // bottom-right, green
    {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}},  // bottom-left, blue
};
constexpr uint32_t kVertexCount = 3;

}  // namespace

int main(int argc, char* argv[]) {
    using namespace snt::platform;
    using namespace snt::core;
    using namespace snt::render_backend;
    using namespace snt::ui;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P1.4)\n");
    std::printf("[snt_engine] Full rendering pipeline: colored triangle\n");

    // --- Init window ---
    Window window;
    if (!window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P1.4)",
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
    std::printf("[snt_engine] Vulkan surface created\n");

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

    // --- Init render pass + framebuffers ---
    VulkanRenderPass vk_render_pass;
    if (!vk_render_pass.init(vk_device, vk_swapchain)) {
        std::fprintf(stderr, "[snt_engine] VulkanRenderPass init failed\n");
        return 1;
    }

    // --- Init graphics pipeline ---
    VulkanPipeline vk_pipeline;
    // SPIR-V files are copied to <exe_dir>/shaders/ by CMake post-build step.
    const std::string exe_dir = "shaders";
    if (!vk_pipeline.init(vk_device, vk_render_pass,
                          exe_dir + "/triangle.vert.spv",
                          exe_dir + "/triangle.frag.spv")) {
        std::fprintf(stderr, "[snt_engine] VulkanPipeline init failed\n");
        return 1;
    }

    // --- Create vertex buffer ---
    VulkanBuffer vk_vertex_buffer;
    if (!vk_vertex_buffer.init(vk_device, sizeof(kTriangleVertices),
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               true /* cpu_visible */)) {
        std::fprintf(stderr, "[snt_engine] Vertex buffer init failed\n");
        return 1;
    }
    vk_vertex_buffer.write(kTriangleVertices, sizeof(kTriangleVertices));
    std::printf("[snt_engine] Vertex buffer created (%zu bytes)\n",
                sizeof(kTriangleVertices));

    // --- Init frame resources (command buffers + sync) ---
    VulkanFrame vk_frame;
    if (!vk_frame.init(vk_device,
                       static_cast<uint32_t>(vk_swapchain.image_views().size()))) {
        std::fprintf(stderr, "[snt_engine] VulkanFrame init failed\n");
        return 1;
    }

    // --- Init Job System + debug panel ---
    JobSystem& js = default_job_system();
    FpsTracker fps_tracker;
    auto& panel = default_debug_panel();
    panel.register_metric("FPS", [&]() { return fps_tracker.fps(); });
    panel.register_metric("Frame Time (ms)",
                          [&]() { return fps_tracker.last_frame_ms; });
    panel.register_metric("Job Workers",
                          [&]() { return static_cast<float>(js.worker_count()); });

    std::printf("[snt_engine] Press ESC to exit\n");
    std::printf("[snt_engine] Rendering colored triangle...\n");

    // --- Main loop ---
    auto last_time = Clock::now();
    while (window.poll_events()) {
        auto now = Clock::now();
        float frame_ms = DurationMs(now - last_time).count();
        last_time = now;
        fps_tracker.tick(frame_ms);

        // Sample metrics + draw debug panel (P1 stub: no visible output).
        panel.sample();
        MuiContext& mui = default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

        // Draw the triangle. Returns false if swapchain needs recreation.
        bool ok = vk_frame.draw_frame(vk_device, vk_swapchain, vk_render_pass,
                                      vk_pipeline, vk_vertex_buffer, kVertexCount);
        if (!ok) {
            // Window resized: recreate swapchain + framebuffers.
            vk_device.wait_idle();
            const auto new_sz = window.size();
            if (vk_swapchain.recreate(static_cast<uint32_t>(new_sz.width),
                                      static_cast<uint32_t>(new_sz.height))) {
                vk_render_pass.recreate_framebuffers(vk_swapchain);
                std::printf("[snt_engine] Swapchain recreated: %dx%d\n",
                            new_sz.width, new_sz.height);
            }
        }
    }

    // --- Cleanup: wait for GPU before destroying resources ---
    vk_device.wait_idle();

    std::printf("[snt_engine] Shutdown\n");
    return 0;
}
