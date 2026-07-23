// AE network topology and aggregate storage index.
//
// This module owns the new AE runtime boundary.  Cable topology is rebuilt
// only after a node or edge mutation, while node/component/channel queries
// are direct slot or hash lookups during normal ticks.  Storage aggregation
// intentionally has no scan-and-transfer API: attached storage publishes an
// initial compact snapshot once and then reports every committed mutation.
// That keeps network totals correct without reintroducing the old ME network's
// per-query scan across arbitrary IResourceStorage implementations.

#pragma once

#include "core/expected.h"
#include "game/automation/ae_network_types.h"
#include "game/resources/resource_key.h"
#include "game/resources/resource_aggregate_storage.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace snt::game {

// Generation checked handles are runtime-only.  A world/controller owner
// keeps stable block addresses in persistence and resolves them after load.
struct AeNetworkNodeHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const AeNetworkNodeHandle&,
                           const AeNetworkNodeHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AeNetworkNodeHandle>);

struct AeNetworkNodeConfig {
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = true;
    // Zero selects the current default (32) for a controller or channel
    // provider.  Other node types must keep this zero.
    int32_t provided_channels = 0;
};

struct AeNetworkNodeState {
    AeNetworkNodeHandle handle;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = false;
    bool online = false;
    uint32_t component_id = 0;
    int32_t provided_channels = 0;
};

struct AeNetworkComponentState {
    uint32_t id = 0;
    uint32_t node_count = 0;
    uint32_t controller_count = 0;
    int32_t total_channels = 0;
    int32_t online_devices = 0;
    int32_t offline_devices = 0;
    bool is_powered = false;
};

// Runtime topology for one AE cable graph.  `add_node`, `connect`, and other
// topology mutations may rebuild connected components in O(nodes + edges).
// Once rebuilt, `find_node`, `component_of`, `is_online`, and
// `component_state` are expected O(1).
class AeNetworkTopology final {
public:
    AeNetworkTopology();

    AeNetworkTopology(const AeNetworkTopology&) = delete;
    AeNetworkTopology& operator=(const AeNetworkTopology&) = delete;

    [[nodiscard]] snt::core::Expected<AeNetworkNodeHandle> add_node(
        AeNetworkNodeConfig config);
    [[nodiscard]] bool remove_node(AeNetworkNodeHandle handle) noexcept;
    [[nodiscard]] snt::core::Expected<void> connect(
        AeNetworkNodeHandle first, AeNetworkNodeHandle second);
    [[nodiscard]] bool disconnect(AeNetworkNodeHandle first,
                                  AeNetworkNodeHandle second) noexcept;
    [[nodiscard]] snt::core::Expected<void> set_node_enabled(
        AeNetworkNodeHandle handle, bool enabled);
    [[nodiscard]] snt::core::Expected<void> set_provided_channels(
        AeNetworkNodeHandle handle, int32_t provided_channels);

    [[nodiscard]] std::optional<AeNetworkNodeState> find_node(
        AeNetworkNodeHandle handle) const noexcept;
    [[nodiscard]] std::optional<uint32_t> component_of(
        AeNetworkNodeHandle handle) const noexcept;
    [[nodiscard]] bool is_online(AeNetworkNodeHandle handle) const noexcept;
    [[nodiscard]] std::optional<AeNetworkComponentState> component_state(
        uint32_t component_id) const noexcept;
    [[nodiscard]] bool are_connected(AeNetworkNodeHandle first,
                                     AeNetworkNodeHandle second) const noexcept;
    [[nodiscard]] size_t node_count() const noexcept { return node_count_; }
    [[nodiscard]] uint64_t topology_revision() const noexcept {
        return topology_revision_;
    }

private:
    struct NodeSlot {
        AeNetworkNodeType type = AeNetworkNodeType::kCable;
        bool occupied = false;
        bool enabled = false;
        bool online = false;
        uint32_t generation = 1;
        uint32_t component_id = 0;
        int32_t provided_channels = 0;
        std::unordered_set<uint32_t> neighbors;
    };

    [[nodiscard]] NodeSlot* find_slot(AeNetworkNodeHandle handle) noexcept;
    [[nodiscard]] const NodeSlot* find_slot(AeNetworkNodeHandle handle) const noexcept;
    [[nodiscard]] static bool is_channel_provider(AeNetworkNodeType type) noexcept;
    [[nodiscard]] static bool is_device(AeNetworkNodeType type) noexcept;
    [[nodiscard]] static int32_t default_provided_channels(
        AeNetworkNodeType type) noexcept;
    void rebuild_topology() noexcept;

    // Slot zero is permanently invalid, matching all runtime handle types.
    std::vector<NodeSlot> slots_;
    std::vector<uint32_t> reusable_slots_;
    std::unordered_map<uint32_t, AeNetworkComponentState> components_;
    size_t node_count_ = 0;
    uint32_t next_component_id_ = 1;
    uint64_t topology_revision_ = 0;
};

// Aggregate amounts for storage that has opted into the AE network ownership
// contract.  Attach and detach are boundary operations and may enumerate a
// endpoint's compact contents. Every normal `amount_of` query and accepted
// mutation is an expected O(1) lookup/update; this class deliberately does
// not expose extract/insert over an arbitrary collection of storages.
class AeNetworkStorageIndex final : public IResourceAggregateMutationObserver {
public:
    explicit AeNetworkStorageIndex(ResourceKeyContext context);

    AeNetworkStorageIndex(const AeNetworkStorageIndex&) = delete;
    AeNetworkStorageIndex& operator=(const AeNetworkStorageIndex&) = delete;

    [[nodiscard]] ResourceKeyContext key_context() const noexcept { return context_; }
    [[nodiscard]] snt::core::Expected<ResourceAggregateStorageHandle> attach_storage(
        std::vector<ResourceStack> initial_contents);
    [[nodiscard]] bool detach_storage(ResourceAggregateStorageHandle handle) noexcept;
    [[nodiscard]] bool is_attached(ResourceAggregateStorageHandle handle) const noexcept;
    [[nodiscard]] size_t storage_count() const noexcept { return storage_count_; }
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const noexcept;
    // Resource owners are indexed by compact key as well as aggregated by
    // compact key. Component transfer routers use this only to find the live
    // owners that currently hold a resource; terminal amount reads still use
    // amount_of() and never touch this list. The span is invalidated by the
    // next execute-mode mutation, attach, or detach on this index.
    [[nodiscard]] std::span<const ResourceAggregateStorageHandle> storage_holders(
        const ResourceKeyContext& context, const ResourceKey& key) const noexcept;
    // Aggregate key enumeration is an inspection/UI boundary. It visits the
    // aggregate map once and intentionally does not enumerate every cell.
    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const;

    [[nodiscard]] bool can_apply_resource_aggregate_delta(
        ResourceAggregateStorageHandle handle,
        const ResourceKeyContext& context,
        const ResourceStack& changed,
        int64_t delta) const noexcept override;
    void apply_resource_aggregate_delta(
        ResourceAggregateStorageHandle handle,
        const ResourceKeyContext& context,
        const ResourceStack& changed,
        int64_t delta) noexcept override;

private:
    struct StorageSlot {
        bool occupied = false;
        uint32_t generation = 1;
        std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts;
    };

    // Swap-remove plus a position hash keeps holder additions and removals
    // expected O(1), while retaining a compact list for a transfer that truly
    // has to visit several physical owners.
    struct ResourceHolderBucket {
        std::vector<ResourceAggregateStorageHandle> handles;
        std::unordered_map<uint64_t, size_t> positions;
    };

    [[nodiscard]] StorageSlot* find_slot(ResourceAggregateStorageHandle handle) noexcept;
    [[nodiscard]] const StorageSlot* find_slot(
        ResourceAggregateStorageHandle handle) const noexcept;
    [[nodiscard]] static bool is_valid_delta(const ResourceStack& changed,
                                             int64_t delta) noexcept;
    [[nodiscard]] static uint64_t handle_token(
        ResourceAggregateStorageHandle handle) noexcept;
    void add_resource_holder(const ResourceKey& key,
                             ResourceAggregateStorageHandle handle) noexcept;
    void remove_resource_holder(const ResourceKey& key,
                                ResourceAggregateStorageHandle handle) noexcept;
    void apply_delta_unchecked(StorageSlot& slot, ResourceAggregateStorageHandle handle,
                               const ResourceStack& changed,
                               int64_t delta) noexcept;

    ResourceKeyContext context_;
    std::vector<StorageSlot> slots_;
    std::vector<uint32_t> reusable_slots_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> aggregate_amounts_;
    std::unordered_map<ResourceKey, ResourceHolderBucket, ResourceKey::Hash>
        resource_holders_;
    size_t storage_count_ = 0;
};

}  // namespace snt::game
