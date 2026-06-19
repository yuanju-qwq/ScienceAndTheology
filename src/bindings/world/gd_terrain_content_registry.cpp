#include "gd_terrain_content_registry.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/material/material_item.hpp"

VARIANT_ENUM_CAST(science_and_theology::GDTerrainContentRegistry::TerrainFlagConst)

namespace science_and_theology {

using namespace godot;

namespace {

std::string to_std_string(const Variant& value) {
    return String(value).utf8().get_data();
}

std::string dimension_from_dict(const Dictionary& def) {
    return to_std_string(def.get("dimension", def.get("dimension_id", "overworld")));
}

TerrainMaterialId to_material_id(int64_t id) {
    if (id < 0) {
        return 0;
    }
    if (id > 255) {
        return 255;
    }
    return static_cast<TerrainMaterialId>(id);
}

TerrainMaterialId material_from_dict(
    const Dictionary& def,
    const char* key_field,
    const char* id_field,
    TerrainMaterialId fallback,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    if (def.has(key_field)) {
        std::string key = to_std_string(def.get(key_field, ""));
        auto it = ids_by_key.find(key);
        if (it != ids_by_key.end()) {
            return it->second;
        }
    }
    if (def.has(id_field)) {
        return to_material_id(static_cast<int64_t>(
            def.get(id_field, static_cast<int>(fallback))));
    }
    return fallback;
}

void assign_role_from_dict(
    TerrainMaterialId& role,
    const Dictionary& def,
    const char* role_name,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    std::string key_field = std::string(role_name) + "_key";
    std::string id_field = std::string(role_name) + "_id";
    role = material_from_dict(
        def, key_field.c_str(), id_field.c_str(), role, ids_by_key);
}

String issue_duplicate_id(int id, const std::string& key, const std::string& other_key) {
    std::ostringstream out;
    out << "Terrain material id " << id << " is used by both '"
        << key << "' and '" << other_key << "'.";
    return String(out.str().c_str());
}

String issue_material_empty_key(int id) {
    std::ostringstream out;
    out << "Terrain material id " << id << " has empty key.";
    return String(out.str().c_str());
}

String issue_duplicate_key(const std::string& key) {
    std::ostringstream out;
    out << "Terrain material key '" << key << "' is registered more than once.";
    return String(out.str().c_str());
}

String issue_missing_visual_material(
    const std::string& dimension_id, int material_id) {
    std::ostringstream out;
    out << "Material visual for dimension '" << dimension_id
        << "' references missing material id " << material_id << ".";
    return String(out.str().c_str());
}

String issue_empty_visual_dimension(int material_id) {
    std::ostringstream out;
    out << "Material visual for material id " << material_id
        << " has empty dimension.";
    return String(out.str().c_str());
}

String issue_duplicate_visual(int material_id, const std::string& dimension_id) {
    std::ostringstream out;
    out << "Duplicate material visual for material id " << material_id
        << " dimension '" << dimension_id << "'.";
    return String(out.str().c_str());
}

String issue_unresolved_drop_item(const std::string& material_key,
                                  const std::string& item_key) {
    std::ostringstream out;
    out << "Terrain material '" << material_key
        << "' has drop item_key '" << item_key
        << "' that does not resolve to a registered item.";
    return String(out.str().c_str());
}

TerrainFaceTexture face_texture_from_dict(const Dictionary& def, const char* prefix) {
    TerrainFaceTexture face;
    std::string path_key = std::string(prefix) + "_texture";
    std::string count_key = std::string(prefix) + "_variant_count";
    if (def.has(path_key.c_str())) {
        face.texture_path = to_std_string(def.get(path_key.c_str(), ""));
    }
    if (def.has(count_key.c_str())) {
        face.variant_count = std::max(1, static_cast<int>(def.get(count_key.c_str(), 1)));
    }
    return face;
}

TerrainMaterialVisualDef visual_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    TerrainMaterialVisualDef visual;

    visual.dimension_id = dimension_from_dict(def);
    visual.enabled = static_cast<bool>(def.get("enabled", true));

    // Per-face textures.
    visual.top = face_texture_from_dict(def, "top");
    visual.bottom = face_texture_from_dict(def, "bottom");
    visual.sides = face_texture_from_dict(def, "sides");

    // Fallback albedo color.
    if (def.has("albedo_color")) {
        Color c = def.get("albedo_color", Color(0.85f, 0.20f, 0.85f));
        visual.albedo_r = c.r;
        visual.albedo_g = c.g;
        visual.albedo_b = c.b;
        visual.albedo_a = c.a;
    } else {
        visual.albedo_r = static_cast<float>(def.get("albedo_r", visual.albedo_r));
        visual.albedo_g = static_cast<float>(def.get("albedo_g", visual.albedo_g));
        visual.albedo_b = static_cast<float>(def.get("albedo_b", visual.albedo_b));
        visual.albedo_a = static_cast<float>(def.get("albedo_a", visual.albedo_a));
    }

    // Material properties.
    visual.roughness = static_cast<float>(def.get("roughness", visual.roughness));
    if (def.has("emissive_color")) {
        Color e = def.get("emissive_color", Color(0.0f, 0.0f, 0.0f));
        visual.emissive_r = e.r;
        visual.emissive_g = e.g;
        visual.emissive_b = e.b;
    } else {
        visual.emissive_r = static_cast<float>(def.get("emissive_r", visual.emissive_r));
        visual.emissive_g = static_cast<float>(def.get("emissive_g", visual.emissive_g));
        visual.emissive_b = static_cast<float>(def.get("emissive_b", visual.emissive_b));
    }
    visual.transparent = static_cast<bool>(def.get("transparent", visual.transparent));
    visual.cull_disabled = static_cast<bool>(def.get("cull_disabled", visual.cull_disabled));

    // Overlay layers.
    Array overlays = def.get("overlays", Array());
    for (int i = 0; i < overlays.size(); ++i) {
        Dictionary overlay_dict = overlays[i];
        TerrainOverlayLayer overlay;
        overlay.texture_path = to_std_string(overlay_dict.get("texture_path", ""));
        overlay.blend = static_cast<float>(overlay_dict.get("blend", 0.5f));
        if (!overlay.texture_path.empty()) {
            visual.overlays.push_back(std::move(overlay));
        }
    }

    // Resolve material key/id.
    if (def.has("material_key")) {
        visual.material_key = to_std_string(def.get("material_key", ""));
        auto it = ids_by_key.find(visual.material_key);
        if (it != ids_by_key.end()) {
            visual.material_id = it->second;
        }
    }

    if (def.has("material_id")) {
        visual.material_id = to_material_id(static_cast<int64_t>(
            def.get("material_id", static_cast<int>(visual.material_id))));
    } else if (def.has("id")) {
        visual.material_id = to_material_id(static_cast<int64_t>(
            def.get("id", static_cast<int>(visual.material_id))));
    }

    if (visual.material_key.empty()) {
        auto key_it = std::find_if(ids_by_key.begin(), ids_by_key.end(),
            [&visual](const auto& pair) {
                return pair.second == visual.material_id;
            });
        if (key_it != ids_by_key.end()) {
            visual.material_key = key_it->first;
        }
    }

    return visual;
}

BaseTerrainRule base_rule_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    BaseTerrainRule rule;
    rule.dimension_id = dimension_from_dict(def);
    rule.mode = to_std_string(def.get("mode", "solid"));
    rule.default_material = material_from_dict(
        def, "default_material_key", "default_material_id",
        rule.default_material, ids_by_key);
    rule.low_elevation_material = material_from_dict(
        def, "low_elevation_material_key", "low_elevation_material_id",
        rule.low_elevation_material, ids_by_key);
    rule.high_elevation_material = material_from_dict(
        def, "high_elevation_material_key", "high_elevation_material_id",
        rule.high_elevation_material, ids_by_key);
    rule.cave_air_material = material_from_dict(
        def, "cave_air_material_key", "cave_air_material_id",
        rule.cave_air_material, ids_by_key);
    rule.elevation_scale = static_cast<float>(
        def.get("elevation_scale", rule.elevation_scale));
    rule.elevation_octaves = static_cast<int>(
        def.get("elevation_octaves", rule.elevation_octaves));
    rule.detail_scale = static_cast<float>(
        def.get("detail_scale", rule.detail_scale));
    rule.detail_octaves = static_cast<int>(
        def.get("detail_octaves", rule.detail_octaves));
    rule.water_elevation_max = static_cast<float>(
        def.get("water_elevation_max", rule.water_elevation_max));
    rule.water_detail_max = static_cast<float>(
        def.get("water_detail_max", rule.water_detail_max));
    rule.stone_elevation_abs_min = static_cast<float>(
        def.get("stone_elevation_abs_min", rule.stone_elevation_abs_min));
    rule.cave_scale = static_cast<float>(
        def.get("cave_scale", rule.cave_scale));
    rule.cave_octaves = static_cast<int>(
        def.get("cave_octaves", rule.cave_octaves));
    rule.cave_threshold = static_cast<float>(
        def.get("cave_threshold", rule.cave_threshold));
    rule.cave_edge_threshold_add = static_cast<float>(
        def.get("cave_edge_threshold_add", rule.cave_edge_threshold_add));
    return rule;
}

BiomeRule biome_rule_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    BiomeRule rule;
    rule.key = to_std_string(def.get("key", ""));
    rule.dimension_id = dimension_from_dict(def);
    rule.source_material = material_from_dict(
        def, "source_material_key", "source_material_id",
        rule.source_material, ids_by_key);
    rule.result_material = material_from_dict(
        def, "result_material_key", "result_material_id",
        rule.result_material, ids_by_key);
    rule.condition = to_std_string(def.get("condition", rule.condition.c_str()));
    rule.temperature_min = static_cast<float>(
        def.get("temperature_min", rule.temperature_min));
    rule.temperature_max = static_cast<float>(
        def.get("temperature_max", rule.temperature_max));
    rule.humidity_min = static_cast<float>(
        def.get("humidity_min", rule.humidity_min));
    rule.humidity_max = static_cast<float>(
        def.get("humidity_max", rule.humidity_max));
    rule.requires_near_material = static_cast<bool>(
        def.get("requires_near_material", rule.requires_near_material));
    rule.near_material = material_from_dict(
        def, "near_material_key", "near_material_id",
        rule.near_material, ids_by_key);
    rule.near_radius = static_cast<int>(
        def.get("near_radius", rule.near_radius));
    rule.requires_floor_support = static_cast<bool>(
        def.get("requires_floor_support", rule.requires_floor_support));
    rule.support_material = material_from_dict(
        def, "support_material_key", "support_material_id",
        rule.support_material, ids_by_key);
    rule.detail_scale = static_cast<float>(
        def.get("detail_scale", rule.detail_scale));
    rule.detail_octaves = static_cast<int>(
        def.get("detail_octaves", rule.detail_octaves));
    rule.detail_threshold = static_cast<float>(
        def.get("detail_threshold", rule.detail_threshold));
    return rule;
}

OreVeinGroup ore_vein_group_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    OreVeinGroup group;
    group.key = to_std_string(def.get("key", ""));
    group.dimension_id = dimension_from_dict(def);
    group.host_material = material_from_dict(
        def, "host_material_key", "host_material_id",
        group.host_material, ids_by_key);
    group.primary_ore = material_from_dict(
        def, "primary_ore_key", "primary_ore_id",
        group.primary_ore, ids_by_key);
    group.secondary_ore = material_from_dict(
        def, "secondary_ore_key", "secondary_ore_id",
        group.secondary_ore, ids_by_key);
    group.between_ore = material_from_dict(
        def, "between_ore_key", "between_ore_id",
        group.between_ore, ids_by_key);
    group.sporadic_ore = material_from_dict(
        def, "sporadic_ore_key", "sporadic_ore_id",
        group.sporadic_ore, ids_by_key);
    group.depth_min = static_cast<float>(
        def.get("depth_min", group.depth_min));
    group.depth_max = static_cast<float>(
        def.get("depth_max", group.depth_max));
    group.radius = static_cast<float>(
        def.get("radius", group.radius));
    group.density = static_cast<float>(
        def.get("density", group.density));
    group.weight = static_cast<float>(
        def.get("weight", group.weight));
    return group;
}

RockLayerRule rock_layer_rule_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    RockLayerRule rule;
    rule.key = to_std_string(def.get("key", ""));
    rule.dimension_id = dimension_from_dict(def);
    rule.rock_material = material_from_dict(
        def, "rock_material_key", "rock_material_id",
        rule.rock_material, ids_by_key);
    rule.noise_scale = static_cast<float>(
        def.get("noise_scale", rule.noise_scale));
    rule.noise_octaves = static_cast<int>(
        def.get("noise_octaves", rule.noise_octaves));
    rule.noise_min = static_cast<float>(
        def.get("noise_min", rule.noise_min));
    rule.noise_max = static_cast<float>(
        def.get("noise_max", rule.noise_max));
    rule.depth_min = static_cast<float>(
        def.get("depth_min", rule.depth_min));
    rule.depth_max = static_cast<float>(
        def.get("depth_max", rule.depth_max));
    rule.hardness_multiplier = static_cast<float>(
        def.get("hardness_multiplier", rule.hardness_multiplier));
    rule.collapse_chance = static_cast<float>(
        def.get("collapse_chance", rule.collapse_chance));

    // Parse associated ores (array of material keys or IDs).
    Array ores = def.get("associated_ores", Array());
    for (int i = 0; i < ores.size(); ++i) {
        TerrainMaterialId ore_id = 0;
        const Variant& v = ores[i];
        if (v.get_type() == Variant::STRING) {
            std::string ore_key = String(v).utf8().get_data();
            auto it = ids_by_key.find(ore_key);
            if (it != ids_by_key.end()) {
                ore_id = it->second;
            }
        } else if (v.get_type() == Variant::INT) {
            ore_id = to_material_id(static_cast<int64_t>(v));
        }
        if (ore_id != 0) {
            rule.associated_ores.push_back(ore_id);
        }
    }

    return rule;
}

PlanetConfig planet_config_from_dict(const Dictionary& def) {
    PlanetConfig config;
    config.dimension_id = dimension_from_dict(def);
    config.planet_radius = static_cast<float>(
        def.get("planet_radius", config.planet_radius));
    config.center_x = static_cast<float>(
        def.get("center_x", config.center_x));
    config.center_y = static_cast<float>(
        def.get("center_y", config.center_y));
    config.center_z = static_cast<float>(
        def.get("center_z", config.center_z));
    config.terrain_height_scale = static_cast<float>(
        def.get("terrain_height_scale", config.terrain_height_scale));
    config.elevation_noise_scale = static_cast<float>(
        def.get("elevation_noise_scale", config.elevation_noise_scale));
    config.elevation_octaves = static_cast<int>(
        def.get("elevation_octaves", config.elevation_octaves));
    config.detail_noise_scale = static_cast<float>(
        def.get("detail_noise_scale", config.detail_noise_scale));
    config.detail_octaves = static_cast<int>(
        def.get("detail_octaves", config.detail_octaves));
    config.cave_noise_scale = static_cast<float>(
        def.get("cave_noise_scale", config.cave_noise_scale));
    config.cave_octaves = static_cast<int>(
        def.get("cave_octaves", config.cave_octaves));
    config.cave_threshold = static_cast<float>(
        def.get("cave_threshold", config.cave_threshold));
    config.sea_level_fraction = static_cast<float>(
        def.get("sea_level_fraction", config.sea_level_fraction));
    config.core_radius_ratio = static_cast<float>(
        def.get("core_radius_ratio", config.core_radius_ratio));
    config.mantle_radius_ratio = static_cast<float>(
        def.get("mantle_radius_ratio", config.mantle_radius_ratio));
    config.core_boundary_noise_scale = static_cast<float>(
        def.get("core_boundary_noise_scale", config.core_boundary_noise_scale));
    config.core_boundary_noise_octaves = static_cast<int>(
        def.get("core_boundary_noise_octaves", config.core_boundary_noise_octaves));
    config.core_boundary_noise_amplitude = static_cast<float>(
        def.get("core_boundary_noise_amplitude", config.core_boundary_noise_amplitude));
    config.atmosphere_type = static_cast<int>(
        def.get("atmosphere_type", config.atmosphere_type));
    return config;
}

} // namespace

GDTerrainContentRegistry::GDTerrainContentRegistry() = default;
GDTerrainContentRegistry::~GDTerrainContentRegistry() = default;

bool GDTerrainContentRegistry::check_mutable(const char* operation) const {
    if (!frozen_) {
        return true;
    }
    UtilityFunctions::push_warning(
        "GDTerrainContentRegistry: cannot ", operation,
        " after freeze(); create a new registry instead.");
    return false;
}

void GDTerrainContentRegistry::rebuild_lookup() {
    material_ids_by_key_.clear();
    material_keys_by_id_.clear();
    for (const auto& material : materials_) {
        material_ids_by_key_[material.key] = material.id;
        material_keys_by_id_[material.id] = material.key;
    }
}

bool GDTerrainContentRegistry::register_material(const Dictionary& def) {
    if (!check_mutable("register material")) {
        return false;
    }

    String key = def.get("key", "");
    if (key.is_empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: material requires a non-empty key.");
        return false;
    }

    if (!def.has("id")) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: material '", key, "' requires an id.");
        return false;
    }

    int64_t raw_id = static_cast<int64_t>(def.get("id", -1));
    if (raw_id < 0 || raw_id > 255) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: material '", key,
            "' id must be in 0..255 while TerrainData stores byte materials.");
        return false;
    }

    TerrainMaterialDef material;
    material.id = static_cast<TerrainMaterialId>(raw_id);
    material.key = key.utf8().get_data();
    material.title_key = String(def.get("title_key", key)).utf8().get_data();
    material.flags = static_cast<uint32_t>(static_cast<int64_t>(
        def.get("flags", 0)));
    material.hardness = static_cast<float>(def.get("hardness", 1.0f));
    material.required_tool_tag = String(def.get("required_tool_tag", "")).utf8().get_data();
    material.required_mining_level = static_cast<int>(
        def.get("required_mining_level", 0));

    // Gravity and collapse properties.
    material.gravity_fall = (material.flags & TF_GRAVITY_FALL) != 0;
    material.collapse_risk = (material.flags & TF_COLLAPSE_RISK) != 0;
    material.collapse_chance = std::clamp(
        static_cast<float>(def.get("collapse_chance", 0.3f)), 0.0f, 1.0f);
    material.support_radius = std::max(
        0, static_cast<int>(def.get("support_radius", 5)));
    material.rock_layer_key = String(
        def.get("rock_layer_key", "")).utf8().get_data();

    Array drops = def.get("drops", Array());
    for (int i = 0; i < drops.size(); ++i) {
        Dictionary drop_dict = drops[i];
        TerrainDropDef drop;
        drop.item_key = String(drop_dict.get("item_key", "")).utf8().get_data();
        drop.count = static_cast<int>(drop_dict.get("count", 1));
        drop.min_count = static_cast<int>(drop_dict.get("min_count", drop.count));
        drop.max_count = static_cast<int>(drop_dict.get("max_count", drop.min_count));
        drop.chance = static_cast<float>(drop_dict.get("chance", 1.0f));
        const int64_t raw_item_id = static_cast<int64_t>(
            drop_dict.get("item_id", static_cast<int64_t>(gt::kInvalidItemId)));
        drop.item_id = raw_item_id > 0
            ? static_cast<gt::ItemId>(raw_item_id)
            : gt::kInvalidItemId;
        if (drop.item_id == gt::kInvalidItemId && !drop.item_key.empty()) {
            gt::ItemRegistry::initialize();
            drop.item_id = gt::ItemRegistry::get_item_id_by_key(drop.item_key.c_str());
        } else if (drop.item_key.empty() && drop.item_id != gt::kInvalidItemId) {
            gt::ItemRegistry::initialize();
            const char* resolved_key = gt::ItemRegistry::get_item_key(drop.item_id);
            if (resolved_key != nullptr) {
                drop.item_key = resolved_key;
            }
        }
        drop.min_count = std::max(1, drop.min_count);
        drop.max_count = std::max(drop.min_count, drop.max_count);
        drop.count = std::max(1, drop.count);
        if ((!drop.item_key.empty() || drop.item_id != gt::kInvalidItemId) &&
                drop.chance > 0.0f) {
            material.drops.push_back(std::move(drop));
        }
    }

    auto existing_key = std::find_if(materials_.begin(), materials_.end(),
        [&material](const TerrainMaterialDef& existing) {
            return existing.key == material.key;
        });
    if (existing_key != materials_.end()) {
        *existing_key = std::move(material);
        rebuild_lookup();
        return true;
    }

    auto existing_id = std::find_if(materials_.begin(), materials_.end(),
        [&material](const TerrainMaterialDef& existing) {
            return existing.id == material.id;
        });
    if (existing_id != materials_.end()) {
        UtilityFunctions::push_warning(
            issue_duplicate_id(material.id, material.key, existing_id->key));
        return false;
    }

    materials_.push_back(std::move(material));
    rebuild_lookup();
    return true;
}

bool GDTerrainContentRegistry::register_material_visual(const Dictionary& def) {
    if (!check_mutable("register material visual")) {
        return false;
    }

    TerrainMaterialVisualDef visual = visual_from_dict(def, material_ids_by_key_);
    if (visual.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: material visual requires a dimension.");
        return false;
    }
    if (!material_keys_by_id_.contains(visual.material_id)) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: material visual references missing material id ",
            static_cast<int>(visual.material_id), ".");
        return false;
    }

    auto existing = std::find_if(material_visuals_.begin(), material_visuals_.end(),
        [&visual](const TerrainMaterialVisualDef& item) {
            return item.material_id == visual.material_id &&
                   item.dimension_id == visual.dimension_id;
        });
    if (existing != material_visuals_.end()) {
        *existing = std::move(visual);
    } else {
        material_visuals_.push_back(std::move(visual));
    }
    return true;
}

bool GDTerrainContentRegistry::set_material_roles(const Dictionary& def) {
    if (!check_mutable("set material roles")) {
        return false;
    }
    assign_role_from_dict(roles_.air, def, "air", material_ids_by_key_);
    assign_role_from_dict(roles_.stone, def, "stone", material_ids_by_key_);
    assign_role_from_dict(roles_.dirt, def, "dirt", material_ids_by_key_);
    assign_role_from_dict(roles_.sand, def, "sand", material_ids_by_key_);
    assign_role_from_dict(roles_.water, def, "water", material_ids_by_key_);
    assign_role_from_dict(roles_.lava, def, "lava", material_ids_by_key_);
    assign_role_from_dict(roles_.ore_iron, def, "ore_iron", material_ids_by_key_);
    assign_role_from_dict(roles_.ore_copper, def, "ore_copper", material_ids_by_key_);
    assign_role_from_dict(roles_.ore_coal, def, "ore_coal", material_ids_by_key_);
    assign_role_from_dict(roles_.wood, def, "wood", material_ids_by_key_);
    assign_role_from_dict(roles_.leaves, def, "leaves", material_ids_by_key_);
    assign_role_from_dict(roles_.deepstone, def, "deepstone", material_ids_by_key_);
    assign_role_from_dict(roles_.core_barrier, def, "core_barrier", material_ids_by_key_);
    return true;
}

bool GDTerrainContentRegistry::set_runtime_material_ids(const Dictionary& def) {
    if (!check_mutable("set runtime material ids")) {
        return false;
    }
    assign_role_from_dict(runtime_ids_.ladder, def, "ladder", material_ids_by_key_);
    assign_role_from_dict(runtime_ids_.workbench, def, "workbench", material_ids_by_key_);
    assign_role_from_dict(runtime_ids_.fence, def, "fence", material_ids_by_key_);
    return true;
}

bool GDTerrainContentRegistry::register_base_terrain_rule(const Dictionary& def) {
    if (!check_mutable("register base terrain rule")) {
        return false;
    }
    BaseTerrainRule rule = base_rule_from_dict(def, material_ids_by_key_);
    if (rule.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: base terrain rule requires a dimension.");
        return false;
    }

    auto existing = std::find_if(base_terrain_rules_.begin(), base_terrain_rules_.end(),
        [&rule](const BaseTerrainRule& item) {
            return item.dimension_id == rule.dimension_id;
        });
    if (existing != base_terrain_rules_.end()) {
        *existing = std::move(rule);
    } else {
        base_terrain_rules_.push_back(std::move(rule));
    }
    return true;
}

bool GDTerrainContentRegistry::register_biome_rule(const Dictionary& def) {
    if (!check_mutable("register biome rule")) {
        return false;
    }
    BiomeRule rule = biome_rule_from_dict(def, material_ids_by_key_);
    if (rule.key.empty() || rule.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: biome rule requires key and dimension.");
        return false;
    }

    auto existing = std::find_if(biome_rules_.begin(), biome_rules_.end(),
        [&rule](const BiomeRule& item) {
            return item.key == rule.key;
        });
    if (existing != biome_rules_.end()) {
        *existing = std::move(rule);
    } else {
        biome_rules_.push_back(std::move(rule));
    }
    return true;
}

bool GDTerrainContentRegistry::register_ore_vein_group(const Dictionary& def) {
    if (!check_mutable("register ore vein group")) {
        return false;
    }
    OreVeinGroup group = ore_vein_group_from_dict(def, material_ids_by_key_);
    if (group.key.empty() || group.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: ore vein group requires key and dimension.");
        return false;
    }
    if (group.primary_ore == 0) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: ore vein group '",
            String(group.key.c_str()),
            "' requires a valid primary_ore.");
        return false;
    }

    auto existing = std::find_if(ore_vein_groups_.begin(), ore_vein_groups_.end(),
        [&group](const OreVeinGroup& item) {
            return item.key == group.key;
        });
    if (existing != ore_vein_groups_.end()) {
        *existing = std::move(group);
    } else {
        ore_vein_groups_.push_back(std::move(group));
    }
    return true;
}

bool GDTerrainContentRegistry::register_rock_layer_rule(const Dictionary& def) {
    if (!check_mutable("register rock layer rule")) {
        return false;
    }

    RockLayerRule rule = rock_layer_rule_from_dict(def, material_ids_by_key_);
    if (rule.key.empty() || rule.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: rock layer rule requires key and dimension.");
        return false;
    }
    if (rule.rock_material == 0) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: rock layer rule '",
            String(rule.key.c_str()),
            "' requires a valid rock_material.");
        return false;
    }

    auto existing = std::find_if(rock_layer_rules_.begin(), rock_layer_rules_.end(),
        [&rule](const RockLayerRule& item) {
            return item.key == rule.key;
        });
    if (existing != rock_layer_rules_.end()) {
        *existing = std::move(rule);
    } else {
        rock_layer_rules_.push_back(std::move(rule));
    }
    return true;
}

bool GDTerrainContentRegistry::register_planet_config(const Dictionary& def) {
    if (!check_mutable("register planet config")) {
        return false;
    }
    PlanetConfig config = planet_config_from_dict(def);
    if (config.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: planet config requires a dimension.");
        return false;
    }
    if (config.planet_radius <= 0.0f) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: planet config requires planet_radius > 0.");
        return false;
    }

    auto existing = std::find_if(planet_configs_.begin(), planet_configs_.end(),
        [&config](const PlanetConfig& item) {
            return item.dimension_id == config.dimension_id;
        });
    if (existing != planet_configs_.end()) {
        *existing = std::move(config);
    } else {
        planet_configs_.push_back(std::move(config));
    }
    return true;
}

bool GDTerrainContentRegistry::register_tree_species(const Dictionary& def) {
    if (!check_mutable("register tree species")) {
        return false;
    }

    TreeSpeciesDef species;
    species.species_key = to_std_string(def.get("species_key", ""));
    species.title_key = to_std_string(def.get("title_key", ""));

    if (species.species_key.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: tree species requires species_key.");
        return false;
    }

    // Biome constraints.
    species.temperature_min = static_cast<float>(def.get("temperature_min", -1.0));
    species.temperature_max = static_cast<float>(def.get("temperature_max", 1.0));
    species.humidity_min = static_cast<float>(def.get("humidity_min", -1.0));
    species.humidity_max = static_cast<float>(def.get("humidity_max", 1.0));
    species.density_weight = static_cast<float>(def.get("density_weight", 1.0));

    // Tree shape.
    species.min_trunk_height = static_cast<int>(def.get("min_trunk_height", 3));
    species.max_trunk_height = static_cast<int>(def.get("max_trunk_height", 5));
    species.canopy_radius = static_cast<int>(def.get("canopy_radius", 2));

    // Canopy shape.
    int canopy_int = static_cast<int>(def.get("canopy_shape", 0));
    if (canopy_int >= 0 && canopy_int < static_cast<int>(CanopyShape::COUNT)) {
        species.canopy_shape = static_cast<CanopyShape>(canopy_int);
    }

    // Material references.
    species.wood_material_key = to_std_string(def.get("wood_material_key", ""));
    species.leaves_material_key = to_std_string(def.get("leaves_material_key", ""));
    species.sapling_material_key = to_std_string(def.get("sapling_material_key", ""));

    // Growth.
    species.is_evergreen = static_cast<bool>(def.get("is_evergreen", false));
    species.ticks_to_young = static_cast<int64_t>(def.get("ticks_to_young", 24000));
    species.ticks_to_mature = static_cast<int64_t>(def.get("ticks_to_mature", 48000));

    // Fruit.
    species.has_fruit = static_cast<bool>(def.get("has_fruit", false));
    species.fruit_item_key = to_std_string(def.get("fruit_item_key", ""));
    species.fruit_season = static_cast<int>(def.get("fruit_season", -1));

    // Visual colors.
    if (def.has("wood_color")) {
        Color c = def.get("wood_color", Color(0.55f, 0.35f, 0.15f));
        species.wood_color_r = c.r;
        species.wood_color_g = c.g;
        species.wood_color_b = c.b;
    } else {
        species.wood_color_r = static_cast<float>(def.get("wood_color_r", 0.55f));
        species.wood_color_g = static_cast<float>(def.get("wood_color_g", 0.35f));
        species.wood_color_b = static_cast<float>(def.get("wood_color_b", 0.15f));
    }
    if (def.has("leaves_color")) {
        Color c = def.get("leaves_color", Color(0.2f, 0.6f, 0.1f));
        species.leaves_color_r = c.r;
        species.leaves_color_g = c.g;
        species.leaves_color_b = c.b;
    } else {
        species.leaves_color_r = static_cast<float>(def.get("leaves_color_r", 0.2f));
        species.leaves_color_g = static_cast<float>(def.get("leaves_color_g", 0.6f));
        species.leaves_color_b = static_cast<float>(def.get("leaves_color_b", 0.1f));
    }
    if (def.has("autumn_color")) {
        Color c = def.get("autumn_color", Color(0.8f, 0.5f, 0.1f));
        species.autumn_color_r = c.r;
        species.autumn_color_g = c.g;
        species.autumn_color_b = c.b;
    } else {
        species.autumn_color_r = static_cast<float>(def.get("autumn_color_r", 0.8f));
        species.autumn_color_g = static_cast<float>(def.get("autumn_color_g", 0.5f));
        species.autumn_color_b = static_cast<float>(def.get("autumn_color_b", 0.1f));
    }

    // Check for duplicate.
    auto existing = std::find_if(tree_species_.begin(), tree_species_.end(),
        [&species](const TreeSpeciesDef& item) {
            return item.species_key == species.species_key;
        });
    if (existing != tree_species_.end()) {
        *existing = std::move(species);
    } else {
        tree_species_.push_back(std::move(species));
    }
    return true;
}

bool GDTerrainContentRegistry::register_crop_species(const Dictionary& def) {
    if (!check_mutable("register crop species")) {
        return false;
    }

    CropSpeciesDef crop;
    crop.species_key = to_std_string(def.get("species_key", ""));
    crop.title_key = to_std_string(def.get("title_key", ""));

    if (crop.species_key.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: crop species requires species_key.");
        return false;
    }

    // Category.
    int category_int = static_cast<int>(def.get("category", 0));
    if (category_int >= 0 && category_int < static_cast<int>(CropCategory::COUNT)) {
        crop.category = static_cast<CropCategory>(category_int);
    }

    // Biome constraints.
    crop.temperature_min = static_cast<float>(def.get("temperature_min", -1.0));
    crop.temperature_max = static_cast<float>(def.get("temperature_max", 1.0));
    crop.humidity_min = static_cast<float>(def.get("humidity_min", -1.0));
    crop.humidity_max = static_cast<float>(def.get("humidity_max", 1.0));

    // Season constraints (-1 = any).
    crop.plant_season = static_cast<int>(def.get("plant_season", -1));
    crop.grow_season = static_cast<int>(def.get("grow_season", -1));
    crop.harvest_season = static_cast<int>(def.get("harvest_season", -1));

    // Growth ticks.
    crop.ticks_seed_to_sprout =
        static_cast<int64_t>(def.get("ticks_seed_to_sprout", 3000));
    crop.ticks_sprout_to_growing =
        static_cast<int64_t>(def.get("ticks_sprout_to_growing", 6000));
    crop.ticks_growing_to_mature =
        static_cast<int64_t>(def.get("ticks_growing_to_mature", 9000));

    // Item production.
    crop.seed_item_key = to_std_string(def.get("seed_item_key", ""));
    crop.crop_item_key = to_std_string(def.get("crop_item_key", ""));
    crop.byproduct_item_key = to_std_string(def.get("byproduct_item_key", ""));
    crop.crop_min = static_cast<int>(def.get("crop_min", 1));
    crop.crop_max = static_cast<int>(def.get("crop_max", 2));
    crop.byproduct_count = static_cast<int>(def.get("byproduct_count", 1));
    crop.repeat_harvest = static_cast<bool>(def.get("repeat_harvest", false));
    crop.regrow_ticks = static_cast<int64_t>(def.get("regrow_ticks", 6000));

    // Stage material keys (array of 4 strings).
    Array stage_keys = def.get("stage_material_keys", Array());
    for (int i = 0; i < 4 && i < stage_keys.size(); ++i) {
        crop.stage_material_keys[i] = to_std_string(stage_keys[i]);
    }

    // Sensitivity.
    crop.fertility_sensitivity =
        static_cast<float>(def.get("fertility_sensitivity", 0.7));
    crop.water_sensitivity =
        static_cast<float>(def.get("water_sensitivity", 0.7));

    // Wild generation.
    crop.wild_spawn = static_cast<bool>(def.get("wild_spawn", false));
    crop.wild_density_weight =
        static_cast<float>(def.get("wild_density_weight", 1.0));

    // Visual color.
    if (def.has("crop_color")) {
        Color c = def.get("crop_color", Color(0.85f, 0.75f, 0.25f));
        crop.crop_color_r = c.r;
        crop.crop_color_g = c.g;
        crop.crop_color_b = c.b;
    } else {
        crop.crop_color_r = static_cast<float>(def.get("crop_color_r", 0.85f));
        crop.crop_color_g = static_cast<float>(def.get("crop_color_g", 0.75f));
        crop.crop_color_b = static_cast<float>(def.get("crop_color_b", 0.25f));
    }

    // Check for duplicate.
    auto existing = std::find_if(crop_species_.begin(), crop_species_.end(),
        [&crop](const CropSpeciesDef& item) {
            return item.species_key == crop.species_key;
        });
    if (existing != crop_species_.end()) {
        *existing = std::move(crop);
    } else {
        crop_species_.push_back(std::move(crop));
    }
    return true;
}

std::shared_ptr<WorldGenConfigSnapshot> GDTerrainContentRegistry::build_snapshot() const {
    auto snapshot = std::make_shared<WorldGenConfigSnapshot>();
    snapshot->materials = materials_;
    snapshot->material_visuals = material_visuals_;
    snapshot->roles = roles_;
    snapshot->runtime_ids = runtime_ids_;
    snapshot->base_terrain_rules = base_terrain_rules_;
    snapshot->biome_rules = biome_rules_;
    snapshot->ore_vein_groups = ore_vein_groups_;
    snapshot->rock_layer_rules = rock_layer_rules_;
    snapshot->planet_configs = planet_configs_;
    snapshot->tree_species = tree_species_;
    snapshot->crop_species = crop_species_;
    snapshot->material_ids_by_key = material_ids_by_key_;
    snapshot->material_keys_by_id = material_keys_by_id_;
    snapshot->content_hash = hash_world_gen_config(*snapshot);
    return snapshot;
}

Ref<GDWorldGenConfig> GDTerrainContentRegistry::freeze() {
    Array issues = validate();
    for (int i = 0; i < issues.size(); ++i) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry validation: ", issues[i]);
    }

    auto snapshot = build_snapshot();
    Ref<GDWorldGenConfig> config;
    config.instantiate();
    config->set_snapshot(snapshot);
    frozen_ = true;
    return config;
}

Array GDTerrainContentRegistry::validate() const {
    Array issues;
    std::unordered_map<std::string, TerrainMaterialId> ids_by_key;
    std::unordered_map<int, std::string> keys_by_id;

    if (materials_.empty()) {
        issues.append("Terrain registry has no materials.");
        return issues;
    }

    for (const auto& material : materials_) {
        if (material.key.empty()) {
            issues.append(issue_material_empty_key(static_cast<int>(material.id)));
        }

        auto key_it = ids_by_key.find(material.key);
        if (key_it != ids_by_key.end()) {
            issues.append(issue_duplicate_key(material.key));
        }
        ids_by_key[material.key] = material.id;

        auto id_it = keys_by_id.find(material.id);
        if (id_it != keys_by_id.end()) {
            issues.append(issue_duplicate_id(material.id, material.key, id_it->second));
        }
        keys_by_id[material.id] = material.key;

        for (const auto& drop : material.drops) {
            if (drop.item_id == gt::kInvalidItemId) {
                issues.append(issue_unresolved_drop_item(material.key, drop.item_key));
            }
        }
    }

    if (!keys_by_id.contains(0)) {
        issues.append("Terrain registry must register id 0 as air.");
    }

    std::unordered_set<std::string> visual_keys;
    for (const auto& visual : material_visuals_) {
        if (!keys_by_id.contains(visual.material_id)) {
            issues.append(issue_missing_visual_material(
                visual.dimension_id, static_cast<int>(visual.material_id)));
            continue;
        }
        if (visual.dimension_id.empty()) {
            issues.append(issue_empty_visual_dimension(
                static_cast<int>(visual.material_id)));
        }

        std::string unique_key = std::to_string(visual.material_id) + "|" + visual.dimension_id;
        if (visual_keys.contains(unique_key)) {
            issues.append(issue_duplicate_visual(
                static_cast<int>(visual.material_id), visual.dimension_id));
        }
        visual_keys.insert(std::move(unique_key));
    }

    const TerrainMaterialId role_values[] = {
        roles_.air, roles_.stone, roles_.dirt, roles_.sand, roles_.water,
        roles_.lava, roles_.ore_iron, roles_.ore_copper, roles_.ore_coal,
        roles_.wood, roles_.leaves,
    };
    for (TerrainMaterialId role_id : role_values) {
        if (!keys_by_id.contains(role_id)) {
            issues.append(String("Terrain material role references missing material id ") +
                          String::num_int64(role_id) + ".");
        }
    }

    for (const auto& rule : base_terrain_rules_) {
        if (rule.dimension_id.empty()) {
            issues.append("Base terrain rule has empty dimension.");
        }
    }
    for (const auto& rule : biome_rules_) {
        if (rule.key.empty() || rule.dimension_id.empty()) {
            issues.append("Biome rule has empty key or dimension.");
        }
    }
    for (const auto& group : ore_vein_groups_) {
        if (group.key.empty() || group.dimension_id.empty()) {
            issues.append("Ore vein group has empty key or dimension.");
        }
        if (group.primary_ore == 0) {
            issues.append(String("Ore vein group '") +
                          String(group.key.c_str()) +
                          "' has no primary ore material.");
        }
    }

    return issues;
}

void GDTerrainContentRegistry::clear() {
    if (!check_mutable("clear")) {
        return;
    }
    materials_.clear();
    material_visuals_.clear();
    roles_ = TerrainMaterialRoles{};
    base_terrain_rules_.clear();
    biome_rules_.clear();
    ore_vein_groups_.clear();
    rock_layer_rules_.clear();
    planet_configs_.clear();
    tree_species_.clear();
    crop_species_.clear();
    material_ids_by_key_.clear();
    material_keys_by_id_.clear();
}

bool GDTerrainContentRegistry::is_frozen() const {
    return frozen_;
}

int64_t GDTerrainContentRegistry::get_material_id(const String& key) const {
    std::string key_str = key.utf8().get_data();
    auto it = material_ids_by_key_.find(key_str);
    if (it == material_ids_by_key_.end()) {
        return -1;
    }
    return static_cast<int64_t>(it->second);
}

String GDTerrainContentRegistry::get_material_key(int64_t id) const {
    auto it = material_keys_by_id_.find(static_cast<int>(id));
    if (it == material_keys_by_id_.end()) {
        return String();
    }
    return String(it->second.c_str());
}

int64_t GDTerrainContentRegistry::get_material_count() const {
    return static_cast<int64_t>(materials_.size());
}

int64_t GDTerrainContentRegistry::get_material_visual_count() const {
    return static_cast<int64_t>(material_visuals_.size());
}

void GDTerrainContentRegistry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_material", "def"),
                         &GDTerrainContentRegistry::register_material);
    ClassDB::bind_method(D_METHOD("register_material_visual", "def"),
                         &GDTerrainContentRegistry::register_material_visual);
    ClassDB::bind_method(D_METHOD("set_material_roles", "def"),
                         &GDTerrainContentRegistry::set_material_roles);
    ClassDB::bind_method(D_METHOD("set_runtime_material_ids", "def"),
                         &GDTerrainContentRegistry::set_runtime_material_ids);
    ClassDB::bind_method(D_METHOD("register_base_terrain_rule", "def"),
                         &GDTerrainContentRegistry::register_base_terrain_rule);
    ClassDB::bind_method(D_METHOD("register_biome_rule", "def"),
                         &GDTerrainContentRegistry::register_biome_rule);
    ClassDB::bind_method(D_METHOD("register_ore_vein_group", "def"),
                         &GDTerrainContentRegistry::register_ore_vein_group);
    ClassDB::bind_method(D_METHOD("register_rock_layer_rule", "def"),
                         &GDTerrainContentRegistry::register_rock_layer_rule);
    ClassDB::bind_method(D_METHOD("register_planet_config", "def"),
                         &GDTerrainContentRegistry::register_planet_config);
    ClassDB::bind_method(D_METHOD("register_tree_species", "def"),
                         &GDTerrainContentRegistry::register_tree_species);
    ClassDB::bind_method(D_METHOD("register_crop_species", "def"),
                         &GDTerrainContentRegistry::register_crop_species);
    ClassDB::bind_method(D_METHOD("freeze"),
                         &GDTerrainContentRegistry::freeze);
    ClassDB::bind_method(D_METHOD("validate"),
                         &GDTerrainContentRegistry::validate);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDTerrainContentRegistry::clear);
    ClassDB::bind_method(D_METHOD("is_frozen"),
                         &GDTerrainContentRegistry::is_frozen);
    ClassDB::bind_method(D_METHOD("get_material_id", "key"),
                         &GDTerrainContentRegistry::get_material_id);
    ClassDB::bind_method(D_METHOD("get_material_key", "id"),
                         &GDTerrainContentRegistry::get_material_key);
    ClassDB::bind_method(D_METHOD("get_material_count"),
                         &GDTerrainContentRegistry::get_material_count);
    ClassDB::bind_method(D_METHOD("get_material_visual_count"),
                         &GDTerrainContentRegistry::get_material_visual_count);

    BIND_ENUM_CONSTANT(FLAG_WALKABLE);
    BIND_ENUM_CONSTANT(FLAG_SOLID);
    BIND_ENUM_CONSTANT(FLAG_LIQUID);
    BIND_ENUM_CONSTANT(FLAG_MINEABLE);
    BIND_ENUM_CONSTANT(FLAG_CLIMBABLE);
    BIND_ENUM_CONSTANT(FLAG_INDESTRUCTIBLE);
}

} // namespace science_and_theology
