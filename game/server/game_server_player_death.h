// Dedicated-server bed, grave, death, and respawn services.
//
// This module owns only server-main-thread value transactions. Chunk sidecars
// carry durable bed/grave state, GameServerPlayerState owns live inventory and
// position, and future combat/block-edit command services call these APIs
// after their own authority and reach validation succeeds.

#pragma once

#include "core/expected.h"
#include "game/player/player_death.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_movement.h"
#include "game/world/game_chunk.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace snt::voxel {
class ChunkRegistry;
class VoxelChunk;
}

namespace snt::game::replication {

class GameServerPlayerState;

struct GameServerPlayerGraveConfig {
    uint32_t grave_material_id = 255;
    uint32_t air_material_id = 0;
    uint32_t vertical_search_blocks = 32;
};

// World-side grave implementation. Each record is anchored in a chunk
// sidecar and mirrored by a CUSTOM block-entity placement plus an
// indestructible terrain cell. It is deliberately not exposed to transport.
class GameServerPlayerGraveStore final : public IGamePlayerGraveStore {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerGraveStore>> create(
        snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
        GameServerPlayerGraveConfig config = {});

    GameServerPlayerGraveStore(const GameServerPlayerGraveStore&) = delete;
    GameServerPlayerGraveStore& operator=(const GameServerPlayerGraveStore&) = delete;

    [[nodiscard]] snt::core::Expected<GamePlayerGraveId> create_indestructible_grave(
        const GamePlayerGraveCreateRequest& request) override;
    [[nodiscard]] snt::core::Expected<GamePlayerGraveContents> read_grave(
        GamePlayerGraveId id, const GamePlayerGraveAccess& access) const override;
    [[nodiscard]] snt::core::Expected<void> erase_grave(
        GamePlayerGraveId id, const GamePlayerGraveAccess& access) override;

    [[nodiscard]] size_t active_grave_count() const noexcept;

private:
    struct GraveLocation {
        ChunkKey chunk_key;
        GameChunkSidecar* sidecar = nullptr;
        size_t record_index = 0;
    };

    struct PlacementCandidate {
        ChunkKey chunk_key;
        GamePlayerWorldPosition position;
        int local_x = 0;
        int local_y = 0;
        int local_z = 0;
    };

    GameServerPlayerGraveStore(snt::voxel::ChunkRegistry& chunks,
                               GameChunkSidecarRegistry& sidecars,
                               GameServerPlayerGraveConfig config,
                               uint64_t next_grave_serial);

    [[nodiscard]] static snt::core::Expected<uint64_t> initial_grave_serial(
        const GameChunkSidecarRegistry& sidecars);
    [[nodiscard]] static ChunkKey chunk_key_for(const GamePlayerWorldPosition& position);
    [[nodiscard]] static bool is_nonempty_stack(const GamePlayerItemStack& stack) noexcept;
    [[nodiscard]] snt::core::Expected<PlacementCandidate> find_placement(
        const GamePlayerWorldPosition& death_position) const;
    [[nodiscard]] snt::core::Expected<GraveLocation> locate_grave(GamePlayerGraveId id) const;
    [[nodiscard]] snt::core::Expected<GamePlayerGraveContents> read_located_grave(
        const GraveLocation& location, const GamePlayerGraveAccess& access) const;
    [[nodiscard]] GameChunkSidecar* mutable_sidecar_for(const ChunkKey& key);

    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    GameServerPlayerGraveConfig config_;
    uint64_t next_grave_serial_ = 1;
};

// Bed records are registered/removed by the future authoritative block-edit
// transaction. The service owns player respawn-point changes so a caller
// cannot store an arbitrary client-provided coordinate.
class GameServerPlayerBedService final : public IGamePlayerBedLocator {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerBedService>> create(
        GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
        GameChunkSidecarRegistry& sidecars,
        IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr);

    GameServerPlayerBedService(const GameServerPlayerBedService&) = delete;
    GameServerPlayerBedService& operator=(const GameServerPlayerBedService&) = delete;

    [[nodiscard]] snt::core::Expected<void> on_bed_placed(
        const GamePlayerWorldPosition& position);
    [[nodiscard]] snt::core::Expected<void> on_bed_removed(
        const GamePlayerWorldPosition& position);
    [[nodiscard]] snt::core::Expected<void> set_respawn_point_from_bed(
        const GameAuthenticatedPeer& peer, const GamePlayerWorldPosition& bed_position);
    [[nodiscard]] snt::core::Expected<void> clear_respawn_point(
        const GameAuthenticatedPeer& peer);
    [[nodiscard]] snt::core::Expected<bool> has_bed_at(
        const GamePlayerWorldPosition& position) const override;

private:
    GameServerPlayerBedService(GameServerPlayerState& player_state,
                               const snt::voxel::ChunkRegistry& chunks,
                               GameChunkSidecarRegistry& sidecars,
                               IGameServerPlayerStateCheckpointSink* checkpoint_sink);

    [[nodiscard]] static ChunkKey chunk_key_for(const GamePlayerWorldPosition& position);

    GameServerPlayerState* player_state_ = nullptr;
    const snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
};

struct GameServerPlayerRespawnConfig {
    GamePlayerWorldPosition world_spawn{
        .dimension_id = "overworld",
        .position = {},
    };
    uint32_t world_spawn_search_radius_blocks = 16;
};

// Resolves valid bed anchors first. A missing bed falls back to a safe world
// spawn; an existing but blocked/unloaded bed remains valid and returns its
// canonical nearby cell, as required by the frozen product rule.
class GameServerPlayerRespawnResolver final : public IGamePlayerRespawnResolver {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerRespawnResolver>>
    create(const snt::voxel::ChunkRegistry& chunks, const IGamePlayerBedLocator& beds,
           GameServerPlayerRespawnConfig config = {});

    GameServerPlayerRespawnResolver(const GameServerPlayerRespawnResolver&) = delete;
    GameServerPlayerRespawnResolver& operator=(const GameServerPlayerRespawnResolver&) = delete;

    [[nodiscard]] snt::core::Expected<GamePlayerWorldPosition> resolve_respawn(
        std::string_view account_id,
        const std::optional<GamePlayerWorldPosition>& saved_respawn_point) override;

private:
    GameServerPlayerRespawnResolver(const snt::voxel::ChunkRegistry& chunks,
                                    const IGamePlayerBedLocator& beds,
                                    GameServerPlayerRespawnConfig config);

    [[nodiscard]] bool is_safe_feet_position(const GamePlayerWorldPosition& position) const;
    [[nodiscard]] std::optional<GamePlayerWorldPosition> find_safe_near(
        const GamePlayerWorldPosition& anchor, uint32_t radius) const;

    const snt::voxel::ChunkRegistry* chunks_ = nullptr;
    const IGamePlayerBedLocator* beds_ = nullptr;
    GameServerPlayerRespawnConfig config_;
};

// Executes the death transaction in explicit order: snapshot, preflight
// respawn, create grave, clear inventory, move player, reset motion. Equipment
// and organ values remain in GameServerPlayerState throughout.
class GameServerPlayerDeathService final {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerDeathService>>
    create(GameServerPlayerState& player_state, IGamePlayerGraveStore& grave_store,
           IGamePlayerRespawnResolver& respawn_resolver,
           IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr,
           IGameServerPlayerMotionReset* motion_reset = nullptr);

    GameServerPlayerDeathService(const GameServerPlayerDeathService&) = delete;
    GameServerPlayerDeathService& operator=(const GameServerPlayerDeathService&) = delete;

    [[nodiscard]] snt::core::Expected<GamePlayerDeathResult> resolve_death(
        const GameAuthenticatedPeer& peer, uint64_t death_tick);
    [[nodiscard]] snt::core::Expected<GamePlayerGraveClaimResult> reclaim_grave(
        const GameAuthenticatedPeer& peer, GamePlayerGraveId grave_id,
        bool is_administrator = false);

private:
    GameServerPlayerDeathService(GameServerPlayerState& player_state,
                                 IGamePlayerGraveStore& grave_store,
                                 IGamePlayerRespawnResolver& respawn_resolver,
                                 IGameServerPlayerStateCheckpointSink* checkpoint_sink,
                                 IGameServerPlayerMotionReset* motion_reset);

    [[nodiscard]] static std::vector<GamePlayerItemStack> nonempty_inventory_items(
        const GamePlayerInventory& inventory);
    [[nodiscard]] snt::core::Expected<void> restore_inventory_after_failed_death(
        const GameAuthenticatedPeer& peer,
        const std::vector<GamePlayerItemStack>& items,
        GamePlayerGraveId grave_id);

    GameServerPlayerState* player_state_ = nullptr;
    IGamePlayerGraveStore* grave_store_ = nullptr;
    IGamePlayerRespawnResolver* respawn_resolver_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    IGameServerPlayerMotionReset* motion_reset_ = nullptr;
};

}  // namespace snt::game::replication
