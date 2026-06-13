#include "terrain_material_palette.hpp"

#include <cstdint>
#include <utility>

#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

TerrainMaterialPalette::TerrainMaterialPalette() {
    layer_source_ids_["surface"] = 0;
    layer_source_ids_["underground"] = 0;
}

void TerrainMaterialPalette::set_config(
    std::shared_ptr<const WorldGenConfigSnapshot> config) {
    config_ = std::move(config);
    warned_missing_registered_mappings_.clear();
}

TileRenderInfo TerrainMaterialPalette::get_tile(
    int material, const std::string& layer_id, int variant_seed) const {

    auto tile = get_tile_for_layer(material, layer_id);
    if (!tile.enabled) {
        return TileRenderInfo{};
    }

    int source_id = tile.configured
        ? tile.source_id
        : get_layer_source_id(layer_id);

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
    if (config_) {
        MaterialTile registered_tile =
            get_registered_tile_for_layer(material, layer_id);
        if (registered_tile.configured) {
            return registered_tile;
        }
    }

    return {};
}

TerrainMaterialPalette::MaterialTile TerrainMaterialPalette::get_registered_tile_for_layer(
    int material, const std::string& layer_id) const {
    if (!config_) {
        return {};
    }

    for (const auto& mapping : config_->tile_mappings) {
        if (mapping.material_id == static_cast<TerrainMaterialId>(material) &&
            mapping.layer_id == layer_id) {
            return {
                mapping.source_id,
                mapping.atlas_x,
                mapping.atlas_y,
                mapping.variant_count,
                mapping.enabled,
                true
            };
        }
    }

    auto key = warning_key(material, layer_id);
    if (warned_missing_registered_mappings_.insert(key).second) {
        godot::UtilityFunctions::push_warning(
            "TerrainMaterialPalette: missing registered tile mapping for material ",
            material, " on layer '", layer_id.c_str(), "'.");
    }
    return {};
}

std::string TerrainMaterialPalette::warning_key(
    int material, const std::string& layer_id) {
    return std::to_string(material) + "|" + layer_id;
}

} // namespace science_and_theology
