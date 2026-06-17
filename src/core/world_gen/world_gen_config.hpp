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
    std::string dimension_id = "overworld";
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
    TerrainMaterialId deepstone = 0;
    TerrainMaterialId core_barrier = 0;
};

// Runtime material IDs for blocks placed by players, NOT by terrain generation.
// These are resolved from the same material registry but are not consumed
// by any terrain pass. The command server uses these to write terrain cells.
struct RuntimeMaterialIds {
    TerrainMaterialId ladder = 0;
    TerrainMaterialId workbench = 0;
};

struct BaseTerrainRule {
    std::string dimension_id = "overworld";
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
    std::string dimension_id = "overworld";
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
    std::string dimension_id = "overworld";
    TerrainMaterialId host_material = 0;
    TerrainMaterialId ore_material = 0;
    float combined_min = 0.5f;
    float combined_max = 1.0f;
};

// Planet configuration for spherical world generation.
// Each dimension can optionally be a planet with a defined radius and center.
// When planet_radius > 0, the terrain generator uses spherical clipping
// instead of the flat infinite-plane model.
struct PlanetConfig {
    std::string dimension_id = "overworld";

    // Radius of the planet in voxel blocks. 0 means flat world (no planet).
    float planet_radius = 0.0f;

    // Center of the planet in world voxel coordinates.
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;

    // Maximum terrain displacement from the base sphere surface.
    // Controls mountain height and ocean depth.
    float terrain_height_scale = 16.0f;

    // Noise scale for the 3D elevation noise applied on the sphere.
    float elevation_noise_scale = 0.008f;

    // Number of octaves for the elevation noise.
    int elevation_octaves = 5;

    // Noise scale for detail/roughness on the sphere surface.
    float detail_noise_scale = 0.03f;

    // Number of octaves for detail noise.
    int detail_octaves = 3;

    // Noise scale for cave generation inside the planet.
    float cave_noise_scale = 0.04f;

    // Number of octaves for cave noise.
    int cave_octaves = 4;

    // Threshold for cave generation. Higher = fewer caves.
    float cave_threshold = 0.35f;

    // Sea level as a fraction of terrain_height_scale above the base radius.
    // 0.0 = sea at base radius, 1.0 = sea at max terrain height.
    float sea_level_fraction = 0.3f;

    // Core radius as a fraction of planet_radius.
    // Blocks within this radius are indestructible core_barrier material.
    // Prevents players from reaching the gravity singularity at the center.
    float core_radius_ratio = 0.05f;

    // Mantle radius as a fraction of planet_radius.
    // Between core and mantle: outer core (lava zone).
    // Between mantle and surface: crust (stone + caves + ores).
    float mantle_radius_ratio = 0.5f;

    // Noise scale for perturbing the core boundary.
    // Makes the core shape irregular instead of a perfect sphere.
    float core_boundary_noise_scale = 0.02f;

    // Number of octaves for core boundary noise.
    int core_boundary_noise_octaves = 3;

    // Amplitude of core boundary noise as a fraction of core radius.
    // 0.15 = core boundary can deviate by ±15% of core radius.
    float core_boundary_noise_amplitude = 0.15f;

    bool is_planet() const { return planet_radius > 0.0f; }
};

struct WorldGenConfigSnapshot {
    static constexpr uint32_t kSchemaVersion = 4;

    uint32_t schema_version = kSchemaVersion;
    uint64_t content_hash = 0;
    std::vector<TerrainMaterialDef> materials;
    std::vector<TerrainTileMapping> tile_mappings;
    TerrainMaterialRoles roles;
    RuntimeMaterialIds runtime_ids;
    std::vector<BaseTerrainRule> base_terrain_rules;
    std::vector<BiomeRule> biome_rules;
    std::vector<OreVeinRule> ore_vein_rules;
    std::vector<PlanetConfig> planet_configs;
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
    const BaseTerrainRule* find_base_rule(const std::string& dimension_id) const;
    const PlanetConfig* find_planet_config(const std::string& dimension_id) const;
};

std::shared_ptr<const WorldGenConfigSnapshot> make_empty_world_gen_config();
uint64_t hash_world_gen_config(const WorldGenConfigSnapshot& config);

} // namespace science_and_theology
