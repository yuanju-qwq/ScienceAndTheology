#pragma once

#include <string>
#include <vector>

#include "simulation_system.hpp"
#include "../world/block_entity.hpp"
#include "../world_gen/tree_species_def.hpp"

namespace science_and_theology {

// Tree growth simulation subsystem.
// Drives the growth of tree block entities from SAPLING → YOUNG → MATURE.
//
// Design:
//   - Each tick, iterates tree block entities in active chunks.
//   - For each tree, checks if enough ticks have elapsed for the next
//     growth stage transition.
//   - Growth conditions: sufficient vertical space, valid ground material,
//     and temperature/humidity within the species' range.
//   - When a tree grows, it replaces terrain blocks (sapling → trunk + canopy)
//     and updates the block entity's owned_cells and growth_stage.
//   - Sleeping chunks skip growth (trees only grow in active chunks).
//
// Priority: 5 (runs after BlockPhysics at 0, Machine at 1).
// This ensures block physics has settled before growth modifies terrain.
//
// Thread safety: main thread only. Not thread-safe.

class TreeGrowthSystem : public SimulationSystem {
public:
    TreeGrowthSystem() = default;

    SIMULATION_SYSTEM_NAME(TreeGrowthSystem, "TreeGrowthSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    // Runs after DayNight (0), BlockPhysics (1), Machine (2), Season (6).
    // This ensures block physics has settled and season is current
    // before growth modifies terrain.
    int priority() const override { return 7; }

    // Maximum number of growth transitions per tick.
    // Prevents frame spikes when many trees are ready simultaneously.
    static constexpr int kMaxGrowthPerTick = 32;

private:
    // Attempt to grow a single tree entity.
    // Returns true if a growth transition occurred.
    bool try_grow_tree(EntityId entity_id,
                       const std::string& dimension_id,
                       int64_t current_tick);

    // Check growth conditions for a tree at the given position.
    // Returns true if the tree can grow to the next stage.
    bool check_growth_conditions(
        const TreeSpeciesDef& species,
        const std::string& dimension_id,
        int root_x, int root_y, int root_z,
        TreeGrowthStage current_stage) const;

    // Apply the SAPLING → YOUNG transition.
    // Places a short trunk and small canopy.
    void grow_sapling_to_young(
        EntityId entity_id,
        const TreeSpeciesDef& species,
        const std::string& dimension_id,
        int root_x, int root_y, int root_z,
        int64_t current_tick);

    // Apply the YOUNG → MATURE transition.
    // Extends the trunk and places a full canopy.
    void grow_young_to_mature(
        EntityId entity_id,
        const TreeSpeciesDef& species,
        const std::string& dimension_id,
        int root_x, int root_y, int root_z,
        int64_t current_tick);

    // Helper: set a terrain cell at world coordinates.
    // Converts world coords to chunk + local coords and writes the cell.
    bool set_world_cell(
        const std::string& dimension_id,
        int world_x, int world_y, int world_z,
        TerrainMaterialId material, uint32_t flags);

    // Helper: read temperature/humidity at a world position.
    // Returns false if the world data is unavailable.
    bool get_biome_at(int global_x, int global_y, int global_z,
                      float& out_temperature, float& out_humidity) const;

    int growth_count_ = 0;
};

} // namespace science_and_theology
