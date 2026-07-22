// Snapshot-bound aggregate ResourceStack storage.
//
// This module owns an un-slotted resource ledger suitable for AE-style
// storage, tanks, network buffers, and tests. Slot limits, item-instance
// rules, fluid physical state, and endpoint policy remain in their owning
// modules. Every stored key is compact and valid only in key_context().

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"
#include "game/resources/resource_aggregate_storage.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace snt::game {

class ResourceLedgerStorage final : public IResourceAggregateStorage {
public:
    explicit ResourceLedgerStorage(ResourceKeyContext context);

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override;
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const override;
    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override;
    [[nodiscard]] int64_t extract(const ResourceKeyContext& context,
                                  const ResourceStack& requested,
                                  ResourceTransferMode mode) override;
    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const override;
    [[nodiscard]] std::vector<ResourceStack> capture_runtime_contents(
        const ResourceKeyContext& context) const override;
    [[nodiscard]] bool set_resource_aggregate_observer(
        ResourceAggregateStorageHandle handle,
        IResourceAggregateMutationObserver& observer) noexcept override;
    void clear_resource_aggregate_observer() noexcept override;
    [[nodiscard]] bool has_resource_aggregate_observer() const noexcept override {
        return resource_aggregate_observer_ != nullptr;
    }

    // Converts every amount through the stable content-key boundary before
    // swapping contexts. An unresolved key or an amount overflow leaves both
    // the old storage values and the old context unchanged.
    [[nodiscard]] snt::core::Expected<void> rebind(
        const IResourceKeyResolver& previous_resolver,
        const IResourceKeyResolver& next_resolver);

private:
    [[nodiscard]] bool can_apply_resource_aggregate_delta(
        const ResourceStack& changed, int64_t delta) const noexcept;
    void apply_resource_aggregate_delta(
        const ResourceStack& changed, int64_t delta) noexcept;

    ResourceKeyContext context_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts_;
    IResourceAggregateMutationObserver* resource_aggregate_observer_ = nullptr;
    ResourceAggregateStorageHandle resource_aggregate_handle_;
};

}  // namespace snt::game
