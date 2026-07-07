// AssetManager implementation.

#define SNT_LOG_CHANNEL "assets"
#include "core/log.h"

#include "assets/asset_manager.h"

#include "render_backend/vulkan_device.h"

namespace snt::assets {

AssetManager& AssetManager::instance() {
    static AssetManager inst;
    return inst;
}

snt::core::Expected<void> AssetManager::init(snt::render_backend::VulkanDevice* device) {
    if (!device) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "AssetManager::init: null device"};
    }
    if (device_) {
        // Already initialized; re-init is a no-op (idempotent).
        return {};
    }
    device_ = device;
    mesh_loader_.init(device);

    // Wire the mesh cache with closures over the loader.
    if (auto r = mesh_cache_.init(
            [this](const std::string& path) { return mesh_loader_.load(path); },
            [this](snt::render_backend::VulkanMesh* m) { mesh_loader_.destroy(m); });
        !r) {
        snt::core::Error e = r.error();
        e.with_context("AssetManager::init (mesh_cache)");
        return e;
    }

    SNT_LOG_INFO("AssetManager initialized (device=%p)",
                 static_cast<const void*>(device));
    return {};
}

void AssetManager::shutdown() {
    if (!device_) return;
    // Wait for the GPU to finish using any cached resources before
    // their backing buffers are torn down. This matches the original
    // MeshCache::destroy() prelude.
    device_->wait_idle();
    mesh_cache_.destroy();
    device_ = nullptr;
    SNT_LOG_INFO("AssetManager shut down");
}

}  // namespace snt::assets
