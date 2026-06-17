#include "world_gen_config.hpp"

#include <algorithm>
#include <functional>

namespace science_and_theology {
namespace {

void hash_combine(uint64_t& seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

} // namespace

const TerrainMaterialDef* WorldGenConfigSnapshot::find_material(
    TerrainMaterialId id) const {
    auto it = std::find_if(materials.begin(), materials.end(),
        [id](const TerrainMaterialDef& def) {
            return def.id == id;
        });
    return it != materials.end() ? &(*it) : nullptr;
}

const TerrainMaterialDef* WorldGenConfigSnapshot::find_material(
    const std::string& key) const {
    auto id_it = material_ids_by_key.find(key);
    if (id_it == material_ids_by_key.end()) {
        return nullptr;
    }
    return find_material(id_it->second);
}

TerrainMaterialId WorldGenConfigSnapshot::material_id_or(
    const std::string& key, TerrainMaterialId fallback) const {
    auto id_it = material_ids_by_key.find(key);
    if (id_it == material_ids_by_key.end()) {
        return fallback;
    }
    return id_it->second;
}

uint32_t WorldGenConfigSnapshot::flags_for_material(TerrainMaterialId id) const {
    if (const auto* def = find_material(id)) {
        return def->flags;
    }
    return 0;
}

bool WorldGenConfigSnapshot::has_material(TerrainMaterialId id) const {
    return material_keys_by_id.find(id) != material_keys_by_id.end();
}

bool WorldGenConfigSnapshot::has_material_key(const std::string& key) const {
    return material_ids_by_key.find(key) != material_ids_by_key.end();
}

bool WorldGenConfigSnapshot::is_role(TerrainMaterialId id, TerrainMaterialId role_id) const {
    return id == role_id;
}

bool WorldGenConfigSnapshot::is_walkable_ground(TerrainMaterialId id) const {
    return id == roles.dirt || id == roles.sand;
}

const BaseTerrainRule* WorldGenConfigSnapshot::find_base_rule(
    const std::string& dimension_id) const {
    auto it = std::find_if(base_terrain_rules.begin(), base_terrain_rules.end(),
        [&dimension_id](const BaseTerrainRule& rule) {
            return rule.dimension_id == dimension_id;
        });
    return it != base_terrain_rules.end() ? &(*it) : nullptr;
}

const PlanetConfig* WorldGenConfigSnapshot::find_planet_config(
    const std::string& dimension_id) const {
    auto it = std::find_if(planet_configs.begin(), planet_configs.end(),
        [&dimension_id](const PlanetConfig& config) {
            return config.dimension_id == dimension_id;
        });
    return it != planet_configs.end() ? &(*it) : nullptr;
}

std::shared_ptr<const WorldGenConfigSnapshot> make_empty_world_gen_config() {
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    config->content_hash = hash_world_gen_config(*config);
    return config;
}

uint64_t hash_world_gen_config(const WorldGenConfigSnapshot& config) {
    uint64_t hash = 1469598103934665603ULL;
    hash_combine(hash, config.schema_version);

    std::hash<std::string> string_hash;
    for (const auto& material : config.materials) {
        hash_combine(hash, material.id);
        hash_combine(hash, string_hash(material.key));
        hash_combine(hash, string_hash(material.display_name));
        hash_combine(hash, material.flags);
        hash_combine(hash, static_cast<uint64_t>(material.hardness * 1000.0f));
        hash_combine(hash, string_hash(material.required_tool_tag));
        hash_combine(hash, static_cast<uint64_t>(material.required_mining_level));
        for (const auto& drop : material.drops) {
            hash_combine(hash, string_hash(drop.item_key));
            hash_combine(hash, static_cast<uint64_t>(drop.item_id));
            hash_combine(hash, static_cast<uint64_t>(drop.count));
            hash_combine(hash, static_cast<uint64_t>(drop.min_count));
            hash_combine(hash, static_cast<uint64_t>(drop.max_count));
            hash_combine(hash, static_cast<uint64_t>(drop.chance * 100000.0f));
        }
    }
    for (const auto& mapping : config.tile_mappings) {
        hash_combine(hash, mapping.material_id);
        hash_combine(hash, string_hash(mapping.material_key));
        hash_combine(hash, string_hash(mapping.dimension_id));
        hash_combine(hash, static_cast<uint64_t>(mapping.source_id));
        hash_combine(hash, static_cast<uint64_t>(mapping.atlas_x));
        hash_combine(hash, static_cast<uint64_t>(mapping.atlas_y));
        hash_combine(hash, static_cast<uint64_t>(mapping.variant_count));
        hash_combine(hash, mapping.enabled ? 1ULL : 0ULL);
    }
    hash_combine(hash, config.roles.air);
    hash_combine(hash, config.roles.stone);
    hash_combine(hash, config.roles.dirt);
    hash_combine(hash, config.roles.sand);
    hash_combine(hash, config.roles.water);
    hash_combine(hash, config.roles.lava);
    hash_combine(hash, config.roles.ore_iron);
    hash_combine(hash, config.roles.ore_copper);
    hash_combine(hash, config.roles.ore_coal);
    hash_combine(hash, config.roles.wood);
    hash_combine(hash, config.roles.leaves);
    hash_combine(hash, config.roles.deepstone);
    hash_combine(hash, config.roles.core_barrier);
    for (const auto& rule : config.base_terrain_rules) {
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, string_hash(rule.mode));
        hash_combine(hash, rule.default_material);
        hash_combine(hash, rule.low_elevation_material);
        hash_combine(hash, rule.high_elevation_material);
        hash_combine(hash, rule.cave_air_material);
        hash_combine(hash, static_cast<uint64_t>(rule.elevation_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.elevation_octaves));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>((rule.water_elevation_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.water_detail_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.stone_elevation_abs_min * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_octaves));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_threshold * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.cave_edge_threshold_add * 100000.0f));
    }
    for (const auto& rule : config.biome_rules) {
        hash_combine(hash, string_hash(rule.key));
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, rule.source_material);
        hash_combine(hash, rule.result_material);
        hash_combine(hash, string_hash(rule.condition));
        hash_combine(hash, static_cast<uint64_t>((rule.temperature_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.temperature_max + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.humidity_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.humidity_max + 2.0f) * 100000.0f));
        hash_combine(hash, rule.requires_near_material ? 1ULL : 0ULL);
        hash_combine(hash, rule.near_material);
        hash_combine(hash, static_cast<uint64_t>(rule.near_radius));
        hash_combine(hash, rule.requires_floor_support ? 1ULL : 0ULL);
        hash_combine(hash, rule.support_material);
        hash_combine(hash, static_cast<uint64_t>(rule.detail_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(rule.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>((rule.detail_threshold + 2.0f) * 100000.0f));
    }
    for (const auto& rule : config.ore_vein_rules) {
        hash_combine(hash, string_hash(rule.key));
        hash_combine(hash, string_hash(rule.dimension_id));
        hash_combine(hash, rule.host_material);
        hash_combine(hash, rule.ore_material);
        hash_combine(hash, static_cast<uint64_t>((rule.combined_min + 2.0f) * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>((rule.combined_max + 2.0f) * 100000.0f));
    }
    for (const auto& planet : config.planet_configs) {
        hash_combine(hash, string_hash(planet.dimension_id));
        hash_combine(hash, static_cast<uint64_t>(planet.planet_radius * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_x * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_y * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.center_z * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.terrain_height_scale * 1000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.elevation_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.elevation_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.detail_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.detail_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.cave_threshold * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.sea_level_fraction * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_radius_ratio * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.mantle_radius_ratio * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_scale * 100000.0f));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_octaves));
        hash_combine(hash, static_cast<uint64_t>(planet.core_boundary_noise_amplitude * 100000.0f));
    }
    return hash;
}

} // namespace science_and_theology
