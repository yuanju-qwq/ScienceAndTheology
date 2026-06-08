#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <i_render_adapter.h>

namespace science_and_theology {

/// Default material-to-tile mapping for the prototype tileset.
/// Maps TerrainMaterial enum values to TileSet atlas coordinates for each world layer.
///
/// Layout reference (dual_layer_tileset_32_clean.png, 32px tiles):
///   Row 0: Surface ground variants (0-3), special tiles 4+
///   Row 3: Underground floor variants (0-2), special tiles 3+
class TerrainMaterialPalette {
public:
    TerrainMaterialPalette();

    /// Returns tile info for a given material on a specific layer.
    TileRenderInfo get_tile(int material, const std::string& layer_id, int variant_seed) const;

    /// Returns the TileSet source ID to use for a given layer.
    int get_layer_source_id(const std::string& layer_id) const;

    /// Per-layer source IDs, exposed for customization.
    void set_layer_source_id(const std::string& layer_id, int source_id);

private:
    struct MaterialTile {
        int atlas_x = 0;
        int atlas_y = 0;
        int variant_count = 1;
        bool enabled = false;
    };

    MaterialTile get_tile_for_layer(int material, const std::string& layer_id) const;

    std::unordered_map<std::string, int> layer_source_ids_;
};

} // namespace science_and_theology
