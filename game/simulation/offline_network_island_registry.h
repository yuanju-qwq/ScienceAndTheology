// Durable offline-network island ownership.
//
// One island snapshot is stored in the sidecar of its deterministic anchor
// chunk. This registry rebuilds the in-memory index, validates that every
// member machine has exactly that owner, and exposes transactional claim and
// release boundaries to the offline machine service.

#pragma once

#include "core/expected.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace snt::game {

struct OfflineNetworkIslandClaim {
    uint64_t island_id = 0;
    uint64_t ownership_epoch = 0;
    ChunkKey anchor_chunk;
};

class OfflineNetworkIslandRegistry final {
public:
    explicit OfflineNetworkIslandRegistry(GameChunkSidecarRegistry& sidecars) noexcept;

    // Rebuilds the transient id index after sidecars load. It rejects a world
    // where an island snapshot and per-machine ownership metadata disagree.
    [[nodiscard]] snt::core::Expected<void> initialize();

    // Claims a complete offline island. The method persists the snapshot and
    // transfers every listed machine record from kLoaded to
    // kOfflineNetworkIsland before the caller destroys their ECS runtimes.
    [[nodiscard]] snt::core::Expected<OfflineNetworkIslandClaim> claim(
        OfflineNetworkIslandSnapshot snapshot,
        uint64_t current_tick);

    // Reverts a just-claimed island when the following ECS destruction step
    // fails. A new record epoch invalidates any stale scheduled work.
    [[nodiscard]] snt::core::Expected<void> rollback_claim(
        const OfflineNetworkIslandClaim& claim);

    // Returns an immutable copy only after verifying that the snapshot still
    // owns every member record. The caller uses it to stage ECS restoration.
    [[nodiscard]] snt::core::Expected<OfflineNetworkIslandSnapshot> prepare_release(
        uint64_t island_id,
        uint64_t ownership_epoch) const;

    // Removes the anchored snapshot after the caller has moved every member
    // record back to kLoaded and restored its ECS runtime.
    [[nodiscard]] snt::core::Expected<void> complete_release(
        uint64_t island_id,
        uint64_t ownership_epoch);

    [[nodiscard]] OfflineNetworkIslandSnapshot* find(uint64_t island_id) noexcept;
    [[nodiscard]] const OfflineNetworkIslandSnapshot* find(uint64_t island_id) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return islands_.size(); }

private:
    struct IslandLocation {
        ChunkKey anchor_chunk;
        uint64_t ownership_epoch = 0;
    };

    GameChunkSidecarRegistry* sidecars_ = nullptr;
    std::map<uint64_t, IslandLocation> islands_;
};

}  // namespace snt::game
