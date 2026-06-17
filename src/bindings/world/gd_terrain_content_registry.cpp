#include "gd_terrain_content_registry.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2i.hpp>

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

String issue_missing_mapping_material(
    const std::string& dimension_id, int material_id) {
    std::ostringstream out;
    out << "Tile mapping for dimension '" << dimension_id
        << "' references missing material id " << material_id << ".";
    return String(out.str().c_str());
}

String issue_empty_mapping_dimension(int material_id) {
    std::ostringstream out;
    out << "Tile mapping for material id " << material_id
        << " has empty dimension.";
    return String(out.str().c_str());
}

String issue_bad_variant_count(int material_id, const std::string& dimension_id) {
    std::ostringstream out;
    out << "Tile mapping for material id " << material_id
        << " dimension '" << dimension_id << "' has variant_count < 1.";
    return String(out.str().c_str());
}

String issue_duplicate_mapping(int material_id, const std::string& dimension_id) {
    std::ostringstream out;
    out << "Duplicate tile mapping for material id " << material_id
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

TerrainTileMapping mapping_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    TerrainTileMapping mapping;

    mapping.dimension_id = dimension_from_dict(def);
    mapping.source_id = static_cast<int>(def.get("source_id", 0));
    mapping.variant_count = std::max(1, static_cast<int>(def.get("variant_count", 1)));
    mapping.enabled = static_cast<bool>(def.get("enabled", true));

    if (def.has("atlas")) {
        Vector2i atlas = def.get("atlas", Vector2i(0, 0));
        mapping.atlas_x = atlas.x;
        mapping.atlas_y = atlas.y;
    } else {
        mapping.atlas_x = static_cast<int>(def.get("atlas_x", 0));
        mapping.atlas_y = static_cast<int>(def.get("atlas_y", 0));
    }

    if (def.has("material_key")) {
        mapping.material_key = to_std_string(def.get("material_key", ""));
        auto it = ids_by_key.find(mapping.material_key);
        if (it != ids_by_key.end()) {
            mapping.material_id = it->second;
        }
    }

    if (def.has("material_id")) {
        mapping.material_id = to_material_id(static_cast<int64_t>(
            def.get("material_id", static_cast<int>(mapping.material_id))));
    } else if (def.has("id")) {
        mapping.material_id = to_material_id(static_cast<int64_t>(
            def.get("id", static_cast<int>(mapping.material_id))));
    }

    if (mapping.material_key.empty()) {
        auto key_it = std::find_if(ids_by_key.begin(), ids_by_key.end(),
            [&mapping](const auto& pair) {
                return pair.second == mapping.material_id;
            });
        if (key_it != ids_by_key.end()) {
            mapping.material_key = key_it->first;
        }
    }

    return mapping;
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

OreVeinRule ore_rule_from_dict(
    const Dictionary& def,
    const std::unordered_map<std::string, TerrainMaterialId>& ids_by_key) {
    OreVeinRule rule;
    rule.key = to_std_string(def.get("key", ""));
    rule.dimension_id = dimension_from_dict(def);
    rule.host_material = material_from_dict(
        def, "host_material_key", "host_material_id",
        rule.host_material, ids_by_key);
    rule.ore_material = material_from_dict(
        def, "ore_material_key", "ore_material_id",
        rule.ore_material, ids_by_key);
    rule.combined_min = static_cast<float>(
        def.get("combined_min", rule.combined_min));
    rule.combined_max = static_cast<float>(
        def.get("combined_max", rule.combined_max));
    return rule;
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
    material.display_name = String(def.get("display_name", key)).utf8().get_data();
    material.flags = static_cast<uint32_t>(static_cast<int64_t>(
        def.get("flags", 0)));
    material.hardness = static_cast<float>(def.get("hardness", 1.0f));
    material.required_tool_tag = String(def.get("required_tool_tag", "")).utf8().get_data();
    material.required_mining_level = static_cast<int>(
        def.get("required_mining_level", 0));

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

bool GDTerrainContentRegistry::register_tile_mapping(const Dictionary& def) {
    if (!check_mutable("register tile mapping")) {
        return false;
    }

    TerrainTileMapping mapping = mapping_from_dict(def, material_ids_by_key_);
    if (mapping.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: tile mapping requires a dimension.");
        return false;
    }
    if (!material_keys_by_id_.contains(mapping.material_id)) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: tile mapping references missing material id ",
            static_cast<int>(mapping.material_id), ".");
        return false;
    }

    auto existing = std::find_if(tile_mappings_.begin(), tile_mappings_.end(),
        [&mapping](const TerrainTileMapping& item) {
            return item.material_id == mapping.material_id &&
                   item.dimension_id == mapping.dimension_id;
        });
    if (existing != tile_mappings_.end()) {
        *existing = std::move(mapping);
    } else {
        tile_mappings_.push_back(std::move(mapping));
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

bool GDTerrainContentRegistry::register_ore_vein_rule(const Dictionary& def) {
    if (!check_mutable("register ore vein rule")) {
        return false;
    }
    OreVeinRule rule = ore_rule_from_dict(def, material_ids_by_key_);
    if (rule.key.empty() || rule.dimension_id.empty()) {
        UtilityFunctions::push_warning(
            "GDTerrainContentRegistry: ore vein rule requires key and dimension.");
        return false;
    }

    auto existing = std::find_if(ore_vein_rules_.begin(), ore_vein_rules_.end(),
        [&rule](const OreVeinRule& item) {
            return item.key == rule.key;
        });
    if (existing != ore_vein_rules_.end()) {
        *existing = std::move(rule);
    } else {
        ore_vein_rules_.push_back(std::move(rule));
    }
    return true;
}

std::shared_ptr<WorldGenConfigSnapshot> GDTerrainContentRegistry::build_snapshot() const {
    auto snapshot = std::make_shared<WorldGenConfigSnapshot>();
    snapshot->materials = materials_;
    snapshot->tile_mappings = tile_mappings_;
    snapshot->roles = roles_;
    snapshot->base_terrain_rules = base_terrain_rules_;
    snapshot->biome_rules = biome_rules_;
    snapshot->ore_vein_rules = ore_vein_rules_;
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

    std::unordered_set<std::string> mapping_keys;
    for (const auto& mapping : tile_mappings_) {
        if (!keys_by_id.contains(mapping.material_id)) {
            issues.append(issue_missing_mapping_material(
                mapping.dimension_id, static_cast<int>(mapping.material_id)));
            continue;
        }
        if (mapping.dimension_id.empty()) {
            issues.append(issue_empty_mapping_dimension(
                static_cast<int>(mapping.material_id)));
        }
        if (mapping.variant_count < 1) {
            issues.append(issue_bad_variant_count(
                static_cast<int>(mapping.material_id), mapping.dimension_id));
        }

        std::string unique_key = std::to_string(mapping.material_id) + "|" + mapping.dimension_id;
        if (mapping_keys.contains(unique_key)) {
            issues.append(issue_duplicate_mapping(
                static_cast<int>(mapping.material_id), mapping.dimension_id));
        }
        mapping_keys.insert(std::move(unique_key));
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
    for (const auto& rule : ore_vein_rules_) {
        if (rule.key.empty() || rule.dimension_id.empty()) {
            issues.append("Ore vein rule has empty key or dimension.");
        }
    }

    return issues;
}

void GDTerrainContentRegistry::clear() {
    if (!check_mutable("clear")) {
        return;
    }
    materials_.clear();
    tile_mappings_.clear();
    roles_ = TerrainMaterialRoles{};
    base_terrain_rules_.clear();
    biome_rules_.clear();
    ore_vein_rules_.clear();
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

int64_t GDTerrainContentRegistry::get_tile_mapping_count() const {
    return static_cast<int64_t>(tile_mappings_.size());
}

void GDTerrainContentRegistry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_material", "def"),
                         &GDTerrainContentRegistry::register_material);
    ClassDB::bind_method(D_METHOD("register_tile_mapping", "def"),
                         &GDTerrainContentRegistry::register_tile_mapping);
    ClassDB::bind_method(D_METHOD("set_material_roles", "def"),
                         &GDTerrainContentRegistry::set_material_roles);
    ClassDB::bind_method(D_METHOD("register_base_terrain_rule", "def"),
                         &GDTerrainContentRegistry::register_base_terrain_rule);
    ClassDB::bind_method(D_METHOD("register_biome_rule", "def"),
                         &GDTerrainContentRegistry::register_biome_rule);
    ClassDB::bind_method(D_METHOD("register_ore_vein_rule", "def"),
                         &GDTerrainContentRegistry::register_ore_vein_rule);
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
    ClassDB::bind_method(D_METHOD("get_tile_mapping_count"),
                         &GDTerrainContentRegistry::get_tile_mapping_count);

    BIND_ENUM_CONSTANT(FLAG_WALKABLE);
    BIND_ENUM_CONSTANT(FLAG_SOLID);
    BIND_ENUM_CONSTANT(FLAG_LIQUID);
    BIND_ENUM_CONSTANT(FLAG_MINEABLE);
}

} // namespace science_and_theology
