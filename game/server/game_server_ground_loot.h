// Dedicated-server ground-loot authority.
//
// Chunk sidecars own durable item records. This module owns their non-reusing
// ids, atomic batch creation, authoritative reach checks, and inventory
// pickup transactions without exposing either world mutation to transport.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game {
class GameContentRegistry;
}

namespace snt::game::replication {

class GameServerPlayerState;
class IGameServerPlayerStateCheckpointSink;
class GameServerGroundLootPickupPersistence;

struct GameGroundLootSpawnRequest {
    ChunkKey chunk;
    ResourceContentStack resource;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    uint64_t spawned_tick = 0;
};

// A replication source observes durable mutations only after their batch has
// committed. It retains no world ownership and can safely coalesce ticks.
class IGameServerGroundLootStateSink {
public:
    virtual ~IGameServerGroundLootStateSink() = default;

    virtual void on_ground_loot_state_changed(uint64_t source_tick) noexcept = 0;
};

class IGameServerGroundLootSpawner {
public:
    virtual ~IGameServerGroundLootSpawner() = default;

    // Preflight is intentionally separate from commit so a creature kill can
    // reject an invalid/full drop batch before it removes the wild proxy.
    [[nodiscard]] virtual snt::core::Expected<void> can_spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest> requests) const = 0;
    [[nodiscard]] virtual snt::core::Expected<std::vector<uint64_t>> spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest> requests) = 0;
};

class IGameServerGroundLootInteractionService {
public:
    virtual ~IGameServerGroundLootInteractionService() = default;

    [[nodiscard]] virtual snt::core::Expected<void> pickup_ground_loot(
        const GameAuthenticatedPeer& peer, const GameGroundLootPickupCommand& command,
        uint64_t source_tick) = 0;
};

struct GameServerGroundLootConfig {
    uint32_t max_loot_per_chunk = kMaxGameGroundLootRecordsPerChunk;
    // Zero preserves a permanent world drop. A positive value expires a
    // record once its persisted terrain-resident age reaches this tick count.
    uint64_t despawn_after_ticks = 0;
    // Bounds scans over the durable sidecar index. Expiration is evaluated
    // only for terrain-resident records, so unloaded chunks remain cheap and
    // do not consume lifetime until materialized again.
    uint64_t lifecycle_sweep_interval_ticks = 20;
};

class GameServerGroundLootService final
    : public IGameServerGroundLootSpawner,
      public IGameServerGroundLootInteractionService {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerGroundLootService>>
    create(GameServerPlayerState& player_state, snt::voxel::ChunkRegistry& chunks,
           GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content,
           IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr,
           IGameServerGroundLootStateSink* state_sink = nullptr,
           GameServerGroundLootConfig config = {},
           GameServerGroundLootPickupPersistence* pickup_persistence = nullptr);

    GameServerGroundLootService(const GameServerGroundLootService&) = delete;
    GameServerGroundLootService& operator=(const GameServerGroundLootService&) = delete;

    [[nodiscard]] snt::core::Expected<void> can_spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest> requests) const override;
    [[nodiscard]] snt::core::Expected<std::vector<uint64_t>> spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest> requests) override;
    [[nodiscard]] snt::core::Expected<void> pickup_ground_loot(
        const GameAuthenticatedPeer& peer, const GameGroundLootPickupCommand& command,
        uint64_t source_tick) override;
    // Advances server-owned expiry for terrain-resident records. The caller
    // supplies the same authoritative tick used for commands and replication.
    [[nodiscard]] snt::core::Expected<size_t> tick(uint64_t source_tick);

    [[nodiscard]] size_t active_loot_count() const noexcept;
    [[nodiscard]] uint64_t next_ground_loot_serial() const noexcept {
        return next_ground_loot_serial_;
    }

private:
    struct GroundLootLocation {
        ChunkKey chunk;
        GameChunkSidecar* sidecar = nullptr;
        size_t record_index = 0;
    };

    GameServerGroundLootService(GameServerPlayerState& player_state,
                                snt::voxel::ChunkRegistry& chunks,
                                GameChunkSidecarRegistry& sidecars,
                                const GameContentRegistry& content,
                                IGameServerPlayerStateCheckpointSink* checkpoint_sink,
                                IGameServerGroundLootStateSink* state_sink,
                                GameServerGroundLootConfig config,
                                GameServerGroundLootPickupPersistence* pickup_persistence,
                                uint64_t next_ground_loot_serial) noexcept;

    [[nodiscard]] static snt::core::Expected<uint64_t> initial_ground_loot_serial(
        const GameChunkSidecarRegistry& sidecars, const GameContentRegistry& content);
    [[nodiscard]] snt::core::Expected<void> validate_spawn_request(
        const GameGroundLootSpawnRequest& request) const;
    [[nodiscard]] snt::core::Expected<GroundLootLocation> locate_ground_loot(
        uint64_t loot_id) const;
    [[nodiscard]] GameChunkSidecar* mutable_sidecar_for(const ChunkKey& chunk);

    GameServerPlayerState* player_state_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    const GameContentRegistry* content_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    IGameServerGroundLootStateSink* state_sink_ = nullptr;
    GameServerGroundLootPickupPersistence* pickup_persistence_ = nullptr;
    GameServerGroundLootConfig config_;
    uint64_t next_ground_loot_serial_ = 1;
    uint64_t last_lifecycle_sweep_tick_ = 0;
    bool has_last_lifecycle_sweep_tick_ = false;
    // This session-local baseline turns the process tick stream into deltas
    // added to each record's durable lifetime_ticks. Unknown records are
    // intentionally baselined at first observation after a restart.
    std::map<uint64_t, uint64_t> lifetime_last_observed_tick_;
};

}  // namespace snt::game::replication
