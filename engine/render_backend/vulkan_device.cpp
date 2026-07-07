// Vulkan Device implementation.

#include "vulkan_device.h"

#include <volk.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

namespace snt::render_backend {

namespace {

// Query swapchain support for a specific physical device + surface.
// Non-member helper to avoid forward-declaration tangles.
VulkanDevice::SwapchainSupport query_swapchain_support_for(
    VkPhysicalDevice dev, VkSurfaceKHR surface) {
    VulkanDevice::SwapchainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface, &support.capabilities);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmt_count, nullptr);
    support.formats.resize(fmt_count);
    if (fmt_count > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmt_count,
                                             support.formats.data());
    }

    uint32_t pm_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pm_count, nullptr);
    support.present_modes.resize(pm_count);
    if (pm_count > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pm_count,
                                                  support.present_modes.data());
    }
    return support;
}

}  // namespace

// ---------------------------------------------------------------------------
// Rate physical device: prefer discrete GPU, fallback integrated.
// ---------------------------------------------------------------------------

int VulkanDevice::rate_device(VkPhysicalDevice dev, VkSurfaceKHR surface) const {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    int score = 0;

    // Discrete GPU is strongly preferred.
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 100;
    }

    // Texture size limits matter for a voxel game.
    score += static_cast<int>(props.limits.maxImageDimension2D);

    // Must support a graphics queue + present queue + swapchain extension.
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qf_props.data());

    bool has_graphics = false;
    bool has_present = false;
    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) has_graphics = true;
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present_support);
        if (present_support) has_present = true;
    }
    if (!has_graphics || !has_present) return 0;

    // Must support VK_KHR_swapchain extension.
    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &ext_count, exts.data());

    bool has_swapchain = false;
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            has_swapchain = true;
            break;
        }
    }
    if (!has_swapchain) return 0;

    // Must have at least one surface format + present mode.
    SwapchainSupport swap_support = query_swapchain_support_for(dev, surface);
    if (swap_support.formats.empty() || swap_support.present_modes.empty()) {
        return 0;
    }

    return score;
}

// Public wrapper: uses stored physical_ + surface_.
VulkanDevice::SwapchainSupport VulkanDevice::query_swapchain_support() const {
    return query_swapchain_support_for(physical_, surface_);
}

// ---------------------------------------------------------------------------
// Find queue families.
// ---------------------------------------------------------------------------

bool VulkanDevice::find_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qf_props.data());

    graphics_family_ = UINT32_MAX;
    present_family_ = UINT32_MAX;

    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_family_ = i;
        }
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present_support);
        if (present_support) {
            present_family_ = i;
        }
        // Prefer a family that supports both (common case).
        if (graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX) {
            return true;
        }
    }
    return graphics_family_ != UINT32_MAX && present_family_ != UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Init / destroy
// ---------------------------------------------------------------------------

VulkanDevice::~VulkanDevice() {
    destroy();
}

bool VulkanDevice::init(VkInstance instance, VkSurfaceKHR surface) {
    surface_ = surface;  // store for query_swapchain_support()

    // --- Step 1: enumerate physical devices ---
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(instance, &dev_count, nullptr);
    if (dev_count == 0) {
        std::fprintf(stderr, "[snt::render_backend] No GPU with Vulkan support\n");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(dev_count);
    vkEnumeratePhysicalDevices(instance, &dev_count, devices.data());

    // --- Step 2: pick best device ---
    VkPhysicalDevice best = VK_NULL_HANDLE;
    int best_score = 0;
    for (auto dev : devices) {
        int score = rate_device(dev, surface);
        if (score > best_score) {
            best_score = score;
            best = dev;
        }
    }
    physical_ = best;

    if (physical_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[snt::render_backend] No suitable GPU found\n");
        return false;
    }

    // Log the chosen device name.
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_, &props);
    std::printf("[snt::render_backend] GPU: %s (score %d)\n",
                props.deviceName, best_score);

    // --- Step 3: find queue families ---
    if (!find_queue_families(physical_, surface)) {
        std::fprintf(stderr, "[snt::render_backend] No graphics+present queue family\n");
        return false;
    }

    // --- Step 4: create logical device ---
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    std::set<uint32_t> unique_families = {graphics_family_, present_family_};

    float queue_priority = 1.0f;
    for (uint32_t family : unique_families) {
        queue_infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    VkPhysicalDeviceFeatures features{};

    const std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledLayerCount = 0,  // device layers deprecated; instance handles it
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &features,
    };

    if (vkCreateDevice(physical_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        std::fprintf(stderr, "[snt::render_backend] vkCreateDevice failed\n");
        return false;
    }

    // --- Step 5: load device-level function pointers ---
    volkLoadDevice(device_);

    // --- Step 6: retrieve queues ---
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);

    std::printf("[snt::render_backend] Logical device created "
                "(gfx family=%u, present family=%u)\n",
                graphics_family_, present_family_);
    return true;
}

void VulkanDevice::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    physical_ = VK_NULL_HANDLE;
    graphics_queue_ = VK_NULL_HANDLE;
    present_queue_ = VK_NULL_HANDLE;
}

void VulkanDevice::wait_idle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

}  // namespace snt::render_backend
