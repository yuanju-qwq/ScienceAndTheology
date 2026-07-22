// Active automation-controller runtime owner.
//
// Durable controller records remain in GameChunkSidecar. This service owns
// only materialized controller executors, the shared endpoint-handle table,
// and compact ResourceKey bindings for the active content snapshot. Anchor
// lookup and chunk-to-controller lookup use hash indexes; fixed ticks visit a
// separately maintained anchor-sorted list for deterministic execution.

#pragma once

#include "core/expected.h"
#include "game/automation/sfm_endpoint_registry.h"
#include "game/resources/resource_runtime_index.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace snt::game {

// Value-only controller state for replication and UI composition. The program
// remains durable/content-facing here; SfmFlowExecutor owns the corresponding
// compact ResourceKey transfers privately on the fixed-tick path.
struct AutomationControllerRuntimePresentation {
    ChunkKey anchor_chunk;
    EntityId anchor_entity_id;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    AutomationControllerKind kind = AutomationControllerKind::kSfmManager;
    std::string controller_key;
    uint64_t authoritative_revision = 0;
    bool online = false;
    SfmFlowProgramRecord sfm_program;
};

struct AutomationControllerRuntimeTickResult {
    size_t active_controller_count = 0;
    size_t online_controller_count = 0;
    uint64_t dispatched_nodes = 0;
    uint64_t executed_transfers = 0;
    int64_t transferred_units = 0;
};

class AutomationControllerRuntimeService final
    : public IResourceRuntimeSnapshotParticipant {
public:
    explicit AutomationControllerRuntimeService(
        ResourceRuntimeIndex::Snapshot resource_snapshot);
    ~AutomationControllerRuntimeService() override = default;

    AutomationControllerRuntimeService(const AutomationControllerRuntimeService&) = delete;
    AutomationControllerRuntimeService& operator=(
        const AutomationControllerRuntimeService&) = delete;

    // Endpoint registration happens at a topology boundary. Registering or
    // removing one endpoint invalidates compiled transfer handles and causes
    // all active SFM programs to recompile before the next fixed tick.
    [[nodiscard]] snt::core::Expected<SfmEndpointHandle> register_sfm_endpoint(
        SfmEndpointAddress address, IResourceStorage& storage);
    [[nodiscard]] bool unregister_sfm_endpoint(SfmEndpointHandle handle) noexcept;

    // Materializes every controller owned by one active chunk. It is
    // transactional: malformed sidecar ownership leaves the prior active
    // runtime untouched. Unavailable endpoints or a currently unsupported
    // controller kind become an offline controller rather than a partial
    // executor, so automation fails closed until its next topology/content
    // rebuild.
    [[nodiscard]] snt::core::Expected<void> materialize_chunk(
        const ChunkKey& chunk_key, const GameChunkSidecar& sidecar);
    void dematerialize_chunk(const ChunkKey& chunk_key) noexcept;

    // Content publication prepares all compact bindings before the registry
    // swaps its ResourceRuntimeIndex snapshot. The service never retains a
    // numeric ResourceKey across generations.
    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

    // Deterministic main-thread execution. The scan is O(active controllers)
    // by necessity; each controller, endpoint handle, and runtime record
    // lookup inside it is expected O(1), and each executor keeps its own
    // due-time heap instead of scanning interval nodes.
    [[nodiscard]] snt::core::Expected<AutomationControllerRuntimeTickResult>
    fixed_tick(uint64_t tick_index);

    [[nodiscard]] size_t active_controller_count() const noexcept {
        return runtimes_.size();
    }
    [[nodiscard]] const AutomationControllerRuntimePresentation* find_controller(
        EntityId anchor_entity_id) const noexcept;
    [[nodiscard]] std::vector<AutomationControllerRuntimePresentation>
    collect_presentations(std::span<const ChunkKey> chunks) const;

private:
    struct Runtime {
        AutomationControllerRuntimePresentation presentation;
        std::optional<SfmFlowExecutor> sfm_executor;
        uint64_t endpoint_topology_revision = 0;
        std::string offline_reason;
    };

    using RuntimeMap = std::unordered_map<uint64_t, Runtime>;
    using ChunkRuntimeIndex = std::unordered_map<ChunkKey, std::vector<uint64_t>>;

    struct PreparedSnapshot {
        ResourceRuntimeIndex::Snapshot resource_snapshot;
        RuntimeMap runtimes;
        ChunkRuntimeIndex chunk_index;
        std::vector<uint64_t> tick_order;
    };

    [[nodiscard]] snt::core::Expected<Runtime> build_runtime(
        const ChunkKey& chunk_key,
        const GameChunkSidecar& sidecar,
        const AutomationControllerPersistenceRecord& record,
        const ResourceRuntimeIndex::Snapshot& resource_snapshot) const;
    [[nodiscard]] snt::core::Expected<PreparedSnapshot> rebuild_all(
        ResourceRuntimeIndex::Snapshot resource_snapshot) const;
    [[nodiscard]] static snt::core::Expected<void> validate_anchor(
        const ChunkKey& chunk_key,
        const GameChunkSidecar& sidecar,
        const AutomationControllerPersistenceRecord& record,
        const BlockEntityPlacement*& out_anchor);
    static void rebuild_indexes(RuntimeMap& runtimes, ChunkRuntimeIndex& chunk_index,
                                std::vector<uint64_t>& tick_order);
    void mark_endpoint_topology_changed() noexcept;
    void log_offline(const Runtime& runtime) const noexcept;

    ResourceRuntimeIndex::Snapshot resource_snapshot_;
    SfmEndpointRegistry endpoints_;
    RuntimeMap runtimes_;
    ChunkRuntimeIndex chunk_index_;
    std::vector<uint64_t> tick_order_;
    std::optional<PreparedSnapshot> prepared_snapshot_;
    uint64_t endpoint_topology_revision_ = 1;
    bool endpoint_rebuild_pending_ = false;
    std::optional<uint64_t> last_fixed_tick_;
};

}  // namespace snt::game
