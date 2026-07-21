// Immutable semantic-resource to runtime-resource index.
//
// ResourceKey remains the durable/content-facing identity. This module turns
// a frozen set of those keys into fixed-width RuntimeResourceKey values for
// worker and storage hot paths. Rebuilds publish an entirely new snapshot so
// already captured work retains coherent numeric IDs through a content reload.

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace snt::game {

class ResourceRuntimeIndex final {
private:
    struct Data;

public:
    class Snapshot final : public IResourceKeyResolver {
    public:
        Snapshot() = default;

        [[nodiscard]] std::optional<RuntimeResourceKey> resolve_runtime(
            const ResourceKey& key) const override;
        [[nodiscard]] std::optional<ResourceKey> resolve_semantic(
            const RuntimeResourceKey& key) const override;
        [[nodiscard]] uint64_t generation() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
        friend class ResourceRuntimeIndex;

        explicit Snapshot(std::shared_ptr<const Data> data) noexcept
            : data_(std::move(data)) {}

        std::shared_ptr<const Data> data_;
    };

    ResourceRuntimeIndex();

    ResourceRuntimeIndex(const ResourceRuntimeIndex&) = delete;
    ResourceRuntimeIndex& operator=(const ResourceRuntimeIndex&) = delete;

    // Keys must be complete and canonical for the content snapshot being
    // published. IDs are assigned in lexical order and are valid only inside
    // the resulting immutable snapshot.
    [[nodiscard]] snt::core::Expected<void> rebuild(std::span<const ResourceKey> keys);

    [[nodiscard]] Snapshot snapshot() const noexcept { return Snapshot(data_); }
    void restore(Snapshot snapshot) noexcept;

private:
    std::shared_ptr<const Data> data_;
};

}  // namespace snt::game
