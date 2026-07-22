// Mutation-aware resource storage attachment contract.
//
// Aggregate owners such as an AE component enumerate an endpoint only while
// attaching or rebuilding.  Live storage operations then publish compact
// ResourceStack deltas through this opt-in contract, keeping aggregate
// ResourceKey queries independent of the number of attached endpoints.

#pragma once

#include "game/resources/resource_key.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace snt::game {

// Opaque, generation-checked slot owned by the aggregate index. It has no
// durable meaning and must never enter a save, protocol message, or UI model.
struct ResourceAggregateStorageHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const ResourceAggregateStorageHandle&,
                           const ResourceAggregateStorageHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<ResourceAggregateStorageHandle>);

class IResourceAggregateMutationObserver {
public:
    virtual ~IResourceAggregateMutationObserver() = default;

    [[nodiscard]] virtual bool can_apply_resource_aggregate_delta(
        ResourceAggregateStorageHandle handle,
        const ResourceKeyContext& context,
        const ResourceStack& changed,
        int64_t delta) const noexcept = 0;
    virtual void apply_resource_aggregate_delta(
        ResourceAggregateStorageHandle handle,
        const ResourceKeyContext& context,
        const ResourceStack& changed,
        int64_t delta) noexcept = 0;
};

// An IResourceStorage that can participate in an aggregate without forcing
// the owner to rescan it every tick. Implementations must call the observer
// exactly once for every committed execute-mode delta after a successful
// preflight, and must clear it before their aggregate owner is destroyed.
class IResourceAggregateStorage : public IResourceStorage {
public:
    ~IResourceAggregateStorage() override = default;

    [[nodiscard]] virtual std::vector<ResourceStack> capture_runtime_contents(
        const ResourceKeyContext& context) const = 0;
    [[nodiscard]] virtual bool set_resource_aggregate_observer(
        ResourceAggregateStorageHandle handle,
        IResourceAggregateMutationObserver& observer) noexcept = 0;
    virtual void clear_resource_aggregate_observer() noexcept = 0;
    [[nodiscard]] virtual bool has_resource_aggregate_observer() const noexcept = 0;
};

}  // namespace snt::game
