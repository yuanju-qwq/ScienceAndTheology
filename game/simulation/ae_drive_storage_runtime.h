// Active owner for persisted AE drive cells.
//
// Chunk sidecars own stable ResourceContentStack values. This service owns
// the corresponding live AeStorageCell objects only while their drive blocks
// are materialized, then attaches them to AeNetworkRuntimeService. It is the
// sole bridge between durable drive contents and compact ResourceKey hot-path
// state; normal AE totals remain hash-index reads in AeNetworkRuntimeService.

#pragma once

#include "core/expected.h"
#include "game/automation/ae_storage_cell.h"
#include "game/resources/resource_runtime_index.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace snt::game {

class GameContentRegistry;

class AeDriveStorageRuntimeService final : public IResourceRuntimeSnapshotParticipant {
public:
    AeDriveStorageRuntimeService(AeNetworkRuntimeService& network_runtime,
                                 const GameContentRegistry& content) noexcept;
    ~AeDriveStorageRuntimeService() override;

    AeDriveStorageRuntimeService(const AeDriveStorageRuntimeService&) = delete;
    AeDriveStorageRuntimeService& operator=(const AeDriveStorageRuntimeService&) = delete;

    // The physical AE topology for chunk_key must already be materialized.
    // Existing live anchors are retained, so this is safe after a placement
    // refresh that adds a second drive to an active chunk.
    [[nodiscard]] snt::core::Expected<void> materialize_chunk(
        const ChunkKey& chunk_key,
        const GameChunkSidecar& sidecar);
    // Captures each compact cell back into the mutable sidecar before
    // detaching it from the network. Call this before physical topology is
    // dematerialized or before the terrain chunk is unloaded.
    [[nodiscard]] snt::core::Expected<void> dematerialize_chunk(
        const ChunkKey& chunk_key,
        GameChunkSidecar& sidecar);
    // Persists live cell contents without changing chunk residency. Save
    // paths call this at a boundary; it intentionally enumerates only one
    // drive cell's stored keys rather than participating in a tick path.
    [[nodiscard]] snt::core::Expected<void> flush_chunk(
        const ChunkKey& chunk_key,
        GameChunkSidecar& sidecar) const;

    [[nodiscard]] size_t active_drive_count() const noexcept { return drives_.size(); }
    [[nodiscard]] AeStorageCell* find_drive_cell(EntityId anchor_entity_id) noexcept;
    [[nodiscard]] const AeStorageCell* find_drive_cell(EntityId anchor_entity_id) const noexcept;

    // The service owns every real drive cell, so it coordinates the two-phase
    // resource snapshot lifecycle with the AE aggregate index. Cell rebinds
    // happen only at this boundary; ordinary insert/extract never parse a
    // stable string or rescan attached storage.
    [[nodiscard]] snt::core::Expected<void> prepare_resource_runtime_snapshot(
        ResourceRuntimeIndex::Snapshot next_snapshot) override;
    void commit_resource_runtime_snapshot() noexcept override;
    void cancel_resource_runtime_snapshot() noexcept override;

private:
    struct LiveDrive {
        ChunkKey chunk_key;
        EntityId anchor_entity_id;
        AeStorageCell cell;
        AeNetworkStorageAttachmentHandle attachment;
    };

    [[nodiscard]] static snt::core::Expected<void> write_persisted_contents(
        GameChunkSidecar& sidecar,
        EntityId anchor_entity_id,
        std::vector<ResourceContentStack> contents);
    [[nodiscard]] snt::core::Expected<void> rebuild_network_aggregates() noexcept;

    AeNetworkRuntimeService* network_runtime_ = nullptr;
    const GameContentRegistry* content_ = nullptr;
    std::unordered_map<uint64_t, std::unique_ptr<LiveDrive>> drives_;
    bool aggregates_detached_for_snapshot_ = false;
};

}  // namespace snt::game
