// Generic deduplicating asset cache.
//
// Design:
//   - load(path) returns an AssetHandle<Tag>. Loading the same path twice
//     returns the same handle (deduplication via path_to_handle_ map).
//   - get(handle) returns T* (or nullptr for invalid handle).
//   - Lifetime: the cache owns all T* objects; destroy() releases them.
//   - Loading + destruction are delegated to caller-provided functions
//     (LoaderFn / DestroyFn), so the cache is resource-agnostic.
//   - `Tag` is decoupled from `T` so handles can be lightweight phantom
//     types (e.g. MeshAssetTag) that don't drag in the asset's full
//     definition. Default Tag=T lets tests omit the second parameter.
//
// Rationale: replaces the ad-hoc MeshCache with a reusable template.
// Adding a new asset type (Texture, Material) is now: define a Tag,
// define a Loader, register with AssetManager — no new cache code.

#pragma once

#include "assets/asset_handle.h"
#include "core/expected.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::assets {

template <typename T, typename Tag = T>
class AssetCache {
public:
    using LoaderFn  = std::function<snt::core::Expected<T*>(const std::string&)>;
    using DestroyFn = std::function<void(T*)>;

    AssetCache() = default;
    ~AssetCache() { destroy(); }

    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;

    // Wire up the loader + destroyer. Must be called before load().
    // `loader` returns a heap-owned T* on success (cache takes ownership)
    // or an Error. `destroyer` is called once per cached asset during
    // destroy(); it must release both the resource (e.g.
    // VulkanMesh::destroy()) and the heap memory (delete).
    snt::core::Expected<void> init(LoaderFn loader, DestroyFn destroyer) {
        if (!loader || !destroyer) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                    "AssetCache::init: null loader/destroyer"};
        }
        loader_   = std::move(loader);
        destroyer_ = std::move(destroyer);
        return {};
    }

    // Release all cached assets. Idempotent.
    void destroy() {
        if (destroyer_) {
            for (auto* p : slots_) {
                if (p) destroyer_(p);
            }
        }
        slots_.clear();
        path_to_handle_.clear();
        loader_   = nullptr;
        destroyer_ = nullptr;
    }

    // Load a path. Dedup: same path -> same handle.
    snt::core::Expected<AssetHandle<Tag>> load(const std::string& path) {
        if (!loader_) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "AssetCache::load: not initialized"};
        }
        auto it = path_to_handle_.find(path);
        if (it != path_to_handle_.end()) {
            return it->second;
        }
        auto r = loader_(path);
        if (!r) {
            snt::core::Error e = r.error();
            e.with_context("AssetCache::load('" + path + "')");
            return e;
        }
        uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(*r);
        AssetHandle<Tag> h{id};
        path_to_handle_[path] = h;
        return h;
    }

    // Look up by handle. Returns nullptr for invalid handle.
    T* get(AssetHandle<Tag> h) const {
        if (h.id == AssetHandle<Tag>::kInvalidId) return nullptr;
        if (h.id >= slots_.size()) return nullptr;
        return slots_[h.id];
    }

    // Convenience: same lookup but accepts a raw id (for callers like
    // ECS components that store the uint32_t directly).
    T* get(uint32_t id) const {
        return get(AssetHandle<Tag>{id});
    }

    // (P3) Reload a cached asset from disk. Stub for now.
    snt::core::Expected<void> reload(AssetHandle<Tag> h) {
        (void)h;
        return snt::core::Error{snt::core::ErrorCode::kNotImplemented,
                                "AssetCache::reload not implemented"};
    }

    size_t size() const { return slots_.size(); }

private:
    LoaderFn  loader_;
    DestroyFn destroyer_;
    std::vector<T*> slots_;
    std::unordered_map<std::string, AssetHandle<Tag>> path_to_handle_;
};

}  // namespace snt::assets
