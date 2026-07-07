#pragma once

// Platform window abstraction built on top of SDL3.
// P1.3 adds Vulkan surface creation (platform layer owns the surface
// because it's tightly coupled to the native window handle).
//
// Vulkan handles (VkInstance/VkSurfaceKHR) are passed as void* to avoid
// pulling <vulkan/vulkan.h> into the platform layer. Callers in
// render_backend cast between void* and the real Vulkan types.

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

    // Create a Vulkan surface backed by this window.
    // `vk_instance` is a VkInstance cast to void*; `out_surface` receives
    // the resulting VkSurfaceKHR cast to uint64_t.
    // The instance must be created with the extensions returned by
    // sdl_vulkan_instance_extensions().
    // Returns true on success.
    bool create_vulkan_surface(void* vk_instance, uint64_t* out_surface);

    // SDL-required Vulkan instance extensions for window surface creation.
    // Call before vkCreateInstance and add these to VkInstanceCreateInfo.
    // Returns a pointer to a static array of extension name strings
    // (valid until SDL_Quit). `count` receives the array length.
    const char* const* sdl_vulkan_instance_extensions(uint32_t* count);

private:
    void* _window = nullptr;        // SDL_Window*
    bool _should_close = false;
};

} // namespace snt::platform
