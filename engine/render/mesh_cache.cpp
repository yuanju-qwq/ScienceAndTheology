// MeshCache implementation.

#define SNT_LOG_CHANNEL "render"
#include "core/log.h"

#include "render/mesh_cache.h"

#include "render_backend/vulkan_device.h"
#include "render_backend/vulkan_mesh.h"

#include <format>

namespace snt::render {

MeshCache::~MeshCache() {
    destroy();
}

snt::core::Expected<void> MeshCache::init(snt::render_backend::VulkanDevice& device) {
    device_ = &device;
    return {};
}

void MeshCache::destroy() {
    if (!device_) return;

    device_->wait_idle();

    for (auto* mesh : meshes_) {
        if (mesh) {
            mesh->destroy();
            delete mesh;
        }
    }
    meshes_.clear();
    path_to_handle_.clear();
    device_ = nullptr;
}

snt::core::Expected<MeshHandle> MeshCache::load(const std::string& path) {
    if (!device_) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "MeshCache::load: not initialized"};
    }

    // Deduplication: return existing handle if path already loaded.
    auto it = path_to_handle_.find(path);
    if (it != path_to_handle_.end()) {
        return it->second;
    }

    // Allocate a new VulkanMesh + load from disk.
    auto* mesh = new snt::render_backend::VulkanMesh();
    // Default color — P3 will replace with material system.
    float default_color[3] = {1.0f, 0.5f, 0.2f};
    if (auto r = mesh->load_obj(*device_, path, default_color); !r) {
        delete mesh;
        snt::core::Error e = r.error();
        e.with_context(std::format("MeshCache::load('{}')", path));
        return e;
    }

    // Register in slot map.
    uint32_t id = static_cast<uint32_t>(meshes_.size());
    meshes_.push_back(mesh);

    MeshHandle handle{id};
    path_to_handle_[path] = handle;
    return handle;
}

snt::render_backend::VulkanMesh* MeshCache::get(uint32_t handle_id) const {
    if (handle_id == 0xFFFFFFFFu) return nullptr;
    if (handle_id >= meshes_.size()) return nullptr;
    return meshes_[handle_id];
}

}  // namespace snt::render
