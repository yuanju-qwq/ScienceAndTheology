#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <i_render_adapter.h>
#include "world_gen/world_gen_config.hpp"

namespace science_and_theology {

/// Default material-to-tile mapping for the prototype tileset.
/// Maps TerrainMaterial enum values to TileSet atlas coordinates for each world layer.
///
/// Layout reference (dual_layer_tileset_32_clean.png, 32px tiles):
///   Row 0: Surface dirt variants (0-3), special tiles 4+
///   Row 1: Surface sand variants (0-3)
///   Row 3: Underground floor variants (0-2), special tiles 3+
///   Row 4: Underground sand variants (6-9)
class TerrainMaterialPalette {
public:
    TerrainMaterialPalette();

    /// Returns tile info for a given material on a specific layer.
    TileRenderInfo get_tile(int material, const std::string& layer_id, int variant_seed) const;

    void set_config(std::shared_ptr<const WorldGenConfigSnapshot> config);

    /// Returns the TileSet source ID to use for a given layer.
    int get_layer_source_id(const std::string& layer_id) const;

    /// Per-layer source IDs, exposed for customization.
    void set_layer_source_id(const std::string& layer_id, int source_id);

private:
    struct MaterialTile {
        int source_id = 0;
        int atlas_x = 0;
        int atlas_y = 0;
        int variant_count = 1;
        bool enabled = false;
        bool configured = false;
    };

    MaterialTile get_tile_for_layer(int material, const std::string& layer_id) const;
    MaterialTile get_registered_tile_for_layer(
        int material, const std::string& layer_id) const;
    static std::string warning_key(int material, const std::string& layer_id);

    std::unordered_map<std::string, int> layer_source_ids_;
    std::shared_ptr<const WorldGenConfigSnapshot> config_;
    mutable std::unordered_set<std::string> warned_missing_registered_mappings_;
};

} // namespace science_and_theology
