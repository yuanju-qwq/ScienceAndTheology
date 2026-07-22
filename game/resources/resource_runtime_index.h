// Immutable content-resource to compact-resource index.
//
// ResourceContentKey remains the durable/content-facing identity. This module
// turns a frozen set of those keys into fixed-width ResourceKey values for
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
#include <string_view>
#include <utility>

namespace snt::game {

class ResourceRuntimeIndex final {
private:
    struct Data;

public:
    class Snapshot final : public IResourceKeyResolver {
    public:
        Snapshot() = default;

        [[nodiscard]] ResourceKeyContext key_context() const noexcept override;
        [[nodiscard]] std::optional<ResourceKey> resolve_runtime(
            const ResourceContentKey& key) const override;
        [[nodiscard]] std::optional<ResourceContentKey> resolve_content(
            const ResourceKey& key) const override;
        // Resolves a content category once at a boundary so hot paths can
        // classify compact keys without retaining or comparing type strings.
        [[nodiscard]] std::optional<ResourceKind> resource_kind(
            std::string_view type) const noexcept;
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

    // Content keys must be complete and canonical for the snapshot being
    // published. IDs are assigned in lexical order and are valid only inside
    // the resulting immutable snapshot.
    [[nodiscard]] snt::core::Expected<Snapshot> build_snapshot(
        std::span<const ResourceContentKey> keys) const;
    [[nodiscard]] snt::core::Expected<void> rebuild(
        std::span<const ResourceContentKey> keys);

    [[nodiscard]] Snapshot snapshot() const noexcept { return Snapshot(data_); }
    void restore(Snapshot snapshot) noexcept;

private:
    [[nodiscard]] static ResourceKeyContext make_key_context(
        std::shared_ptr<const Data> data) noexcept;

    std::shared_ptr<const Data> data_;
};

// Main-thread participant in a resource-runtime snapshot publication. A
// participant owns live compact ResourceKey values such as player inventory,
// machine slots, tanks, pipes, or digital storage. It first resolves every
// retained value through the durable content identity; only when every
// participant has prepared successfully does the registry publish the new
// snapshot and ask each participant to commit its already prepared values.
//
// prepare_resource_runtime_snapshot() must leave its live state unchanged on
// failure. commit_resource_runtime_snapshot() is deliberately infallible: it
// may only install values prepared by the preceding call. cancel is invoked
// for every prepared participant when another participant rejects a reload.
class IResourceRuntimeSnapshotParticipant {
public:
    virtual ~IResourceRuntimeSnapshotParticipant() = default;

    [[nodiscard]] virtual snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) = 0;
    virtual void commit_resource_runtime_snapshot() noexcept = 0;
    virtual void cancel_resource_runtime_snapshot() noexcept = 0;
};

}  // namespace snt::game
