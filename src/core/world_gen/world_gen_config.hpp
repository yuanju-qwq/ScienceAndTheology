#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/resource_types.hpp"
#include "../world/terrain_data.hpp"

namespace science_and_theology {

struct TerrainDropDef {
    std::string item_key;
    gt::ItemId item_id = gt::kInvalidItemId;
    int count = 1;
    int min_count = 1;
    int max_count = 1;
    float chance = 1.0f;
};

struct TerrainMaterialDef {
    TerrainMaterialId id = 0;
    std::string key;
    std::string display_name;
    uint32_t flags = 0;
    float hardness = 1.0f;
    std::string required_tool_tag;
    int required_mining_level = 0;
    std::vector<TerrainDropDef> drops;
};

struct TerrainTileMapping {
    TerrainMaterialId material_id = 0;
    std::string material_key;
    std::string layer_id;
    int source_id = 0;
    int atlas_x = 0;
    int atlas_y = 0;
    int variant_count = 1;
    bool enabled = true;
};

struct TerrainMaterialRoles {
    TerrainMaterialId air = 0;
    TerrainMaterialId stone = 0;
    TerrainMaterialId dirt = 0;
    TerrainMaterialId sand = 0;
    TerrainMaterialId water = 0;
    TerrainMaterialId lava = 0;
    TerrainMaterialId ore_iron = 0;
    TerrainMaterialId ore_copper = 0;
    TerrainMaterialId ore_coal = 0;
    TerrainMaterialId wood = 0;
    TerrainMaterialId leaves = 0;
};

struct BaseTerrainRule {
    std::string layer_id;
    std::string mode = "solid";
    TerrainMaterialId default_material = 0;
    TerrainMaterialId low_elevation_material = 0;
    TerrainMaterialId high_elevation_material = 0;
    TerrainMaterialId cave_air_material = 0;
    float elevation_scale = 0.02f;
    int elevation_octaves = 4;
    float detail_scale = 0.05f;
    int detail_octaves = 3;
    float water_elevation_max = -0.25f;
    float water_detail_max = 0.3f;
    float stone_elevation_abs_min = 0.55f;
    float cave_scale = 0.04f;
    int cave_octaves = 4;
    float cave_threshold = 0.35f;
    float cave_edge_threshold_add = 0.25f;
};

struct BiomeRule {
    std::string key;
    std::string layer_id;
    TerrainMaterialId source_material = 0;
    TerrainMaterialId result_material = 0;
    std::string condition = "temperature_humidity";
    float temperature_min = -1.0f;
    float temperature_max = 1.0f;
    float humidity_min = -1.0f;
    float humidity_max = 1.0f;
    bool requires_near_material = false;
    TerrainMaterialId near_material = 0;
    int near_radius = 2;
    bool requires_floor_support = false;
    TerrainMaterialId support_material = 0;
    float detail_scale = 0.1f;
    int detail_octaves = 2;
    float detail_threshold = -1.0f;
};

struct OreVeinRule {
    std::string key;
    std::string layer_id = "underground";
    TerrainMaterialId host_material = 0;
    TerrainMaterialId ore_material = 0;
    float combined_min = 0.5f;
    float combined_max = 1.0f;
};

struct WorldGenConfigSnapshot {
    static constexpr uint32_t kSchemaVersion = 2;

    uint32_t schema_version = kSchemaVersion;
    uint64_t content_hash = 0;
    std::vector<TerrainMaterialDef> materials;
    std::vector<TerrainTileMapping> tile_mappings;
    TerrainMaterialRoles roles;
    std::vector<BaseTerrainRule> base_terrain_rules;
    std::vector<BiomeRule> biome_rules;
    std::vector<OreVeinRule> ore_vein_rules;
    std::unordered_map<std::string, TerrainMaterialId> material_ids_by_key;
    std::unordered_map<int, std::string> material_keys_by_id;

    const TerrainMaterialDef* find_material(TerrainMaterialId id) const;
    const TerrainMaterialDef* find_material(const std::string& key) const;
    TerrainMaterialId material_id_or(const std::string& key, TerrainMaterialId fallback) const;
    uint32_t flags_for_material(TerrainMaterialId id) const;
    bool has_material(TerrainMaterialId id) const;
    bool has_material_key(const std::string& key) const;
    bool is_role(TerrainMaterialId id, TerrainMaterialId role_id) const;
    bool is_walkable_ground(TerrainMaterialId id) const;
    const BaseTerrainRule* find_base_rule(const std::string& layer_id) const;
};

std::shared_ptr<const WorldGenConfigSnapshot> make_empty_world_gen_config();
uint64_t hash_world_gen_config(const WorldGenConfigSnapshot& config);

} // namespace science_and_theology
