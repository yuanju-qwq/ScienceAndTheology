// Game-owned terrain and machine replication payloads.
//
// The generic SNTG envelope owns chunk keys, entity ids, ordering, and
// budgets. This module owns the current game payloads carried inside those
// opaque records, plus headless client-side caches. It intentionally exposes
// presentation terrain and machine state only; inventory ownership, machine
// job owners, and persistent sidecars remain authoritative server data.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "voxel/data/chunk_registry.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {

struct GameChunkKeyLess final {
    [[nodiscard]] bool operator()(const snt::voxel::ChunkKey& left,
                                  const snt::voxel::ChunkKey& right) const noexcept;
};

// Terrain snapshots deliberately omit fluids. Current P7 block interaction
// commits only material and flags, which are exactly the fields carried by
// GameBlockDelta. A future fluid replication payload can be introduced as a
// new latest-only chunk payload version instead of overloading this format.
struct GameReplicatedTerrainCell {
    snt::voxel::TerrainMaterialId material = 0;
    uint32_t flags = 0;
};

struct GameReplicatedTerrainChunk {
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t size_z = 0;
    std::vector<GameReplicatedTerrainCell> cells;
};

[[nodiscard]] bool same_game_replicated_terrain_cell(
    const GameReplicatedTerrainCell& left,
    const GameReplicatedTerrainCell& right) noexcept;

[[nodiscard]] snt::core::Expected<GameReplicatedTerrainChunk>
make_game_replicated_terrain_chunk(const snt::voxel::VoxelChunk& chunk);
[[nodiscard]] snt::core::Expected<snt::voxel::VoxelChunk>
make_voxel_chunk_from_game_replicated_terrain(
    const snt::voxel::ChunkKey& key,
    const GameReplicatedTerrainChunk& terrain);

[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_terrain_chunk_snapshot(const snt::voxel::VoxelChunk& chunk);
[[nodiscard]] snt::core::Expected<GameReplicatedTerrainChunk>
decode_game_terrain_chunk_snapshot(std::span<const std::byte> payload);

inline constexpr uint8_t kGameMachineReplicationEntityKind = 2;
inline constexpr uint8_t kGameMachineReplicationEntityVersion = 2;

enum class GameMachineReplicationOperation : uint8_t {
    kUpsert = 1,
    kRemove = 2,
};

struct GameReplicatedMachineItemStack {
    std::string item_id;
    int32_t count = 0;
};

// The client needs enough state to render a machine and later build a machine
// UI. Authenticated job ownership, source recipe inputs, and ECS handles do
// not cross this boundary.
struct GameReplicatedMachineState {
    snt::voxel::ChunkKey anchor_chunk;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    std::string machine_id;
    std::vector<GameReplicatedMachineItemStack> input_slots;
    std::vector<GameReplicatedMachineItemStack> output_slots;
    uint8_t max_input_slots = 4;
    uint8_t max_output_slots = 4;
    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t progress_ticks = 0;
    int32_t active_recipe_duration_ticks = 0;
    uint8_t run_state = 0;
};

struct GameMachineReplicationEntity {
    GameMachineReplicationOperation operation = GameMachineReplicationOperation::kUpsert;
    std::optional<GameReplicatedMachineState> machine;
};

[[nodiscard]] bool is_game_machine_replication_entity_payload(
    std::span<const std::byte> payload) noexcept;
[[nodiscard]] snt::core::Expected<std::vector<std::byte>>
encode_game_machine_replication_entity(const GameMachineReplicationEntity& entity);
[[nodiscard]] snt::core::Expected<GameMachineReplicationEntity>
decode_game_machine_replication_entity(std::span<const std::byte> payload);

// Applies server terrain snapshots/deltas to the client simulation chunk
// registry. Before the first server overwrite of a chunk it keeps the local
// bootstrap copy, so a disconnect restores the standalone presentation world
// instead of retaining stale data from a former server session.
class GameClientRemoteChunkWorld final {
public:
    explicit GameClientRemoteChunkWorld(snt::voxel::ChunkRegistry& chunks) noexcept;

    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] size_t chunk_count() const noexcept { return active_chunks_.size(); }
    [[nodiscard]] std::vector<snt::voxel::ChunkKey> drain_dirty_chunks();
    void clear() noexcept;

private:
    using ChunkSet = std::map<snt::voxel::ChunkKey, bool, GameChunkKeyLess>;
    using OriginalChunkMap = std::map<snt::voxel::ChunkKey,
                                      std::optional<snt::voxel::VoxelChunk>,
                                      GameChunkKeyLess>;

    [[nodiscard]] snt::core::Expected<void> apply_chunk_snapshot(
        const GameChunkSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> restore_chunk(
        const snt::voxel::ChunkKey& key);
    void mark_dirty(const snt::voxel::ChunkKey& key);

    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    ChunkSet active_chunks_;
    OriginalChunkMap original_chunks_;
    std::map<snt::voxel::ChunkKey, bool, GameChunkKeyLess> dirty_chunks_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

struct GameRemoteMachineState {
    snt::ecs::EntityGuid entity_guid;
    GameReplicatedMachineState machine;
};

// A value-only cache for later machine presentation/UI consumers. It is not
// an ECS owner and cannot mutate the authoritative machine runtime.
class GameRemoteMachineWorld final {
public:
    [[nodiscard]] snt::core::Expected<void> apply(const GameSnapshot& snapshot);
    [[nodiscard]] snt::core::Expected<void> apply(const GameDelta& delta);

    [[nodiscard]] uint64_t active_snapshot_id() const noexcept { return active_snapshot_id_; }
    [[nodiscard]] size_t machine_count() const noexcept { return machines_.size(); }
    [[nodiscard]] std::vector<GameRemoteMachineState> machines() const;
    // Returns a value copy so presentation callers never retain cache-backed
    // references across a later replication update.
    [[nodiscard]] std::optional<GameRemoteMachineState> find_machine_at(
        std::string_view dimension_id, int32_t root_x, int32_t root_y, int32_t root_z) const;
    void clear() noexcept;

private:
    [[nodiscard]] snt::core::Expected<void> apply_entity(
        snt::ecs::EntityGuid entity_guid,
        const GameMachineReplicationEntity& entity);

    std::map<uint64_t, GameRemoteMachineState> machines_;
    uint64_t active_snapshot_id_ = 0;
    uint64_t last_delta_sequence_ = 0;
};

}  // namespace snt::game::replication
