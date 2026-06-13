#include "gd_world_gen_config.h"

#include <sstream>
#include <utility>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace science_and_theology {

using namespace godot;

namespace {

Dictionary drop_to_dict(const TerrainDropDef& drop) {
    Dictionary d;
    d["item_key"] = String(drop.item_key.c_str());
    d["item_id"] = static_cast<int64_t>(drop.item_id);
    d["count"] = drop.count;
    d["min_count"] = drop.min_count;
    d["max_count"] = drop.max_count;
    d["chance"] = drop.chance;
    return d;
}

Dictionary material_to_dict(const TerrainMaterialDef& def) {
    Dictionary d;
    d["id"] = static_cast<int>(def.id);
    d["key"] = String(def.key.c_str());
    d["display_name"] = String(def.display_name.c_str());
    d["flags"] = static_cast<int64_t>(def.flags);
    d["hardness"] = def.hardness;
    d["required_tool_tag"] = String(def.required_tool_tag.c_str());
    d["required_mining_level"] = def.required_mining_level;

    Array drops;
    for (const auto& drop : def.drops) {
        drops.append(drop_to_dict(drop));
    }
    d["drops"] = drops;
    return d;
}

Dictionary mapping_to_dict(const TerrainTileMapping& mapping) {
    Dictionary d;
    d["material_id"] = static_cast<int>(mapping.material_id);
    d["material_key"] = String(mapping.material_key.c_str());
    d["layer"] = String(mapping.layer_id.c_str());
    d["source_id"] = mapping.source_id;
    d["atlas_x"] = mapping.atlas_x;
    d["atlas_y"] = mapping.atlas_y;
    d["variant_count"] = mapping.variant_count;
    d["enabled"] = mapping.enabled;
    return d;
}

Dictionary base_rule_to_dict(const BaseTerrainRule& rule) {
    Dictionary d;
    d["layer"] = String(rule.layer_id.c_str());
    d["mode"] = String(rule.mode.c_str());
    d["default_material_id"] = static_cast<int>(rule.default_material);
    d["low_elevation_material_id"] = static_cast<int>(rule.low_elevation_material);
    d["high_elevation_material_id"] = static_cast<int>(rule.high_elevation_material);
    d["cave_air_material_id"] = static_cast<int>(rule.cave_air_material);
    d["elevation_scale"] = rule.elevation_scale;
    d["elevation_octaves"] = rule.elevation_octaves;
    d["detail_scale"] = rule.detail_scale;
    d["detail_octaves"] = rule.detail_octaves;
    d["water_elevation_max"] = rule.water_elevation_max;
    d["water_detail_max"] = rule.water_detail_max;
    d["stone_elevation_abs_min"] = rule.stone_elevation_abs_min;
    d["cave_scale"] = rule.cave_scale;
    d["cave_octaves"] = rule.cave_octaves;
    d["cave_threshold"] = rule.cave_threshold;
    d["cave_edge_threshold_add"] = rule.cave_edge_threshold_add;
    return d;
}

Dictionary biome_rule_to_dict(const BiomeRule& rule) {
    Dictionary d;
    d["key"] = String(rule.key.c_str());
    d["layer"] = String(rule.layer_id.c_str());
    d["source_material_id"] = static_cast<int>(rule.source_material);
    d["result_material_id"] = static_cast<int>(rule.result_material);
    d["condition"] = String(rule.condition.c_str());
    d["temperature_min"] = rule.temperature_min;
    d["temperature_max"] = rule.temperature_max;
    d["humidity_min"] = rule.humidity_min;
    d["humidity_max"] = rule.humidity_max;
    d["requires_near_material"] = rule.requires_near_material;
    d["near_material_id"] = static_cast<int>(rule.near_material);
    d["near_radius"] = rule.near_radius;
    d["requires_floor_support"] = rule.requires_floor_support;
    d["support_material_id"] = static_cast<int>(rule.support_material);
    d["detail_scale"] = rule.detail_scale;
    d["detail_octaves"] = rule.detail_octaves;
    d["detail_threshold"] = rule.detail_threshold;
    return d;
}

Dictionary ore_rule_to_dict(const OreVeinRule& rule) {
    Dictionary d;
    d["key"] = String(rule.key.c_str());
    d["layer"] = String(rule.layer_id.c_str());
    d["host_material_id"] = static_cast<int>(rule.host_material);
    d["ore_material_id"] = static_cast<int>(rule.ore_material);
    d["combined_min"] = rule.combined_min;
    d["combined_max"] = rule.combined_max;
    return d;
}

String issue_missing_mapping_material(int material_id) {
    std::ostringstream out;
    out << "Tile mapping references missing material id "
        << material_id << ".";
    return String(out.str().c_str());
}

} // namespace

GDWorldGenConfig::GDWorldGenConfig()
    : snapshot_(make_empty_world_gen_config()) {}

GDWorldGenConfig::~GDWorldGenConfig() = default;

void GDWorldGenConfig::set_snapshot(
    std::shared_ptr<const WorldGenConfigSnapshot> snapshot) {
    snapshot_ = snapshot ? std::move(snapshot) : make_empty_world_gen_config();
}

std::shared_ptr<const WorldGenConfigSnapshot> GDWorldGenConfig::get_snapshot() const {
    if (snapshot_) {
        return snapshot_;
    }
    return make_empty_world_gen_config();
}

int64_t GDWorldGenConfig::get_schema_version() const {
    return static_cast<int64_t>(get_snapshot()->schema_version);
}

int64_t GDWorldGenConfig::get_content_hash() const {
    return static_cast<int64_t>(get_snapshot()->content_hash);
}

int64_t GDWorldGenConfig::get_material_count() const {
    return static_cast<int64_t>(get_snapshot()->materials.size());
}

int64_t GDWorldGenConfig::get_tile_mapping_count() const {
    return static_cast<int64_t>(get_snapshot()->tile_mappings.size());
}

int64_t GDWorldGenConfig::get_material_id(const String& key) const {
    std::string key_str = key.utf8().get_data();
    const auto snapshot = get_snapshot();
    auto it = snapshot->material_ids_by_key.find(key_str);
    if (it == snapshot->material_ids_by_key.end()) {
        return -1;
    }
    return static_cast<int64_t>(it->second);
}

String GDWorldGenConfig::get_material_key(int64_t id) const {
    const auto snapshot = get_snapshot();
    auto it = snapshot->material_keys_by_id.find(static_cast<int>(id));
    if (it == snapshot->material_keys_by_id.end()) {
        return String();
    }
    return String(it->second.c_str());
}

Dictionary GDWorldGenConfig::get_material_def(int64_t id) const {
    const auto snapshot = get_snapshot();
    const auto* def = snapshot->find_material(static_cast<TerrainMaterialId>(id));
    if (def == nullptr) {
        return Dictionary();
    }
    return material_to_dict(*def);
}

Array GDWorldGenConfig::get_material_defs() const {
    Array result;
    for (const auto& material : get_snapshot()->materials) {
        result.append(material_to_dict(material));
    }
    return result;
}

Array GDWorldGenConfig::get_tile_mappings() const {
    Array result;
    for (const auto& mapping : get_snapshot()->tile_mappings) {
        result.append(mapping_to_dict(mapping));
    }
    return result;
}

Dictionary GDWorldGenConfig::get_material_roles() const {
    const auto snapshot = get_snapshot();
    Dictionary d;
    d["air"] = static_cast<int>(snapshot->roles.air);
    d["stone"] = static_cast<int>(snapshot->roles.stone);
    d["dirt"] = static_cast<int>(snapshot->roles.dirt);
    d["sand"] = static_cast<int>(snapshot->roles.sand);
    d["water"] = static_cast<int>(snapshot->roles.water);
    d["lava"] = static_cast<int>(snapshot->roles.lava);
    d["ore_iron"] = static_cast<int>(snapshot->roles.ore_iron);
    d["ore_copper"] = static_cast<int>(snapshot->roles.ore_copper);
    d["ore_coal"] = static_cast<int>(snapshot->roles.ore_coal);
    d["wood"] = static_cast<int>(snapshot->roles.wood);
    d["leaves"] = static_cast<int>(snapshot->roles.leaves);
    return d;
}

Array GDWorldGenConfig::get_base_terrain_rules() const {
    Array result;
    for (const auto& rule : get_snapshot()->base_terrain_rules) {
        result.append(base_rule_to_dict(rule));
    }
    return result;
}

Array GDWorldGenConfig::get_biome_rules() const {
    Array result;
    for (const auto& rule : get_snapshot()->biome_rules) {
        result.append(biome_rule_to_dict(rule));
    }
    return result;
}

Array GDWorldGenConfig::get_ore_vein_rules() const {
    Array result;
    for (const auto& rule : get_snapshot()->ore_vein_rules) {
        result.append(ore_rule_to_dict(rule));
    }
    return result;
}

Array GDWorldGenConfig::validate() const {
    Array issues;
    const auto snapshot = get_snapshot();
    if (snapshot->materials.empty()) {
        issues.append("WorldGenConfig has no terrain materials.");
    }
    if (!snapshot->has_material(0)) {
        issues.append("WorldGenConfig is missing material id 0 (air).");
    }
    for (const auto& mapping : snapshot->tile_mappings) {
        if (!snapshot->has_material(mapping.material_id)) {
            issues.append(issue_missing_mapping_material(
                static_cast<int>(mapping.material_id)));
        }
    }
    return issues;
}

void GDWorldGenConfig::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_schema_version"),
                         &GDWorldGenConfig::get_schema_version);
    ClassDB::bind_method(D_METHOD("get_content_hash"),
                         &GDWorldGenConfig::get_content_hash);
    ClassDB::bind_method(D_METHOD("get_material_count"),
                         &GDWorldGenConfig::get_material_count);
    ClassDB::bind_method(D_METHOD("get_tile_mapping_count"),
                         &GDWorldGenConfig::get_tile_mapping_count);
    ClassDB::bind_method(D_METHOD("get_material_id", "key"),
                         &GDWorldGenConfig::get_material_id);
    ClassDB::bind_method(D_METHOD("get_material_key", "id"),
                         &GDWorldGenConfig::get_material_key);
    ClassDB::bind_method(D_METHOD("get_material_def", "id"),
                         &GDWorldGenConfig::get_material_def);
    ClassDB::bind_method(D_METHOD("get_material_defs"),
                         &GDWorldGenConfig::get_material_defs);
    ClassDB::bind_method(D_METHOD("get_tile_mappings"),
                         &GDWorldGenConfig::get_tile_mappings);
    ClassDB::bind_method(D_METHOD("get_material_roles"),
                         &GDWorldGenConfig::get_material_roles);
    ClassDB::bind_method(D_METHOD("get_base_terrain_rules"),
                         &GDWorldGenConfig::get_base_terrain_rules);
    ClassDB::bind_method(D_METHOD("get_biome_rules"),
                         &GDWorldGenConfig::get_biome_rules);
    ClassDB::bind_method(D_METHOD("get_ore_vein_rules"),
                         &GDWorldGenConfig::get_ore_vein_rules);
    ClassDB::bind_method(D_METHOD("validate"),
                         &GDWorldGenConfig::validate);
}

} // namespace science_and_theology
