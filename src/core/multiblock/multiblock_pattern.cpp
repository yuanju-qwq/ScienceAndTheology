#include "multiblock_pattern.hpp"

#include <cmath>

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"

namespace science_and_theology {

// ============================================================
// MultiblockPattern
// ============================================================

MultiblockPattern MultiblockPattern::build(
    const std::vector<std::vector<std::string>>& aisles,
    const std::unordered_map<char, MultiblockElement>& char_map) {

    MultiblockPattern result;

    if (aisles.empty()) return result;

    // Determine dimensions.
    const int sz = static_cast<int>(aisles.size());
    int sy = 0;
    int sx = 0;
    for (const auto& aisle : aisles) {
        sy = (static_cast<int>(aisle.size()) > sy)
            ? static_cast<int>(aisle.size()) : sy;
        for (const auto& row : aisle) {
            sx = (static_cast<int>(row.size()) > sx)
                ? static_cast<int>(row.size()) : sx;
        }
    }
    if (sx <= 0 || sy <= 0 || sz <= 0) return result;

    result.size_x_ = sx;
    result.size_y_ = sy;
    result.size_z_ = sz;
    result.elements_.assign(
        static_cast<size_t>(sx * sy * sz), MultiblockElement::any());

    bool found_controller = false;

    for (int z = 0; z < sz; ++z) {
        const auto& aisle = aisles[z];
        for (int y = 0; y < sy; ++y) {
            // Rows are bottom-to-top in the input; index directly.
            const std::string* row_ptr = nullptr;
            if (y < static_cast<int>(aisle.size())) {
                row_ptr = &aisle[y];
            }
            for (int x = 0; x < sx; ++x) {
                MultiblockElement elem = MultiblockElement::any();
                if (row_ptr && x < static_cast<int>(row_ptr->size())) {
                    char c = row_ptr->at(x);
                    if (c == '~') {
                        elem = MultiblockElement::controller();
                        if (found_controller) {
                            // Multiple controllers — malformed.
                            return MultiblockPattern();
                        }
                        found_controller = true;
                        result.ctrl_ox_ = x;
                        result.ctrl_oy_ = y;
                        result.ctrl_oz_ = z;
                    } else if (c == ' ' || c == '.') {
                        // Space/dot = wildcard (skip, don't claim).
                        elem = MultiblockElement::any();
                    } else {
                        auto it = char_map.find(c);
                        if (it != char_map.end()) {
                            elem = it->second;
                        } else {
                            // Unknown char — treat as wildcard.
                            elem = MultiblockElement::any();
                        }
                    }
                }
                size_t idx = static_cast<size_t>((z * sy + y) * sx + x);
                result.elements_[idx] = elem;
            }
        }
    }

    if (!found_controller) {
        // No controller marker — malformed pattern.
        return MultiblockPattern();
    }

    return result;
}

const MultiblockElement& MultiblockPattern::at(int x, int y, int z) const {
    static const MultiblockElement fallback = MultiblockElement::any();
    if (x < 0 || x >= size_x_ || y < 0 || y >= size_y_ || z < 0 || z >= size_z_) {
        return fallback;
    }
    size_t idx = static_cast<size_t>((z * size_y_ + y) * size_x_ + x);
    return elements_[idx];
}

// ============================================================
// MultiblockPatternRegistry
// ============================================================

void MultiblockPatternRegistry::register_pattern(
    const std::string& machine_type, MultiblockPattern pattern) {
    patterns_[machine_type] = std::move(pattern);
}

const MultiblockPattern* MultiblockPatternRegistry::get_pattern(
    const std::string& machine_type) const {
    auto it = patterns_.find(machine_type);
    if (it == patterns_.end()) return nullptr;
    return &it->second;
}

// ============================================================
// Formation check
// ============================================================

namespace {

// Query terrain material at world block coords.
// Returns 0 (air) if the chunk is not loaded or coords are out of range.
TerrainMaterialId query_material_at(const WorldData& world,
                                     const std::string& dimension_id,
                                     int bx, int by, int bz) {
    constexpr int kChunkSize = ChunkData::kChunkSize;  // 32

    int cx = static_cast<int>(std::floor(static_cast<float>(bx) / kChunkSize));
    int cy = static_cast<int>(std::floor(static_cast<float>(by) / kChunkSize));
    int cz = static_cast<int>(std::floor(static_cast<float>(bz) / kChunkSize));

    const ChunkData* chunk = world.get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return 0;

    int lx = bx - cx * kChunkSize;
    int ly = by - cy * kChunkSize;
    int lz = bz - cz * kChunkSize;

    const TerrainData& terrain = chunk->terrain;
    if (!terrain.is_valid_cell(lx, ly, lz)) return 0;
    return terrain.cell_at(lx, ly, lz).material;
}

} // anonymous namespace

FormationResult check_formation(
    const WorldData& world,
    const BlockEntityRegistry& registry,
    const MultiblockPatternRegistry& patterns,
    EntityId machine_id) {

    FormationResult result;

    const MachineBlockEntityState* machine = registry.get_machine_state(machine_id);
    if (!machine) return result;  // not a machine → not formed

    const BlockEntityPlacement* placement = registry.get_placement(machine_id);
    if (!placement) return result;

    const std::string* dim_ptr = registry.get_dimension_id(machine_id);
    if (!dim_ptr) return result;
    const std::string& dimension_id = *dim_ptr;

    // No pattern registered → trivial 1x1x1 machine (always formed).
    const MultiblockPattern* pattern = patterns.get_pattern(machine->machine_type);
    if (!pattern || !pattern->valid() || pattern->is_trivial()) {
        result.formed = true;
        return result;
    }

    const int sx = pattern->size_x();
    const int sy = pattern->size_y();
    const int sz = pattern->size_z();
    const uint8_t facing = machine->facing;

    // Iterate all pattern cells, check against the world.
    for (int pz = 0; pz < sz; ++pz) {
        for (int py = 0; py < sy; ++py) {
            for (int px = 0; px < sx; ++px) {
                const MultiblockElement& elem = pattern->at(px, py, pz);

                // Offset relative to controller in pattern space.
                int rx = px - pattern->controller_offset_x();
                int ry = py - pattern->controller_offset_y();
                int rz = pz - pattern->controller_offset_z();

                // Rotate to world-relative offset.
                int dx, dy, dz;
                rotate_pattern_offset(rx, ry, rz, facing, dx, dy, dz);

                // World position.
                int wx = placement->root_x + dx;
                int wy = placement->root_y + dy;
                int wz = placement->root_z + dz;

                switch (elem.kind) {
                    case MultiblockElement::CONTROLLER:
                        // The controller cell itself — always matches.
                        // (It's the machine's root position.)
                        break;

                    case MultiblockElement::ANY:
                        // Wildcard — matches anything, but we still claim
                        // the cell so breaking it triggers re-validation.
                        // Actually, for wildcard we DON'T claim (it could
                        // be anything, including air). Skip claiming.
                        break;

                    case MultiblockElement::AIR: {
                        TerrainMaterialId m = query_material_at(
                            world, dimension_id, wx, wy, wz);
                        if (m != 0) {
                            result.mismatch_x = px;
                            result.mismatch_y = py;
                            result.mismatch_z = pz;
                            return result;  // not formed
                        }
                        // Air cells are not claimed (they're empty space).
                        break;
                    }

                    case MultiblockElement::MATERIAL: {
                        TerrainMaterialId m = query_material_at(
                            world, dimension_id, wx, wy, wz);
                        if (m != elem.material_id) {
                            result.mismatch_x = px;
                            result.mismatch_y = py;
                            result.mismatch_z = pz;
                            return result;  // not formed
                        }
                        // Claim this cell.
                        result.claimed_cells.push_back(
                            OwnedCell{wx, wy, wz});
                        break;
                    }

                    case MultiblockElement::HATCH: {
                        // A hatch slot: check if there's a MACHINE block
                        // entity at this cell (other than the controller).
                        EntityId owner = registry.find_owner_at(wx, wy, wz);
                        if (!owner.is_valid() || owner == machine_id) {
                            result.mismatch_x = px;
                            result.mismatch_y = py;
                            result.mismatch_z = pz;
                            return result;  // not formed
                        }
                        // Verify the owner is actually a machine (hatch).
                        if (registry.get_entity_type(owner) != BlockEntityType::MACHINE) {
                            result.mismatch_x = px;
                            result.mismatch_y = py;
                            result.mismatch_z = pz;
                            return result;  // not formed
                        }
                        // Claim the cell and record the hatch entity.
                        result.claimed_cells.push_back(
                            OwnedCell{wx, wy, wz});
                        result.hatch_entities.push_back(owner);
                        break;
                    }
                }
            }
        }
    }

    // All cells matched.
    result.formed = true;
    return result;
}

} // namespace science_and_theology
