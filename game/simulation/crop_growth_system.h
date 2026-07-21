// Deterministic game-owned crop and farmland simulation.
//
// Generic CROP and FARMLAND block anchors only establish identity and root
// coordinates. Typed chunk-sidecar records own mutable crop stages, soil
// state, timers, and rotation history. Inventory and player authority remain
// outside this module so the same system can run in any game host.

#pragma once

#include "core/expected.h"
#include "game/simulation/crop_growth_events.h"
#include "game/simulation/season_cycle.h"
#include "game/world/game_chunk.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/data/chunk_registry.h"

#include <cstdint>
#include <string>

namespace snt::game {

struct FarmlandTillingRequest {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    float initial_moisture = 0.5f;
    float initial_fertility = 0.7f;
};

struct CropPlantingRequest {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    std::string species_key;
};

struct CropHarvestRequest {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
};

struct CropHarvestResult {
    EntityId crop_anchor_id;
    std::string species_key;
    std::string crop_item_key;
    int32_t crop_count = 0;
    std::string byproduct_item_key;
    int32_t byproduct_count = 0;
    bool regrowing = false;
};

struct CropFertilizationRequest {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
};

struct CropGrowthLimits {
    uint32_t max_stage_transitions_per_tick = 64;
    uint32_t max_moisture_updates_per_tick = 128;
};

class GameCropGrowthSystem final {
public:
    GameCropGrowthSystem(snt::voxel::ChunkRegistry& chunks,
                         GameChunkSidecarRegistry& sidecars,
                         const WorldGenConfigSnapshot& worldgen_config,
                         CropGrowthLimits limits = {}) noexcept;

    GameCropGrowthSystem(const GameCropGrowthSystem&) = delete;
    GameCropGrowthSystem& operator=(const GameCropGrowthSystem&) = delete;

    void set_mutation_sink(ICropGrowthMutationSink* sink) noexcept {
        mutation_sink_ = sink;
    }
    void set_environment_provider(const ICropGrowthEnvironmentProvider* provider) noexcept {
        environment_provider_ = provider;
    }

    // These mutations own only terrain and typed sidecar state. The caller
    // must complete reach, inventory, and item-grant transactions before or
    // after invoking the matching operation as appropriate.
    [[nodiscard]] snt::core::Expected<EntityId> till_farmland(
        const FarmlandTillingRequest& request,
        uint64_t source_tick);
    [[nodiscard]] snt::core::Expected<EntityId> plant_crop(
        const CropPlantingRequest& request,
        uint64_t source_tick);
    [[nodiscard]] snt::core::Expected<CropHarvestResult> harvest_crop(
        const CropHarvestRequest& request,
        uint64_t source_tick);
    // Resolves the exact mature-crop yield without mutating terrain or
    // sidecars. Player interaction services use this to prove that a harvest
    // inventory transaction can fit before either side of the operation is
    // committed.
    [[nodiscard]] snt::core::Expected<CropHarvestResult> preview_harvest_crop(
        const CropHarvestRequest& request,
        uint64_t source_tick);
    [[nodiscard]] snt::core::Expected<CropGrowthStage> fertilize_crop(
        const CropFertilizationRequest& request,
        uint64_t source_tick);

    // Advances a bounded deterministic snapshot of active, loaded records.
    // The authoritative SeasonCycle owns the season argument; missing climate
    // samples never fall back to the legacy pseudo-noise model.
    void tick(uint64_t current_tick, Season current_season);

private:
    [[nodiscard]] bool try_grow_crop(const ChunkKey& key,
                                     GameChunkSidecar& sidecar,
                                     CropGrowthPersistenceRecord& record,
                                     uint64_t current_tick,
                                     Season current_season);
    [[nodiscard]] bool update_farmland_moisture(const ChunkKey& key,
                                                GameChunkSidecar& sidecar,
                                                FarmlandPersistenceRecord& record,
                                                uint64_t current_tick);
    [[nodiscard]] bool set_crop_stage(std::string_view dimension_id,
                                      BlockEntityPlacement& anchor,
                                      CropGrowthPersistenceRecord& record,
                                      const CropSpeciesDef& species,
                                      CropGrowthStage new_stage,
                                      uint64_t current_tick,
                                      bool allow_stage_regression = false);
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
    CropGrowthLimits limits_;
    ICropGrowthMutationSink* mutation_sink_ = nullptr;
    const ICropGrowthEnvironmentProvider* environment_provider_ = nullptr;
};

}  // namespace snt::game
