// AssetManager: central registry for asset caches.
//
// Design:
//   - Global singleton (mirrors default_job_system() pattern) so any
//     module (Engine, ECS systems, future scripts) can load assets
//     without threading pointers through every constructor.
//   - init(VulkanDevice*) wires up the built-in caches (currently mesh).
//     New asset types register here: add a VulkanTextureLoader, an
//     AssetCache<VulkanTexture>, and a texture_cache() accessor.
//   - shutdown() releases every cache. Must be called BEFORE the
//     VulkanDevice it was initialized with is destroyed.
//
// Future:
//   - Hot-reload: a watch thread on asset dirs triggers reload(handle).
//   - Async loading: load_async() submits a JobSystem task + returns
//     Future<Handle>.

#pragma once

#include "assets/asset_cache.h"
#include "assets/asset_handle.h"
#include "assets/mesh_asset_loader.h"
#include "core/expected.h"

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::assets {

class AssetManager {
public:
    // Singleton accessor.
    static AssetManager& instance();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Initialize all built-in caches. Must be called before any load().
    // The device pointer is borrowed (not owned) and must outlive the
    // AssetManager (call shutdown() before destroying the device).
    snt::core::Expected<void> init(snt::render_backend::VulkanDevice* device);

    // Release all caches. Idempotent. Must be called before the
    // VulkanDevice passed to init() is destroyed.
    void shutdown();

    // Access the mesh cache. Engine loads via this; RenderSystem resolves
    // handles via this. Returns a reference valid until shutdown().
    // Uses MeshAssetTag so handles are AssetHandle<MeshAssetTag> (= MeshHandle),
    // decoupled from the VulkanMesh type.
    AssetCache<snt::render_backend::VulkanMesh, MeshAssetTag>& mesh_cache() { return mesh_cache_; }

private:
    AssetManager() = default;
    ~AssetManager() { shutdown(); }

    snt::render_backend::VulkanDevice* device_ = nullptr;

    // Built-in loaders (own no heap state beyond the borrowed device ptr).
    VulkanMeshLoader mesh_loader_;

    // Built-in caches.
    AssetCache<snt::render_backend::VulkanMesh, MeshAssetTag> mesh_cache_;
};

}  // namespace snt::assets
