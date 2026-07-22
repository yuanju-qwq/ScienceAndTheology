// BlockEntity — per-block runtime state attached to a voxel cell.
//
// Ported from src/core/world/block_entity.hpp.
// Namespace: science_and_theology -> snt::game.
// gt sub-namespace merged into snt::game (gt::PipeType -> PipeType, etc.).
//
// Normal terrain cells only store material + flags. When a block needs an
// anchor in persistent game data, a BlockEntityPlacement records its root.
// Typed dynamic state is owned by its feature sidecar (for example, tree
// growth state is stored in GameChunkSidecar::tree_growth_records).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/world/defs/entity_data.h"
#include "game/world/defs/creature_species.h"
#include "game/world/defs/pipe_types.h"
#include "game/world/defs/gt_values.h"

namespace snt::game {

// ============================================================
// BlockEntity — per-block runtime state attached to a voxel cell
// ============================================================
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
//   - BlockEntityPlacement is stored in GameChunk for save/load.
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
    PIPE        = 4,
    CABLE       = 5,
    FARMLAND    = 6,   // Tilled farmland anchor; state is in a chunk sidecar.
    CROP        = 7,   // Crop anchor; growth state is in a chunk sidecar.
    SIGNAL_WIRE = 8,   // Signal wire segment (per-block signal network)
    CUSTOM      = 9,   // Mod-registered block entity type
    // Root anchor for SFM managers, AE controllers, and other automation
    // logic. Typed mutable state is owned by automation_controller_records.
    AUTOMATION_CONTROLLER = 10,
    COUNT       = 11,
};

constexpr const char* kBlockEntityTypeNames[] = {
    "None", "Tree", "Machine", "Creature", "Pipe", "Cable",
    "Farmland", "Crop", "SignalWire", "Custom", "AutomationController",
};

// 6-face connection bitmask used by PIPE and CABLE entities.
// Convention: bit 0 = +X, bit 1 = -X, bit 2 = +Y, bit 3 = -Y,
//             bit 4 = +Z, bit 5 = -Z.
// A set bit means the entity connects to the neighbor on that face.
// (Mirrors the FaceMask ordering used by greedy mesh face culling.)
enum BlockEntityConnectionMask : uint8_t {
    CONN_POS_X = 1u << 0,
    CONN_NEG_X = 1u << 1,
    CONN_POS_Y = 1u << 2,
    CONN_NEG_Y = 1u << 3,
    CONN_POS_Z = 1u << 4,
    CONN_NEG_Z = 1u << 5,
};

// A cell coordinate owned by a BlockEntity.
// Can reference cells in any chunk; the chunk is implied by
// world coordinates (block_x / kChunkSize, etc.).
struct OwnedCell {
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
};

// Lightweight placement data stored in GameChunk for serialization.
// Contains enough information to reconstruct the full runtime entity.
struct BlockEntityPlacement {
    EntityId id;
    BlockEntityType entity_type = BlockEntityType::NONE;

    // Root cell position (world coordinates).
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;

    // Type-specific data encoded as key-value pairs for legacy sidecar
    // anchors that do not yet have a dedicated typed record.
    // MACHINE:  { "machine_type": str, "facing": uint8, ... }
    // PIPE:     { "pipe_type": uint8, "connections": uint8 }
    // CABLE:    { "cable_tier": uint8, "connections": uint8 }
    // This keeps the placement struct generic and extensible.
    std::string type_data_json;

    // Number of owned cells (for serialization bounds checking).
    uint32_t owned_cell_count = 0;
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

// ============================================================
// MachineBlockEntityState — voxel-anchored machine (V28)
// ============================================================

struct MachineBlockEntityState {
    std::string machine_type;        // e.g. "furnace", "coke_oven"
    uint8_t facing = 0;              // 0..5

    // Multiblock formation state.
    bool formed = false;             // is the structure currently formed?
    std::vector<OwnedCell> claimed_cells;  // cells claimed when formed (excl. root)
    std::vector<EntityId> hatch_entities;  // hatch EntityIds when formed
};

// ============================================================
// PipeBlockEntityState — voxel-anchored pipe segment (V28)
// ============================================================

struct PipeBlockEntityState {
    PipeType pipe_type = PipeType::LIQUID;
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// CableBlockEntityState — voxel-anchored cable segment (V28)
// ============================================================

struct CableBlockEntityState {
    VoltageTier cable_tier = VoltageTier::ULV;
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// SignalWireBlockEntityState — voxel-anchored signal wire segment
// ============================================================

struct SignalWireBlockEntityState {
    uint8_t connections = 0;          // BlockEntityConnectionMask bitmask
    int32_t signal_strength = 0;      // Cached signal value (0 = unpowered)
    bool is_source = false;           // True if this wire emits signal
    std::vector<OwnedCell> owned_cells;  // usually empty (single cell)
};

// ============================================================
// CustomBlockEntityState — runtime state for mod-registered entities
// ============================================================

struct CustomBlockEntityState {
    std::string type_key;        // e.g. "my_mod:custom_furnace"
    std::string state_json;      // opaque mod-controlled state blob
    std::vector<OwnedCell> owned_cells;
};

} // namespace snt::game
