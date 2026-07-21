// Snapshot-bound aggregate ResourceStack storage.
//
// This module owns an un-slotted resource ledger suitable for AE-style
// storage, tanks, network buffers, and tests. Slot limits, item-instance
// rules, fluid physical state, and endpoint policy remain in their owning
// modules. Every stored key is compact and valid only in key_context().

#pragma once

#include "core/expected.h"
#include "game/resources/resource_key.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace snt::game {

class ResourceLedgerStorage final : public IResourceStorage {
public:
    explicit ResourceLedgerStorage(ResourceKeyContext context);

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override;
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const override;
    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override;
    [[nodiscard]] int64_t extract(const ResourceKeyContext& context,
                                  const ResourceKey& key,
                                  int64_t amount,
                                  ResourceTransferMode mode) override;
    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const override;

    // Converts every amount through the stable content-key boundary before
    // swapping contexts. An unresolved key or an amount overflow leaves both
    // the old storage values and the old context unchanged.
    [[nodiscard]] snt::core::Expected<void> rebind(
        const IResourceKeyResolver& previous_resolver,
        const IResourceKeyResolver& next_resolver);

private:
    ResourceKeyContext context_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts_;
};

}  // namespace snt::game
