// Active owner for chunk-materialized AE physical topology.
//
// Durable node records live in GameChunkSidecar. This service owns only
// generation-checked AeNetworkNodeHandle values and the rebuilt physical
// graph for active chunks. Placement/removal/materialization may rebuild in
// O(nodes + edges); all normal anchor, position, node, and component queries
// are expected O(1).

#pragma once

#include "core/expected.h"
#include "game/automation/ae_network.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace snt::game {

struct AeNetworkRuntimeNodePresentation {
    ChunkKey anchor_chunk;
    EntityId anchor_entity_id;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    AeNetworkNodeType type = AeNetworkNodeType::kCable;
    bool enabled = true;
    bool online = false;
    uint32_t component_id = 0;
    int32_t provided_channels = 0;
    uint64_t authoritative_revision = 0;
};

// Value-only aggregate for one currently materialized physical component.
// The service resolves this through the topology component hash table, so
// controller/UI/AOI reads stay O(1) after a boundary-time rebuild.
struct AeNetworkRuntimeComponentPresentation {
    uint32_t id = 0;
    uint32_t node_count = 0;
    uint32_t controller_count = 0;
    int32_t total_channels = 0;
    int32_t online_devices = 0;
    int32_t offline_devices = 0;
    bool is_powered = false;
};

// Generation-checked association between one live aggregate-capable endpoint
// and the physical drive/storage-bus node that owns it. This is runtime-only:
// the block anchor and durable endpoint contents remain the persistence
// boundary.
struct AeNetworkStorageAttachmentHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool is_valid() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(const AeNetworkStorageAttachmentHandle&,
                           const AeNetworkStorageAttachmentHandle&) = default;
};
static_assert(std::is_trivially_copyable_v<AeNetworkStorageAttachmentHandle>);

class AeNetworkRuntimeService final {
public:
    AeNetworkRuntimeService();
    ~AeNetworkRuntimeService();

    AeNetworkRuntimeService(const AeNetworkRuntimeService&) = delete;
    AeNetworkRuntimeService& operator=(const AeNetworkRuntimeService&) = delete;

    // Atomically replaces the active nodes owned by one chunk. Edges are
    // derived from adjacent roots with reciprocal connection-mask bits, so no
    // stale object pointer or persisted graph edge can survive a reload.
    [[nodiscard]] snt::core::Expected<void> materialize_chunk(
        const ChunkKey& chunk_key, const GameChunkSidecar& sidecar);
    [[nodiscard]] snt::core::Expected<void> dematerialize_chunk(
        const ChunkKey& chunk_key);

    [[nodiscard]] size_t active_node_count() const noexcept { return runtimes_.size(); }
    [[nodiscard]] uint64_t topology_revision() const noexcept;
    [[nodiscard]] const AeNetworkRuntimeNodePresentation* find_node(
        EntityId anchor_entity_id) const noexcept;
    [[nodiscard]] const AeNetworkRuntimeNodePresentation* find_node_at(
        std::string_view dimension_id, int32_t root_x, int32_t root_y,
        int32_t root_z) const;
    [[nodiscard]] std::optional<AeNetworkRuntimeComponentPresentation>
    find_component(uint32_t component_id) const noexcept;
    [[nodiscard]] std::vector<AeNetworkRuntimeNodePresentation> collect_presentations(
        std::span<const ChunkKey> chunks) const;
    [[nodiscard]] const AeNetworkTopology* topology() const noexcept { return topology_.get(); }

    // Attaches a mutation-aware storage endpoint to an active physical drive
    // or storage bus. An endpoint can remain attached while its node is
    // offline, but only an online component receives its aggregate. The owner
    // must detach before destroying the endpoint; this service intentionally
    // owns neither endpoint nor durable content.
    [[nodiscard]] snt::core::Expected<AeNetworkStorageAttachmentHandle>
    attach_storage(EntityId node_anchor, IResourceAggregateStorage& storage);
    [[nodiscard]] bool detach_storage(AeNetworkStorageAttachmentHandle handle) noexcept;
    [[nodiscard]] bool is_storage_attached(
        AeNetworkStorageAttachmentHandle handle) const noexcept;
    [[nodiscard]] std::optional<uint32_t> storage_component_of(
        AeNetworkStorageAttachmentHandle handle) const noexcept;
    [[nodiscard]] size_t attached_storage_count() const noexcept {
        return attached_storage_count_;
    }

    // A component-facing resource owner resolves a live terminal/controller
    // anchor to its current aggregate. Amount and key queries are direct
    // component-index reads. Mutations route only to the indexed physical
    // endpoint owners; they never expose the aggregate map as mutable state.
    [[nodiscard]] ResourceKeyContext resource_key_context_at_node(
        EntityId node_anchor) const noexcept;
    [[nodiscard]] int64_t insert_at_node(
        EntityId node_anchor, const ResourceKeyContext& context,
        const ResourceStack& stack, ResourceTransferMode mode);
    [[nodiscard]] int64_t extract_at_node(
        EntityId node_anchor, const ResourceKeyContext& context,
        const ResourceStack& requested, ResourceTransferMode mode);
    [[nodiscard]] std::vector<ResourceKey> stored_keys_at_node(
        EntityId node_anchor, const ResourceKeyContext& context) const;

    // Normal terminal/automation reads are two hash lookups at most: anchor
    // to component, then ResourceKey to aggregate amount.  No query walks the
    // attached cells or their contents.
    [[nodiscard]] int64_t amount_at_node(
        EntityId node_anchor, const ResourceKeyContext& context,
        const ResourceKey& key) const noexcept;
    [[nodiscard]] int64_t amount_in_component(
        uint32_t component_id, const ResourceKeyContext& context,
        const ResourceKey& key) const noexcept;
    [[nodiscard]] size_t storage_count_in_component(uint32_t component_id) const noexcept;

    // Content reload owners call the first method before endpoints rebind
    // their compact keys, then call the second after every endpoint has
    // committed the new resource snapshot. The physical attachment handles
    // remain valid; only snapshot-bound aggregate indexes are replaced.
    void detach_storage_aggregates_for_resource_reload() noexcept;
    [[nodiscard]] snt::core::Expected<void> rebuild_storage_aggregates();

private:
    struct Position {
        std::string dimension_id;
        int32_t root_x = 0;
        int32_t root_y = 0;
        int32_t root_z = 0;

        friend bool operator==(const Position&, const Position&) = default;
    };

    struct PositionHash {
        [[nodiscard]] size_t operator()(const Position& position) const noexcept;
    };

    struct Runtime {
        AeNetworkRuntimeNodePresentation presentation;
        AeNetworkNodeHandle handle;
        uint8_t connection_mask = CONN_ALL;
    };

    using RuntimeMap = std::unordered_map<uint64_t, Runtime>;
    using ChunkRuntimeIndex = std::unordered_map<ChunkKey, std::vector<uint64_t>>;
    using PositionRuntimeIndex = std::unordered_map<Position, uint64_t, PositionHash>;
    using ComponentStorageIndexMap =
        std::unordered_map<uint32_t, std::unique_ptr<AeNetworkStorageIndex>>;
    using ComponentStorageAttachmentIndex =
        std::unordered_map<uint32_t, std::unordered_map<uint64_t, uint32_t>>;

    struct StorageAttachmentSlot {
        IResourceAggregateStorage* storage = nullptr;
        EntityId node_anchor;
        uint32_t generation = 1;
        uint32_t component_id = 0;
        ResourceAggregateStorageHandle aggregate_handle;
    };

    struct PreparedStorageAttachment {
        uint32_t slot = 0;
        uint32_t component_id = 0;
        ResourceAggregateStorageHandle aggregate_handle;
    };

    struct PreparedStorageIndexes {
        ComponentStorageIndexMap indexes;
        std::vector<PreparedStorageAttachment> attachments;
    };

    struct TopologyBuild {
        std::unique_ptr<AeNetworkTopology> topology;
        PositionRuntimeIndex positions;
    };

    [[nodiscard]] static snt::core::Expected<void> validate_node(
        const ChunkKey& chunk_key,
        const GameChunkSidecar& sidecar,
        const AeNetworkNodePersistenceRecord& node,
        const BlockEntityPlacement*& out_anchor);
    [[nodiscard]] static snt::core::Expected<TopologyBuild> rebuild_topology(
        RuntimeMap& runtimes);
    [[nodiscard]] snt::core::Expected<PreparedStorageIndexes>
    prepare_storage_indexes(const RuntimeMap& runtimes) const;
    void commit_storage_indexes(PreparedStorageIndexes indexes) noexcept;
    void clear_storage_observers() noexcept;
    [[nodiscard]] StorageAttachmentSlot* find_storage_attachment(
        AeNetworkStorageAttachmentHandle handle) noexcept;
    [[nodiscard]] const StorageAttachmentSlot* find_storage_attachment(
        AeNetworkStorageAttachmentHandle handle) const noexcept;
    [[nodiscard]] StorageAttachmentSlot* find_component_storage_attachment(
        uint32_t component_id, ResourceAggregateStorageHandle handle) noexcept;
    [[nodiscard]] const StorageAttachmentSlot* find_component_storage_attachment(
        uint32_t component_id, ResourceAggregateStorageHandle handle) const noexcept;
    [[nodiscard]] static uint64_t aggregate_handle_token(
        ResourceAggregateStorageHandle handle) noexcept;
    [[nodiscard]] int64_t transfer_at_node(
        EntityId node_anchor, const ResourceKeyContext& context,
        const ResourceStack& stack, ResourceTransferMode mode, bool insert);
    [[nodiscard]] static bool is_storage_node_type(AeNetworkNodeType type) noexcept;
    static void rebuild_chunk_index(const RuntimeMap& runtimes,
                                    ChunkRuntimeIndex& chunk_index);

    RuntimeMap runtimes_;
    ChunkRuntimeIndex chunk_index_;
    PositionRuntimeIndex positions_;
    std::unique_ptr<AeNetworkTopology> topology_;
    std::vector<StorageAttachmentSlot> storage_attachments_;
    std::vector<uint32_t> reusable_storage_attachment_slots_;
    ComponentStorageIndexMap component_storage_indexes_;
    ComponentStorageAttachmentIndex component_storage_attachment_slots_;
    size_t attached_storage_count_ = 0;
};

// Non-owning resource facade for one active AE terminal/controller node.
// It intentionally holds no cached component id or endpoint pointer: a
// topology rebuild, chunk unload, or resource snapshot reload is observed on
// the next call. This lets AE autocrafting use the real network owner through
// the generic IResourceStorage contract without giving it aggregate internals.
class AeNetworkComponentStorage final : public IResourceStorage {
public:
    AeNetworkComponentStorage(AeNetworkRuntimeService& runtime,
                              EntityId node_anchor) noexcept
        : runtime_(&runtime), node_anchor_(node_anchor) {}

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

private:
    AeNetworkRuntimeService* runtime_ = nullptr;
    EntityId node_anchor_;
};

}  // namespace snt::game
