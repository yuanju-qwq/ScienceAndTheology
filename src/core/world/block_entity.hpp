#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "entity_data.hpp"
#include "simulation/creature_species.hpp"

namespace science_and_theology {

// ============================================================
// BlockEntity — per-block runtime state attached to a voxel cell
// ============================================================
//
// Normal terrain cells only store material + flags. When a block needs
// additional runtime state (e.g. tree growth stage, machine inventory,
// furnace progress), a BlockEntity is attached to that cell position.
//
// Ownership:
//   - Each BlockEntity is owned by the chunk containing its root cell
//     (the "anchor" position, typically the bottom-most block).
//   - A BlockEntity may reference additional owned cells in the same or
//     neighboring chunks (e.g. a tree's trunk + canopy).
//   - The BlockEntityRegistry provides global lookup by EntityId and
//     spatial lookup by world position.
//
// Serialization:
//   - BlockEntityPlacement is stored in ChunkData for save/load.
//   - Full runtime state is reconstructed from placement data on load.

// Growth stage for tree-type block entities.
enum class TreeGrowthStage : uint8_t {
    SAPLING     = 0,
    YOUNG       = 1,
    MATURE      = 2,
    COUNT       = 3,
};

constexpr const char* kTreeGrowthStageNames[] = {
    "Sapling", "Young", "Mature",
};

// Type discriminator for block entity variants.
enum class BlockEntityType : uint8_t {
    NONE        = 0,
    TREE        = 1,
    MACHINE     = 2,
    CREATURE    = 3,
    COUNT       = 4,
};

constexpr const char* kBlockEntityTypeNames[] = {
    "None", "Tree", "Machine", "Creature",
};

// A cell coordinate owned by a BlockEntity.
// Can reference cells in any chunk; the chunk is implied by
// world coordinates (block_x / kChunkSize, etc.).
struct OwnedCell {
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
};

// Lightweight placement data stored in ChunkData for serialization.
// Contains enough information to reconstruct the full runtime entity.
struct BlockEntityPlacement {
    EntityId id;
    BlockEntityType entity_type = BlockEntityType::NONE;

    // Root cell position (world coordinates).
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;

    // Type-specific data encoded as key-value pairs.
    // TREE:     { "species_key": str, "growth_stage": uint8, "planted_tick": int64 }
    // MACHINE:  { "machine_type": str, "facing": uint8, ... }
    // This keeps the placement struct generic and extensible.
    std::string type_data_json;

    // Number of owned cells (for serialization bounds checking).
    uint32_t owned_cell_count = 0;
};

// Full runtime state for a tree block entity.
// Reconstructed from BlockEntityPlacement on chunk load.
struct TreeBlockEntityState {
    std::string species_key;
    TreeGrowthStage growth_stage = TreeGrowthStage::SAPLING;
    int64_t planted_tick = 0;
    int64_t last_growth_tick = 0;
    std::vector<OwnedCell> owned_cells;
};

// ============================================================
// CreatureBlockEntityState — proxy creature entity for ecosystem
// ============================================================
//
// Proxy creatures are visual representations of population density.
// They are spawned in active chunks based on PopulationCell density
// and despawned when the chunk goes to sleep.
//
// AI states:
//   IDLE     — standing still, waiting for next action
//   WANDERING — moving toward a random target within the chunk
//   FLEEING   — running away from a nearby predator (herbivores only)

enum class CreatureState : uint8_t {
    IDLE      = 0,
    WANDERING = 1,
    FLEEING   = 2,
    COUNT     = 3,
};

constexpr const char* kCreatureStateNames[] = {
    "Idle", "Wandering", "Fleeing",
};

struct CreatureBlockEntityState {
    // Species identifier (references CreatureSpeciesRegistry).
    // 0 = invalid, must be set before first tick.
    uint16_t species_id = 0;

    // Cached behavioral role. Set at spawn time from species definition.
    // Avoids per-tick registry lookups in the AI hot path.
    CreatureRole creature_role = CreatureRole::HERBIVORE;

    CreatureState ai_state = CreatureState::IDLE;
    float health = 1.0f;
    int64_t spawn_tick = 0;

    // Current position (world block coordinates).
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;

    // Wander target position.
    float wander_target_x = 0.0f;
    float wander_target_y = 0.0f;
    float wander_target_z = 0.0f;
    int64_t next_wander_tick = 0;

    // Flee target: position to run away from.
    float flee_from_x = 0.0f;
    float flee_from_y = 0.0f;
    float flee_from_z = 0.0f;
    int64_t flee_end_tick = 0;
};

} // namespace science_and_theology
