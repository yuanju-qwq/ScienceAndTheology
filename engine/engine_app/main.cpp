// SNT engine main entry.
// P1.1: minimal skeleton — create window + poll events until close.
// P1.3 will add Vulkan device; P1.4 will render the triangle.

#include "platform/window.h"

#include <cstdio>

int main(int argc, char* argv[]) {
    using namespace snt::platform;

    std::printf("[snt_engine] Starting ScienceAndTheology engine (P1.1 skeleton)\n");
    std::printf("[snt_engine] SDL3 window + event loop only\n");

    Window window;
    if (!window.create(WindowDesc{
            .title = "ScienceAndTheology Engine (P1.1)",
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
    std::printf("[snt_engine] Press ESC or close window to exit\n");

    // P1.2 will add proper input + render frame loop here.
    while (window.poll_events()) {
        // P1.3: vkAcquireNextImage -> record -> vkQueueSubmit -> vkQueuePresent
    }

    std::printf("[snt_engine] Shutdown\n");
    return 0;
}
