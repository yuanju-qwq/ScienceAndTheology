// MeshCache: deduplicating mesh loader keyed by file path.
//
// P2.4 design (decision A: handle-based):
//   - load(path) returns a MeshHandle (uint32_t). Loading the same path
//     twice returns the same handle + reuses the underlying VulkanMesh.
//   - get(handle) returns the VulkanMesh* for rendering.
//   - Hot-reload hook: reload(path) re-loads the file + updates the
//     underlying VulkanMesh in place (P3 feature, stub for now).
//
// Rationale: handle-based access is type-safe + stable across cache
// rebuilds. Industry standard (Bevy Handle<T>, UE FMeshResourceHandle,
// Granite Handle<Mesh>).
//
// Lifetime: the cache owns all VulkanMesh objects. destroy() frees them.
// MeshRef components store handles, so cache rebuilds don't invalidate
// component data.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::render_backend {
class VulkanDevice;
class VulkanMesh;
}

namespace snt::render {

// Opaque mesh handle. kInvalid = 0xFFFFFFFF.
struct MeshHandle {
    uint32_t id = kInvalidId;
    bool valid() const { return id != kInvalidId; }
    static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
};

class MeshCache {
public:
    MeshCache() = default;
    ~MeshCache();

    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;

    // Bind the cache to a Vulkan device. Must be called before load().
    bool init(snt::render_backend::VulkanDevice& device);

    // Release all cached meshes. Idempotent.
    void destroy();

    // Load a mesh by path. If the path was already loaded, returns the
    // existing handle (deduplication). Otherwise loads + caches it.
    // Returns kInvalid on failure.
    MeshHandle load(const std::string& path);

    // Look up the VulkanMesh for a handle. Returns nullptr if invalid.
    // Accepts raw uint32_t so callers can pass any handle type with the
    // same layout (e.g. snt::ecs::MeshHandle) without tight coupling.
    snt::render_backend::VulkanMesh* get(uint32_t handle_id) const;

    // Convenience overload for the render-layer handle type.
    snt::render_backend::VulkanMesh* get(MeshHandle handle) const {
        return get(handle.id);
    }

    // (P3) Reload a mesh from disk. Stub for now.
    bool reload(MeshHandle handle) { (void)handle; return false; }

private:
    snt::render_backend::VulkanDevice* device_ = nullptr;

    // Slot map: handle.id -> index in meshes_.
    // Handles are stable; meshes_ can be reindexed without invalidating
    // handles (P3 hot-reload support).
    std::vector<snt::render_backend::VulkanMesh*> meshes_;
    std::unordered_map<std::string, MeshHandle> path_to_handle_;
};

}  // namespace snt::render
