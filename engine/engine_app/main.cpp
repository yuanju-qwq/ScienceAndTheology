// SNT engine main entry.
// P1.1: minimal skeleton — create window + poll events until close.
// P1.2: add MUI debug panel (stub) + Job System (serial stub).
// P1.3: add Vulkan instance + device + swapchain.
// P1.4 will render the triangle.

#include "core/job_system.h"
#include "platform/window.h"
#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_instance.h"
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
        // Average over last 60 frames.
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

int main(int argc, char* argv[]) {
    using namespace snt::platform;
    using namespace snt::core;
    using namespace snt::render_backend;
    using namespace snt::ui;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P1.3)\n");
    std::printf("[snt_engine] SDL3 window + Vulkan device + Job System + MUI\n");

    // --- Init window ---
    Window window;
    if (!window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P1.3)",
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

    // --- Create Vulkan surface via platform window ---
    uint64_t surface_u64 = 0;
    if (!window.create_vulkan_surface(
            reinterpret_cast<void*>(vk_instance.handle()), &surface_u64)) {
        std::fprintf(stderr, "[snt_engine] create_vulkan_surface failed\n");
        return 1;
    }
    VkSurfaceKHR surface = reinterpret_cast<VkSurfaceKHR>(surface_u64);
    std::printf("[snt_engine] Vulkan surface created\n");

    // --- Init Vulkan device ---
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

    // --- Init Job System (P1 stub: serial) ---
    JobSystem& js = default_job_system();
    std::printf("[snt_engine] JobSystem: %d worker(s) (P1 serial stub)\n",
                js.worker_count());

    // --- Register debug metrics ---
    FpsTracker fps_tracker;
    auto& panel = default_debug_panel();
    panel.register_metric("FPS", [&]() { return fps_tracker.fps(); });
    panel.register_metric("Frame Time (ms)",
                          [&]() { return fps_tracker.last_frame_ms; });
    panel.register_metric("Job Workers",
                          [&]() { return static_cast<float>(js.worker_count()); });

    std::printf("[snt_engine] Press ESC to exit\n");

    // --- Main loop ---
    auto last_time = Clock::now();
    while (window.poll_events()) {
        auto now = Clock::now();
        float frame_ms = DurationMs(now - last_time).count();
        last_time = now;
        fps_tracker.tick(frame_ms);

        // Sample metrics and draw debug panel (P1 stub: no visible output).
        panel.sample();
        MuiContext& mui = default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

        // Demonstrate Job System API (P1 stub: runs synchronously).
        constexpr int kTileCount = 4;
        js.parallel_for(kTileCount, [](int32_t /*thread*/, int32_t tile) {
            (void)tile;
        });

        // P1.4: vkAcquireNextImage -> record -> vkQueueSubmit -> vkQueuePresent
    }

    // --- Cleanup: wait for GPU before destroying swapchain ---
    vk_device.wait_idle();

    std::printf("[snt_engine] Shutdown\n");
    return 0;
}
