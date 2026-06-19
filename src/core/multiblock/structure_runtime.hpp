#pragma once

// ============================================================
// GT V3 Multiblock System — Runtime Layer
// ============================================================
//
// Ported from GregTech CEu's V3 StructureRuntime +
// StructureCheckResult + FormedStructureView.
//
// StructureRuntime:
//   - Per-controller mutable runtime state
//   - Holds a reference to the shared StructureDefinition
//   - Performs formation checks against the live world
//   - Tracks formed/invalidate lifecycle
//
// StructureCheckResult:
//   - Result of a single check pass
//   - Contains matched parts, hatches, channel values
//   - On failure, contains mismatch location for diagnostics
//
// FormedStructureView:
//   - Immutable view passed to the controller's form_structure()
//     callback when formation succeeds
//   - Replaces GT V3's PatternMatchContext (which is legacy)

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "structure_definition.hpp"
#include "structure_element.hpp"
#include "../world/entity_data.hpp"
#include "../world/block_entity.hpp"

namespace science_and_theology {

// Forward declarations.
class WorldData;
class BlockEntityRegistry;

namespace multiblock {

// ------------------------------------------------------------
// StructureCheckResult — result of a formation check.
//
// GT V3 equivalent: StructureCheckResult (simplified — no
// PatternMatchContext, no legacy projection).
// ------------------------------------------------------------
struct StructureCheckResult {
    bool matched = false;

    // World-space claimed cells (non-controller matched cells).
    std::vector<OwnedCell> claimed_cells;

    // Hatch EntityIds matched during the check.
    std::vector<EntityId> hatch_entities;

    // Channel values (for future repeat group support).
    std::unordered_map<std::string, int> channel_values;

    // First mismatch location (pattern coords), for diagnostics.
    // Valid only when matched == false.
    int mismatch_x = -1;
    int mismatch_y = -1;
    int mismatch_z = -1;
    std::string mismatch_reason;

    static StructureCheckResult success(
        std::vector<OwnedCell> cells,
        std::vector<EntityId> hatches) {
        StructureCheckResult r;
        r.matched = true;
        r.claimed_cells = std::move(cells);
        r.hatch_entities = std::move(hatches);
        return r;
    }

    static StructureCheckResult failure(
        int mx, int my, int mz, const std::string& reason) {
        StructureCheckResult r;
        r.matched = false;
        r.mismatch_x = mx;
        r.mismatch_y = my;
        r.mismatch_z = mz;
        r.mismatch_reason = reason;
        return r;
    }
};

// ------------------------------------------------------------
// FormedStructureView — passed to form_structure() callback.
//
// GT V3 equivalent: FormedStructureView (without legacy projection).
// ------------------------------------------------------------
class FormedStructureView {
public:
    FormedStructureView() = default;
    FormedStructureView(
        std::vector<OwnedCell> cells,
        std::vector<EntityId> hatches,
        std::unordered_map<std::string, int> channels)
        : claimed_cells_(std::move(cells))
        , hatch_entities_(std::move(hatches))
        , channel_values_(std::move(channels)) {}

    const std::vector<OwnedCell>& claimed_cells() const { return claimed_cells_; }
    const std::vector<EntityId>& hatch_entities() const { return hatch_entities_; }

    int get_channel_value(const std::string& channel) const {
        auto it = channel_values_.find(channel);
        return (it != channel_values_.end()) ? it->second : 0;
    }

private:
    std::vector<OwnedCell> claimed_cells_;
    std::vector<EntityId> hatch_entities_;
    std::unordered_map<std::string, int> channel_values_;
};

// ------------------------------------------------------------
// StructureRuntime — per-controller runtime.
//
// Each multiblock controller has its own StructureRuntime.
// The runtime performs formation checks and tracks the
// formed/invalidate lifecycle.
//
// GT V3 equivalent: StructureRuntime (simplified — no incremental
// checking, no async, no dirty state).
// ------------------------------------------------------------
class StructureRuntime {
public:
    StructureRuntime() = default;
    explicit StructureRuntime(std::shared_ptr<StructureDefinition> def);

    // Set or replace the structure definition.
    void set_definition(std::shared_ptr<StructureDefinition> def);

    // Check formation against the live world.
    StructureCheckResult check(
        const WorldData& world,
        const BlockEntityRegistry& registry,
        EntityId controller_id,
        const std::string& dimension_id,
        int32_t ctrl_x, int32_t ctrl_y, int32_t ctrl_z,
        uint8_t facing);

    // Invalidate the current formation.
    void invalidate();

    bool is_formed() const { return formed_; }
    const FormedStructureView& formed_view() const { return formed_view_; }
    const StructureDefinition* definition() const { return definition_.get(); }

private:
    std::shared_ptr<StructureDefinition> definition_;
    bool formed_ = false;
    FormedStructureView formed_view_;
};

// ------------------------------------------------------------
// Rotation helper — rotate a pattern-local offset to world offset.
//
// facing: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
// Canonical pattern orientation: front = +Z.
// Horizontal rotations only (around Y axis); vertical facings (2,3)
// fall back to canonical +Z orientation.
// ------------------------------------------------------------
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

} // namespace multiblock
} // namespace science_and_theology
