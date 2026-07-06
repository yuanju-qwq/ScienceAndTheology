// SNT engine main entry.
// P1.1: minimal skeleton — create window + poll events until close.
// P1.2: add MUI debug panel (stub) + Job System (serial stub).
// P1.3 will add Vulkan device; P1.4 will render the triangle.

#include "core/job_system.h"
#include "platform/window.h"
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
    using namespace snt::ui;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P1.2)\n");
    std::printf("[snt_engine] SDL3 window + MUI debug panel + Job System\n");

    // --- Init window ---
    Window window;
    if (!window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P1.2)",
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

    std::printf("[snt_engine] Press F1 to toggle debug panel, ESC to exit\n");

    // --- Main loop ---
    auto last_time = Clock::now();
    while (window.poll_events()) {
        auto now = Clock::now();
        float frame_ms = DurationMs(now - last_time).count();
        last_time = now;
        fps_tracker.tick(frame_ms);

        // F1 toggles debug panel visibility.
        // (P1: input not wired yet; toggle via MuiContext in P1.4.)
        // For now, panel is always visible (stub draws nothing).

        // Sample metrics and draw debug panel (P1 stub: no visible output).
        panel.sample();
        MuiContext& mui = default_mui_context();
        mui.begin_frame();
        panel.draw();
        mui.end_frame();

        // Demonstrate Job System API (P1 stub: runs synchronously).
        // P2: this will actually run on worker threads.
        constexpr int kTileCount = 4;
        js.parallel_for(kTileCount, [](int32_t /*thread*/, int32_t tile) {
            // P1: no-op tile. P2 will tick ECS chunks here.
            (void)tile;
        });

        // P1.3: vkAcquireNextImage -> record -> vkQueueSubmit -> vkQueuePresent
    }

    std::printf("[snt_engine] Shutdown\n");
    return 0;
}
