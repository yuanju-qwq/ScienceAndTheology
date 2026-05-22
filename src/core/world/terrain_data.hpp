#pragma once

#include <cstdint>
#include <vector>

namespace science_and_theology {

// World material type, not rendering semantics.
// Determines physical properties and gameplay behavior of each cell.
enum class TerrainMaterial : uint8_t {
    AIR = 0,
    STONE,
    DIRT,
    SAND,
    WATER,
    LAVA,

    ORE_IRON,
    ORE_COPPER,
    ORE_COAL,
};

// Display name for each terrain material.
constexpr const char* kTerrainMaterialNames[] = {
    "Air", "Stone", "Dirt", "Sand", "Water", "Lava",
    "Iron Ore", "Copper Ore", "Coal Ore",
};

// Per-cell gameplay flags derived from material properties.
enum TerrainFlags : uint32_t {
    TF_WALKABLE = 1 << 0,
    TF_SOLID    = 1 << 1,
    TF_LIQUID   = 1 << 2,
    TF_MINEABLE = 1 << 3,
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

// Default flags for each terrain material.
constexpr uint32_t kTerrainMaterialFlags[] = {
    // AIR
    0,
    // STONE
    TF_SOLID | TF_MINEABLE,
    // DIRT
    TF_WALKABLE | TF_MINEABLE,
    // SAND
    TF_WALKABLE | TF_MINEABLE,
    // WATER
    TF_LIQUID,
    // LAVA
    TF_LIQUID,
    // ORE_IRON
    TF_SOLID | TF_MINEABLE,
    // ORE_COPPER
    TF_SOLID | TF_MINEABLE,
    // ORE_COAL
    TF_SOLID | TF_MINEABLE,
};

// A single cell in the terrain grid.
struct TerrainCell {
    TerrainMaterial material = TerrainMaterial::AIR;
    uint32_t flags = 0;

    bool is_walkable() const { return flags & TF_WALKABLE; }
    bool is_solid() const { return flags & TF_SOLID; }
    bool is_liquid() const { return flags & TF_LIQUID; }
    bool is_mineable() const { return flags & TF_MINEABLE; }
};

// Terrain data for one chunk. Pure data, no rendering information.
struct TerrainData {
    int size_x = 0;
    int size_y = 0;
    std::vector<TerrainCell> cells;

    const TerrainCell& cell_at(int x, int y) const {
        return cells[static_cast<size_t>(y * size_x + x)];
    }

    TerrainCell& cell_at(int x, int y) {
        return cells[static_cast<size_t>(y * size_x + x)];
    }

    void resize(int x, int y) {
        size_x = x;
        size_y = y;
        cells.resize(static_cast<size_t>(x * y));
    }

    bool is_valid_cell(int x, int y) const {
        return x >= 0 && x < size_x && y >= 0 && y < size_y;
    }
};

} // namespace science_and_theology