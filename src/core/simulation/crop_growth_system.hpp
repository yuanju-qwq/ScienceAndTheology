#pragma once

#include <string>
#include <vector>

#include "simulation_system.hpp"
#include "../world/block_entity.hpp"
#include "../world_gen/crop_species_def.hpp"

namespace science_and_theology {

// Crop growth simulation subsystem.
// Drives the growth of crop block entities from SEED → SPROUT → GROWING → MATURE.
//
// Design:
//   - Each tick, iterates crop block entities in active chunks.
//   - For each crop, checks if enough ticks have elapsed for the next
//     growth stage transition.
//   - Growth rate is modulated by:
//       * Farmland moisture/fertility (per-cell, from FARMLAND entity below)
//       * Ecosystem soil_fertility/water_availability (per-chunk, fallback)
//       * Season (off-season growth rate x0.3)
//       * Continuous-cropping penalty (same crop 3+ times: x0.5)
//   - When a crop grows, it replaces the terrain material to match the new
//     stage and updates the block entity's growth_stage.
//   - Also updates farmland moisture (evaporation over time).
//   - Sleeping chunks skip growth (crops only grow in active chunks).
//
// Priority: 9 (runs after TreeGrowth at 8).
// This ensures season is current (6), ecosystem is updated (7), and trees
// have been processed (8) before crops.
//
// Thread safety: main thread only. Not thread-safe.

class CropGrowthSystem : public SimulationSystem {
public:
    CropGrowthSystem() = default;

    SIMULATION_SYSTEM_NAME(CropGrowthSystem, "CropGrowthSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta,
                     const TickContext* ctx = nullptr) override;
    void tick_sleeping(const ChunkKey& chunk, float delta,
                       const TickContext* ctx = nullptr) override;
    void shutdown() override;

    // Runs after TreeGrowth (8). Ensures season and ecosystem are current.
    int priority() const override { return 9; }

    // Maximum number of growth transitions per tick.
    // Prevents frame spikes when many crops are ready simultaneously.
    static constexpr int kMaxGrowthPerTick = 64;

    // Maximum number of farmland moisture updates per tick.
    static constexpr int kMaxMoistureUpdatesPerTick = 128;

private:
    // Attempt to grow a single crop entity.
    // ctx: optional TickContext for ecosystem/season lookup.
    // Returns true if a growth transition occurred.
    bool try_grow_crop(EntityId entity_id,
                       const std::string& dimension_id,
                       int64_t current_tick,
                       const TickContext* ctx);

    // Check growth conditions for a crop at the given position.
    bool check_growth_conditions(
        const CropSpeciesDef& species,
        const std::string& dimension_id,
        int root_x, int root_y, int root_z,
        CropGrowthStage current_stage,
        const TickContext* ctx) const;

    // Apply a stage transition: update terrain material + entity state.
    void advance_crop_stage(
        EntityId entity_id,
        const CropSpeciesDef& species,
        const std::string& dimension_id,
        int root_x, int root_y, int root_z,
        CropGrowthStage new_stage,
        int64_t current_tick);

    // Update farmland moisture (evaporation) for farmland entities in chunk.
    void update_farmland_moisture(
        const ChunkKey& chunk, int64_t current_tick,
        const TickContext* ctx);

    // Helper: set a terrain cell at world coordinates.
    bool set_world_cell(
        const std::string& dimension_id,
        int world_x, int world_y, int world_z,
        TerrainMaterialId material, uint32_t flags);

    // Helper: read temperature/humidity at a world position.
    bool get_biome_at(int global_x, int global_y, int global_z,
                      float& out_temperature, float& out_humidity) const;

    // Helper: get the current season from TickContext.
    // Returns -1 if season system is unavailable.
    int current_season(const TickContext* ctx) const;

    int growth_count_ = 0;
    int moisture_update_count_ = 0;
};

} // namespace science_and_theology
