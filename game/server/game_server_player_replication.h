// Dedicated-server player, terrain, and machine replication composition.
//
// This service derives every outbound value from authoritative game state. It
// owns each peer's latest snapshot baseline, listens to committed block
// interactions for sparse terrain changes, and never accepts client terrain,
// machine, position, or inventory state.

#pragma once

#include "core/expected.h"
#include "game/network/game_chunk_replication.h"
#include "game/network/game_replication_services.h"
#include "game/player/player_replication.h"
#include "game/server/game_server_player_interaction.h"
#include "game/server/game_server_player_state.h"
#include "game/simulation/block_physics_events.h"
#include "game/simulation/crop_growth_events.h"
#include "game/simulation/tree_growth_events.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace snt::ecs {
class World;
}

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game {
class GameChunkSidecarRegistry;
}

namespace snt::game::replication {

struct GameServerPlayerReplicationConfig {
    uint32_t horizontal_aoi_radius_blocks = 96;
    uint32_t vertical_aoi_radius_blocks = 48;
    uint32_t max_visible_players = 64;
    uint32_t chunk_horizontal_aoi_radius_blocks = 64;
    uint32_t chunk_vertical_aoi_radius_blocks = 64;
    uint32_t max_visible_chunks = 8;
};

class GameServerPlayerReplication final : public IGameReplicationInterestProvider,
                                          public IGameReplicationSnapshotSource,
                                          public IGameServerPlayerInteractionEventSink,
                                          public IBlockPhysicsMutationSink,
                                          public ICropGrowthMutationSink,
                                          public ITreeGrowthMutationSink {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerReplication>> create(
        GameServerPlayerState& player_state, snt::ecs::World& world,
        snt::voxel::ChunkRegistry& chunks, GameChunkSidecarRegistry& sidecars,
        GameServerPlayerReplicationConfig config = {},
        std::vector<IGameReplicationValueSource*> value_sources = {});

    GameServerPlayerReplication(const GameServerPlayerReplication&) = delete;
    GameServerPlayerReplication& operator=(const GameServerPlayerReplication&) = delete;

    [[nodiscard]] snt::core::Expected<GameReplicationInterest> compute_interest(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationMessage>> build_initial_snapshot(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationMessage>> build_deltas(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

    // Interaction services call this after their host-owned commit. Other
    // server systems that change terrain must use the same declaration rather
    // than writing an opaque delta or polling every loaded chunk.
    void on_player_interaction(const GameServerPlayerInteractionEvent& event) override;
    void on_block_physics_terrain_changed(const BlockPhysicsTerrainChange& change) override;
    void on_crop_growth_terrain_changed(const CropGrowthTerrainChange& change) override;
    void on_tree_growth_terrain_changed(const TreeGrowthTerrainChange& change) override;
    void mark_block_dirty(std::string_view dimension_id, int32_t block_x,
                          int32_t block_y, int32_t block_z) noexcept;

private:
    struct VisiblePlayer {
        snt::ecs::EntityGuid entity_guid;
        GameReplicatedPlayerState state;
    };

    struct VisibleChunk {
        snt::voxel::ChunkKey key;
        GameReplicatedTerrainChunk terrain;
        std::vector<std::byte> payload;
    };

    struct VisibleMachine {
        snt::ecs::EntityGuid entity_guid;
        GameReplicatedMachineState state;
    };

    struct PeerBaseline {
        uint64_t snapshot_id = 0;
        uint64_t next_delta_sequence = 1;
        std::map<uint64_t, GameReplicatedPlayerState> players;
        std::map<snt::voxel::ChunkKey, GameReplicatedTerrainChunk, GameChunkKeyLess> chunks;
        std::map<uint64_t, GameReplicatedMachineState> machines;
        std::map<uint8_t, GameReplicationValue> values;
    };

    using DirtyBlocks = std::map<snt::voxel::ChunkKey,
                                 std::map<uint16_t, GameBlockDelta>,
                                 GameChunkKeyLess>;

    GameServerPlayerReplication(GameServerPlayerState& player_state, snt::ecs::World& world,
                                snt::voxel::ChunkRegistry& chunks,
                                GameChunkSidecarRegistry& sidecars,
                                GameServerPlayerReplicationConfig config,
                                std::vector<IGameReplicationValueSource*> value_sources);

    [[nodiscard]] snt::core::Expected<std::vector<VisiblePlayer>> visible_players(
        const GameReplicationInterest& interest, const GameReplicationBudget& budget) const;
    [[nodiscard]] snt::core::Expected<std::vector<VisibleChunk>> visible_chunks(
        const GameReplicationInterest& interest) const;
    [[nodiscard]] snt::core::Expected<std::vector<VisibleMachine>> visible_machines(
        const GameReplicationInterest& interest) const;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context,
        GameReplicationValueCollectionPhase phase) const;
    void notify_values_committed(const GameAuthenticatedPeer& peer,
                                 GameReplicationValueCollectionPhase phase,
                                 std::span<const GameReplicationValue> values) noexcept;
    [[nodiscard]] static snt::core::Expected<GameReplicatedPlayerState> make_player_state(
        const GameServerPlayerSnapshot& snapshot);
    [[nodiscard]] static bool same_player_state(const GameReplicatedPlayerState& left,
                                                const GameReplicatedPlayerState& right) noexcept;
    [[nodiscard]] static bool same_machine_state(const GameReplicatedMachineState& left,
                                                 const GameReplicatedMachineState& right) noexcept;
    [[nodiscard]] static bool same_replication_value(const GameReplicationValue& left,
                                                      const GameReplicationValue& right) noexcept;
    void prune_dirty_blocks() noexcept;

    GameServerPlayerState* player_state_ = nullptr;
    snt::ecs::World* world_ = nullptr;
    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    GameServerPlayerReplicationConfig config_;
    std::vector<IGameReplicationValueSource*> value_sources_;
    uint64_t next_snapshot_id_ = 1;
    std::map<snt::network::PeerId, PeerBaseline> peer_baselines_;
    DirtyBlocks dirty_blocks_;
};

}  // namespace snt::game::replication
