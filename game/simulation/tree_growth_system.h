// Deterministic game-owned tree growth.
//
// This system owns only mature/sapling tree state anchored in current chunk
// sidecars. It advances active, loaded trees and emits committed terrain
// snapshots. Placement, player inventory, climate and replication remain
// narrow external boundaries rather than dependencies on a legacy world
// singleton.

#pragma once

#include "core/expected.h"
#include "game/simulation/tree_growth_events.h"
#include "game/world/game_chunk.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/data/chunk_registry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace snt::game {

struct TreeSaplingPlacement {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    std::string species_key;
};

struct TreeGrowthLimits {
    uint32_t max_stage_transitions_per_tick = 32;
};

class GameTreeGrowthSystem final {
public:
    GameTreeGrowthSystem(snt::voxel::ChunkRegistry& chunks,
                         GameChunkSidecarRegistry& sidecars,
                         const WorldGenConfigSnapshot& worldgen_config,
                         TreeGrowthLimits limits = {}) noexcept;

    GameTreeGrowthSystem(const GameTreeGrowthSystem&) = delete;
    GameTreeGrowthSystem& operator=(const GameTreeGrowthSystem&) = delete;

    void set_mutation_sink(ITreeGrowthMutationSink* sink) noexcept {
        mutation_sink_ = sink;
    }
    void set_environment_provider(const ITreeGrowthEnvironmentProvider* provider) noexcept {
        environment_provider_ = provider;
    }

    // Creates one durable SAPLING tree anchor after terrain validation. The
    // caller owns reach, inventory, and item semantics; this system owns only
    // the current world mutation and typed growth record.
    [[nodiscard]] snt::core::Expected<EntityId> plant_sapling(
        const TreeSaplingPlacement& placement,
        uint64_t source_tick);

    // Advances a bounded, deterministic snapshot of active tree records.
    // Missing or sleeping chunks are never mutated.
    void tick(uint64_t current_tick);

private:
    [[nodiscard]] bool try_grow_tree(const ChunkKey& key,
                                     GameChunkSidecar& sidecar,
                                     TreeGrowthPersistenceRecord& record,
                                     uint64_t current_tick);
    [[nodiscard]] bool build_young_layout(const BlockEntityPlacement& anchor,
                                          TerrainMaterialId wood_material,
                                          TerrainMaterialId leaves_material,
                                          std::vector<TreeGrowthOwnedCell>& out_layout) const;
    [[nodiscard]] bool build_mature_layout(const BlockEntityPlacement& anchor,
                                           const TreeSpeciesDef& species,
                                           TerrainMaterialId wood_material,
                                           TerrainMaterialId leaves_material,
                                           std::vector<TreeGrowthOwnedCell>& out_layout) const;
    [[nodiscard]] bool can_apply_layout(
        std::string_view dimension_id,
        const TreeGrowthPersistenceRecord& record,
        const std::vector<TreeGrowthOwnedCell>& layout) const;
    [[nodiscard]] bool apply_layout(
        std::string_view dimension_id,
        BlockEntityPlacement& anchor,
        TreeGrowthPersistenceRecord& record,
        std::vector<TreeGrowthOwnedCell> layout,
        TreeGrowthStage new_stage,
        uint64_t current_tick);
    [[nodiscard]] bool write_cell(std::string_view dimension_id,
                                  int32_t block_x,
                                  int32_t block_y,
                                  int32_t block_z,
                                  TerrainMaterialId material);

    void emit_terrain_change(std::string_view dimension_id,
                             int32_t block_x,
                             int32_t block_y,
                             int32_t block_z,
                             const snt::voxel::TerrainCell& previous,
                             const snt::voxel::TerrainCell& current) const;

    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameChunkSidecarRegistry* sidecars_ = nullptr;
    const WorldGenConfigSnapshot* worldgen_config_ = nullptr;
    TreeGrowthLimits limits_;
    ITreeGrowthMutationSink* mutation_sink_ = nullptr;
    const ITreeGrowthEnvironmentProvider* environment_provider_ = nullptr;
};

}  // namespace snt::game
