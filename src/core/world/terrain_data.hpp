#pragma once

#include <cstdint>
#include <vector>

namespace science_and_theology {

using TerrainMaterialId = uint8_t;
using TerrainMaterial = TerrainMaterialId;

// Per-cell gameplay flags derived from material properties.
enum TerrainFlags : uint32_t {
    TF_WALKABLE       = 1 << 0,
    TF_SOLID          = 1 << 1,
    TF_LIQUID         = 1 << 2,
    TF_MINEABLE       = 1 << 3,
    TF_CLIMBABLE      = 1 << 4,
    TF_INDESTRUCTIBLE = 1 << 5,
    // Gravity-affected: block falls when unsupported (sand, gravel).
    TF_GRAVITY_FALL   = 1 << 6,
    // Collapse-eligible: block can cave-in when structural support is lost (stone, deepstone).
    TF_COLLAPSE_RISK  = 1 << 7,
    // Support beam: provides structural support to nearby blocks, preventing cave-ins.
    TF_SUPPORT_BEAM   = 1 << 8,
};

inline constexpr uint32_t operator|(TerrainFlags a, TerrainFlags b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline constexpr uint32_t operator|(uint32_t a, TerrainFlags b) {
    return a | static_cast<uint32_t>(b);
}
inline constexpr bool operator&(uint32_t a, TerrainFlags b) {
    return (a & static_cast<uint32_t>(b)) != 0;
}

// A single voxel in the terrain volume.
struct TerrainCell {
    TerrainMaterial material = 0;
    uint32_t flags = 0;

    bool is_walkable() const { return flags & TF_WALKABLE; }
    bool is_solid() const { return flags & TF_SOLID; }
    bool is_liquid() const { return flags & TF_LIQUID; }
    bool is_mineable() const { return flags & TF_MINEABLE; }
    bool is_climbable() const { return flags & TF_CLIMBABLE; }
    bool is_indestructible() const { return flags & TF_INDESTRUCTIBLE; }
    // Gravity-affected: falls when the block below is empty (sand, gravel).
    bool is_gravity_fall() const { return flags & TF_GRAVITY_FALL; }
    // Collapse-eligible: can cave-in when structural support is lost.
    bool is_collapse_risk() const { return flags & TF_COLLAPSE_RISK; }
    // Support beam: prevents cave-ins within its support radius.
    bool is_support_beam() const { return flags & TF_SUPPORT_BEAM; }
};

// Terrain data for one chunk. Pure data, no rendering information.
struct TerrainData {
    int size_x = 0;
    int size_y = 0;
    int size_z = 0;
    std::vector<TerrainCell> cells;

    const TerrainCell& cell_at(int x, int y, int z) const {
        return cells[index_of(x, y, z)];
    }

    TerrainCell& cell_at(int x, int y, int z) {
        return cells[index_of(x, y, z)];
    }

    void resize(int x, int y, int z) {
        size_x = x;
        size_y = y;
        size_z = z;
        cells.resize(static_cast<size_t>(x * y * z));
    }

    bool is_valid_cell(int x, int y, int z) const {
        return x >= 0 && x < size_x
            && y >= 0 && y < size_y
            && z >= 0 && z < size_z;
    }

    void set_cell(int x, int y, int z, TerrainMaterial material) {
        set_cell(x, y, z, material, 0);
    }

    void set_cell(int x, int y, int z, TerrainMaterial material, uint32_t flags) {
        if (!is_valid_cell(x, y, z)) return;
        auto& cell = cells[index_of(x, y, z)];
        cell.material = material;
        cell.flags = flags;
    }

    size_t index_of(int x, int y, int z) const {
        return static_cast<size_t>((y * size_z + z) * size_x + x);
    }
};

} // namespace science_and_theology
