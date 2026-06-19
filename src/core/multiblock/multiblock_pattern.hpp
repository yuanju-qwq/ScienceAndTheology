#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../world/terrain_data.hpp"
#include "../world/entity_data.hpp"
#include "../world/block_entity.hpp"

namespace science_and_theology {

// Forward declarations to avoid heavy includes in the header.
class WorldData;
class BlockEntityRegistry;

// ============================================================
// Multiblock pattern system (V28) — GT-style controller formation
// ============================================================
//
// Ported concept from GregTech's multiblock system (see
// ../../../../GregTech/docs/structure-system-v3-design.md and the
// classic structurelib BlockPattern approach):
//
//   - A machine controller validates its surrounding structure
//     against a 3D pattern of symbols.
//   - Each symbol maps to a MultiblockElement (air / material /
//     controller / hatch / wildcard).
//   - When the world matches the pattern, the controller "forms"
//     and claims all matched cells. Breaking any claimed cell
//     triggers re-validation.
//
// Simplifications vs GT V3:
//   - No repeat groups, no dynamic size, no channels, no async.
//   - Horizontal-facing rotation only (around Y axis). Vertical
//     facings fall back to the canonical +Z orientation.
//   - Hatch type matching is structural (any MACHINE entity at the
//     cell counts as a hatch); the hatch_type_mask is stored for
//     future strict enforcement.

// Hatch type bits for MultiblockElement::HATCH.
// Used by hatch_type_mask to select which hatch kinds a slot accepts.
enum MultiblockHatchType : uint16_t {
    HATCH_ITEM_INPUT    = 1u << 0,
    HATCH_ITEM_OUTPUT   = 1u << 1,
    HATCH_FLUID_INPUT   = 1u << 2,
    HATCH_FLUID_OUTPUT  = 1u << 3,
    HATCH_ENERGY_INPUT  = 1u << 4,
    HATCH_ENERGY_OUTPUT = 1u << 5,
    HATCH_ANY           = 0xFFFFu,  // accepts any hatch type
};

// What's expected at a single pattern cell.
struct MultiblockElement {
    enum Kind : uint8_t {
        AIR        = 0,  // expects empty/non-solid cell
        MATERIAL   = 1,  // expects a specific terrain material
        CONTROLLER = 2,  // the controller itself (marked '~')
        HATCH      = 3,  // accepts a hatch machine entity
        ANY        = 4,  // wildcard — matches anything
    };

    Kind kind = Kind::AIR;
    TerrainMaterialId material_id = 0;  // for MATERIAL
    uint16_t hatch_type_mask = 0;       // for HATCH: accepted hatch types

    static MultiblockElement air() { return {Kind::AIR, 0, 0}; }
    static MultiblockElement material(TerrainMaterialId m) { return {Kind::MATERIAL, m, 0}; }
    static MultiblockElement controller() { return {Kind::CONTROLLER, 0, 0}; }
    static MultiblockElement hatch(uint16_t mask = HATCH_ANY) { return {Kind::HATCH, 0, mask}; }
    static MultiblockElement any() { return {Kind::ANY, 0, 0}; }
};

// A 3D pattern for multiblock structure validation.
//
// Layout convention (matches GT aisle format):
//   - aisles are indexed by Z (depth, front-to-back in canonical orient)
//   - within an aisle, rows are indexed by Y (bottom-to-top)
//   - within a row, chars are indexed by X (left-to-right)
//
// The '~' char marks the controller cell. Its position becomes the
// controller offset, used to align the pattern against the controller's
// world position.
class MultiblockPattern {
public:
    MultiblockPattern() = default;

    // Build a pattern from aisle definitions.
    // aisles[z] is a vector of strings (one per Y row, bottom-to-top).
    // char_map maps each non-controller char to its element.
    // '~' is always CONTROLLER; ' ' (space) is always ANY (skipped).
    // Returns an empty pattern (size 0) on malformed input.
    static MultiblockPattern build(
        const std::vector<std::vector<std::string>>& aisles,
        const std::unordered_map<char, MultiblockElement>& char_map);

    int size_x() const { return size_x_; }
    int size_y() const { return size_y_; }
    int size_z() const { return size_z_; }

    int controller_offset_x() const { return ctrl_ox_; }
    int controller_offset_y() const { return ctrl_oy_; }
    int controller_offset_z() const { return ctrl_oz_; }

    // Element at pattern coords. Returns ANY for out-of-range (defensive).
    const MultiblockElement& at(int x, int y, int z) const;

    // A trivial 1x1x1 pattern (controller only) needs no structure check.
    bool is_trivial() const {
        return size_x_ <= 1 && size_y_ <= 1 && size_z_ <= 1;
    }

    bool valid() const { return size_x_ > 0 && size_y_ > 0 && size_z_ > 0; }

private:
    std::vector<MultiblockElement> elements_;  // flattened, index = (z*sy + y)*sx + x
    int size_x_ = 0, size_y_ = 0, size_z_ = 0;
    int ctrl_ox_ = 0, ctrl_oy_ = 0, ctrl_oz_ = 0;
};

// Registry of multiblock patterns keyed by machine_type.
class MultiblockPatternRegistry {
public:
    // Register or replace a pattern for a machine_type.
    void register_pattern(const std::string& machine_type,
                          MultiblockPattern pattern);

    // Lookup. Returns nullptr if machine_type has no pattern.
    // A machine_type with no registered pattern is treated as a
    // trivial 1x1x1 machine (always formed).
    const MultiblockPattern* get_pattern(const std::string& machine_type) const;

    bool has_pattern(const std::string& machine_type) const {
        return patterns_.find(machine_type) != patterns_.end();
    }

    size_t size() const { return patterns_.size(); }

private:
    std::unordered_map<std::string, MultiblockPattern> patterns_;
};

// Result of a formation check.
struct FormationResult {
    bool formed = false;
    std::vector<OwnedCell> claimed_cells;   // non-controller matched cells
    std::vector<EntityId> hatch_entities;   // hatch machine EntityIds
    // First mismatch location (pattern coords), for diagnostics.
    // Valid only when formed == false.
    int mismatch_x = -1;
    int mismatch_y = -1;
    int mismatch_z = -1;
};

// Check multiblock formation for a machine controller entity.
//
// world:     provides terrain material queries (loaded chunks only).
// registry:  block entity registry — looks up the machine + hatches.
// patterns:  multiblock pattern registry — looks up the machine's pattern.
// machine_id: the controller's EntityId (must be a MACHINE entity).
//
// Behavior:
//   - If machine_id is not a MACHINE entity, returns formed=false.
//   - If machine_type has no registered pattern, treats it as a trivial
//     1x1x1 machine: returns formed=true with no claimed cells.
//   - Otherwise iterates the pattern, rotates by facing, queries the
//     world, and checks each cell against the expected element.
//   - On full match: returns formed=true with claimed_cells (non-controller
//     matched cells) and hatch_entities (MACHINE entities at HATCH slots).
//   - On first mismatch: returns formed=false with the mismatch location.
//
// Note: cells in unloaded chunks are treated as air (material 0).
FormationResult check_formation(
    const WorldData& world,
    const BlockEntityRegistry& registry,
    const MultiblockPatternRegistry& patterns,
    EntityId machine_id);

// Rotate a pattern-local offset (relative to controller) into a
// world-relative offset, based on the controller's facing.
// facing: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
// Canonical pattern orientation: front = +Z.
// Horizontal rotations only (around Y axis); vertical facings (2,3)
// fall back to canonical +Z orientation.
inline void rotate_pattern_offset(int px, int py, int pz, uint8_t facing,
                                  int& dx, int& dy, int& dz) {
    switch (facing) {
        case 4: // +Z (canonical)
            dx = px; dy = py; dz = pz; break;
        case 5: // -Z (180° around Y)
            dx = -px; dy = py; dz = -pz; break;
        case 0: // +X (90° around Y: pattern +Z → world +X)
            dx = pz; dy = py; dz = -px; break;
        case 1: // -X (270° around Y: pattern +Z → world -X)
            dx = -pz; dy = py; dz = px; break;
        default: // 2=+Y, 3=-Y — no vertical rotation yet
            dx = px; dy = py; dz = pz; break;
    }
}

} // namespace science_and_theology
