#pragma once

#include <cstdint>

namespace science_and_theology {

/// Coordinates of a tile within a TileSet atlas.
struct TileAtlasCoords {
    int x = 0;
    int y = 0;
};

/// Parameters for rendering a terrain cell as a tile.
struct TileRenderInfo {
    int source_id = -1;          ///< TileSet source ID (-1 = no tile).
    TileAtlasCoords atlas_coords; ///< Atlas coordinates within the source.
    bool enabled = false;         ///< Whether a tile should be placed.
};

/// Pure virtual interface for rendering operations.
/// Decouples core simulation data from the rendering engine's tile system.
class IRenderAdapter {
public:
    virtual ~IRenderAdapter() = default;

    /// Converts a terrain material to tile rendering info for a specific layer.
    /// \param material       The TerrainMaterial enum value (as int).
    /// \param layer_id       Layer identifier (e.g. "surface", "underground").
    /// \param variant_seed   Seed value for deterministic tile variant selection.
    virtual TileRenderInfo get_tile_for_material(
        int material, const char* layer_id, int variant_seed) const = 0;

    /// Returns the TileSet source ID to use for a given world layer.
    virtual int get_layer_source_id(const char* layer_id) const = 0;
};

} // namespace science_and_theology