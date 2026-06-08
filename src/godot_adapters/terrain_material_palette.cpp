#include "terrain_material_palette.hpp"

#include <cstdint>

#include "world/terrain_data.hpp"

namespace science_and_theology {

TerrainMaterialPalette::TerrainMaterialPalette() {
    layer_source_ids_["surface"] = 0;
    layer_source_ids_["underground"] = 0;
}

TileRenderInfo TerrainMaterialPalette::get_tile(
    int material, const std::string& layer_id, int variant_seed) const {

    auto tile = get_tile_for_layer(material, layer_id);
    if (!tile.enabled) {
        return TileRenderInfo{};
    }

    int source_id = get_layer_source_id(layer_id);

    if (tile.variant_count > 1) {
        int variant = (variant_seed & 0x7FFFFFFF) % tile.variant_count;
        return TileRenderInfo{
            source_id,
            {tile.atlas_x + variant, tile.atlas_y},
            true
        };
    }

    return TileRenderInfo{
        source_id,
        {tile.atlas_x, tile.atlas_y},
        true
    };
}

int TerrainMaterialPalette::get_layer_source_id(const std::string& layer_id) const {
    auto it = layer_source_ids_.find(layer_id);
    if (it != layer_source_ids_.end()) {
        return it->second;
    }
    return 0;
}

void TerrainMaterialPalette::set_layer_source_id(const std::string& layer_id, int source_id) {
    layer_source_ids_[layer_id] = source_id;
}

TerrainMaterialPalette::MaterialTile TerrainMaterialPalette::get_tile_for_layer(
    int material, const std::string& layer_id) const {

    const bool is_surface = (layer_id == "surface");

    switch (static_cast<TerrainMaterial>(material)) {
    case TerrainMaterial::AIR:
        return {0, 0, 1, false};

    case TerrainMaterial::STONE:
        if (is_surface) {
            return {4, 0, 1, true};
        }
        return {3, 3, 1, true};

    case TerrainMaterial::DIRT:
        if (is_surface) {
            return {0, 0, 4, true};
        }
        return {0, 3, 3, true};

    case TerrainMaterial::SAND:
        if (is_surface) {
            return {0, 0, 4, true};
        }
        return {0, 3, 3, true};

    case TerrainMaterial::WATER:
        return {8, 0, 1, true};

    case TerrainMaterial::LAVA:
        return {9, 0, 1, true};

    case TerrainMaterial::ORE_IRON:
        if (is_surface) {
            return {5, 0, 1, true};
        }
        return {4, 3, 1, true};

    case TerrainMaterial::ORE_COPPER:
        if (is_surface) {
            return {6, 0, 1, true};
        }
        return {5, 3, 1, true};

    case TerrainMaterial::ORE_COAL:
        if (is_surface) {
            return {7, 0, 1, true};
        }
        return {6, 3, 1, true};

    default:
        return {0, 0, 1, false};
    }
}

} // namespace science_and_theology
