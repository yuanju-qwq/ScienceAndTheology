// Dedicated-server coordinator for an exactly-once ground-loot pickup.
//
// It bridges the player-state checkpoint, the chunk sidecar checkpoint, and
// the durable journal without exposing any transport or ECS implementation to
// world-save code.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/world/save/ground_loot_pickup_journal.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game {
class GameChunkSidecarRegistry;
class GameContentRegistry;
class GameWorldPersistenceLifecycle;
}

namespace snt::game::replication {

class GameServerPlayerLifecycle;

class GameServerGroundLootPickupPersistence final {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerGroundLootPickupPersistence>>
    create(std::string universe_save_dir, GameServerPlayerLifecycle& player_lifecycle,
           GameWorldPersistenceLifecycle& world_persistence, snt::voxel::ChunkRegistry& chunks,
           GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content);

    GameServerGroundLootPickupPersistence(const GameServerGroundLootPickupPersistence&) = delete;
    GameServerGroundLootPickupPersistence& operator=(
        const GameServerGroundLootPickupPersistence&) = delete;

    // Reconciles every journal entry before players or replication can see
    // sidecar state. A persisted receipt commits the player owner; an absent
    // receipt restores the original chunk-owned record.
    [[nodiscard]] snt::core::Expected<void> recover();

    // Persists the original stack before the live inventory and sidecar are
    // changed. `claim.loot_id` is also the stable, non-reusing claim id.
    [[nodiscard]] snt::core::Expected<void> begin_pickup(GameGroundLootPickupClaim claim);
    // Writes the player's temporary receipt with its current inventory state.
    // The caller keeps the journal if this fails because the failed file write
    // may still have reached durable storage.
    [[nodiscard]] snt::core::Expected<void> checkpoint_player_claim(
        const GameAuthenticatedPeer& peer, uint64_t loot_id);
    // Drops a prepared journal entry only while neither live owner changed.
    [[nodiscard]] snt::core::Expected<void> abandon_pickup(uint64_t loot_id);
    // Called after the live sidecar has removed the claimed record. It writes
    // that sidecar, clears the journal, then lets normal player autosave
    // compact the now-stale receipt.
    [[nodiscard]] snt::core::Expected<void> finalize_pickup(uint64_t loot_id);

    [[nodiscard]] size_t pending_claim_count() const noexcept { return claims_.size(); }

private:
    GameServerGroundLootPickupPersistence(
        std::string universe_save_dir, GameServerPlayerLifecycle& player_lifecycle,
        GameWorldPersistenceLifecycle& world_persistence, snt::voxel::ChunkRegistry& chunks,
        GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content) noexcept;

    [[nodiscard]] snt::core::Expected<void> validate_claim(
        const GameGroundLootPickupClaim& claim) const;
    [[nodiscard]] snt::core::Expected<void> persist_claims();
    [[nodiscard]] snt::core::Expected<void> persist_chunk(const ChunkKey& chunk);
    [[nodiscard]] snt::core::Expected<bool> player_has_durable_receipt(
        const GameGroundLootPickupClaim& claim) const;
    [[nodiscard]] snt::core::Expected<void> restore_world_record(
        const GameGroundLootPickupClaim& claim);
    [[nodiscard]] snt::core::Expected<void> remove_world_record(
        const GameGroundLootPickupClaim& claim);
    [[nodiscard]] snt::core::Expected<void> erase_claim(uint64_t loot_id);

    std::string universe_save_dir_;
    GameServerPlayerLifecycle* player_lifecycle_ = nullptr;
    GameWorldPersistenceLifecycle* world_persistence_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    const GameContentRegistry* content_ = nullptr;
    std::map<uint64_t, GameGroundLootPickupClaim> claims_;
    bool recovered_ = false;
};

}  // namespace snt::game::replication
