#pragma once

// Platform window abstraction built on top of SDL3.
// P1.2 will add event loop; P1.1 only declares the interface.

#include <cstdint>
#include <string_view>

namespace snt::platform {

struct WindowSize {
    int width;
    int height;
};

// Window creation descriptor.
struct WindowDesc {
    std::string_view title;
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    // Vulkan surface will be created by render_backend in P1.3.
    bool vulkan_enabled = true;
};

// RAII window. Destructor releases SDL resources.
class Window {
public:
    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    // Create the window. Returns false on failure (SDL error logged).
    bool create(const WindowDesc& desc);
    void destroy();

    // Poll window events. Returns false if window requested close.
    bool poll_events();

    // Swap chain presentation is handled in render_backend; this only
    // exposes the native window handle for surface creation.
    void* native_handle() const;

    WindowSize size() const;
    bool should_close() const { return _should_close; }

private:
    void* _window = nullptr;        // SDL_Window*
    bool _should_close = false;
};

} // namespace snt::platform
