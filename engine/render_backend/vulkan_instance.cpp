// Vulkan Instance implementation.

#include "vulkan_instance.h"

#include "platform/window.h"

#include <volk.h>  // must come after <vulkan/vulkan.h> types are available

#include <cstdio>
#include <cstring>
#include <vector>

namespace snt::render_backend {

// ---------------------------------------------------------------------------
// Debug messenger callback
// ---------------------------------------------------------------------------

VKAPI_ATTR VkBool32 VKAPI_CALL
VulkanInstance::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                               VkDebugUtilsMessageTypeFlagsEXT type,
                               const VkDebugUtilsMessengerCallbackDataEXT* data,
                               void* /*user_data*/) {
    // Map severity to a log prefix.
    const char* prefix = "[Vulkan]";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        prefix = "[Vulkan ERROR]";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        prefix = "[Vulkan WARN]";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        prefix = "[Vulkan info]";
    } else {
        prefix = "[Vulkan verbose]";
    }

    // Skip verbose/info by default to reduce noise; show warnings+errors.
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "%s: %s\n", prefix, data->pMessage);
    }
    return VK_FALSE;  // don't abort the call
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanInstance::~VulkanInstance() {
    destroy();
}

bool VulkanInstance::init(snt::platform::Window& window) {
    // --- Step 1: Volk initialize (loads vulkan-1.dll via LoadLibrary) ---
    if (volkInitialize() != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] volkInitialize failed: "
                             "vulkan-1.dll not found\n");
        return false;
    }

    // --- Step 2: gather instance extensions ---
    // SDL provides the platform-specific surface extensions (e.g.
    // VK_KHR_win32_surface, VK_KHR_surface).
    uint32_t sdl_ext_count = 0;
    const char* const* sdl_exts = window.sdl_vulkan_instance_extensions(&sdl_ext_count);

    std::vector<const char*> extensions;
    extensions.reserve(sdl_ext_count + 1);
    for (uint32_t i = 0; i < sdl_ext_count; ++i) {
        extensions.push_back(sdl_exts[i]);
    }

    // Debug utils extension for the messenger.
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // --- Step 3: validation layers (Debug only) ---
    std::vector<const char*> layers;
#ifdef NDEBUG
    // Release: no validation layer.
#else
    // Check if the layer is available before requesting it.
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    bool has_validation = false;
    for (const auto& l : available_layers) {
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            has_validation = true;
            break;
        }
    }
    if (has_validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        std::printf("[snt::render_backend] Validation layer: enabled\n");
    } else {
        std::fprintf(stderr, "[snt::render_backend] Warning: "
                             "VK_LAYER_KHRONOS_validation not available\n");
    }
#endif

    // --- Step 4: create VkInstance ---
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ScienceAndTheology",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "SNT Engine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    if (vkCreateInstance(&create_info, nullptr, &instance_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateInstance failed\n");
        return false;
    }

    // --- Step 5: load instance-level function pointers via Volk ---
    volkLoadInstance(instance_);

    // --- Step 6: register debug messenger ---
    if (!layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback,
            .pUserData = nullptr,
        };
        if (vkCreateDebugUtilsMessengerEXT(instance_, &messenger_info, nullptr,
                                           &debug_messenger_) != VK_SUCCESS) {
            std::fprintf(stderr, "[snt::render_backend] Warning: "
                                 "failed to create debug messenger\n");
            // Non-fatal: continue without messenger.
        }
    }

    std::printf("[snt::render_backend] VkInstance created (Vulkan 1.3)\n");
    return true;
}

void VulkanInstance::destroy() {
    if (instance_ == VK_NULL_HANDLE) return;

    if (debug_messenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
}

}  // namespace snt::render_backend
