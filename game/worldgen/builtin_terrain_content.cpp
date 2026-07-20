// Built-in game-owned terrain content registration.

#define SNT_LOG_CHANNEL "game.worldgen_content"
#include "game/worldgen/builtin_terrain_content.h"

#include "core/error.h"
#include "core/log.h"
#include "game/worldgen/builtin_terrain_material_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace snt::game {
namespace {

void add_tree_species(WorldGenConfigSnapshot& config, TreeSpeciesDef species) {
    config.tree_species.push_back(std::move(species));
}

void add_crop_species(WorldGenConfigSnapshot& config, CropSpeciesDef species) {
    config.crop_species.push_back(std::move(species));
}

void register_builtin_tree_species(WorldGenConfigSnapshot& config) {
    add_tree_species(config, {
        .species_key = "oak",
        .title_key = "tree.oak",
        .temperature_min = -0.2f,
        .temperature_max = 0.5f,
        .humidity_min = 0.0f,
        .humidity_max = 1.0f,
        .density_weight = 1.2f,
        .min_trunk_height = 3,
        .max_trunk_height = 5,
        .canopy_shape = CanopyShape::SPHERE,
        .canopy_radius = 2,
        .wood_material_key = "snt:oak_wood",
        .leaves_material_key = "snt:oak_leaves",
        .sapling_material_key = "snt:oak_sapling",
        .ticks_to_young = 24000,
        .ticks_to_mature = 48000,
        .wood_color_r = 0.45f,
        .wood_color_g = 0.27f,
        .wood_color_b = 0.12f,
        .leaves_color_r = 0.21f,
        .leaves_color_g = 0.42f,
        .leaves_color_b = 0.15f,
        .autumn_color_r = 0.75f,
        .autumn_color_g = 0.50f,
        .autumn_color_b = 0.12f,
    });
    add_tree_species(config, {
        .species_key = "birch",
        .title_key = "tree.birch",
        .temperature_min = -0.6f,
        .temperature_max = 0.2f,
        .humidity_min = 0.1f,
        .humidity_max = 1.0f,
        .density_weight = 0.8f,
        .min_trunk_height = 5,
        .max_trunk_height = 8,
        .canopy_shape = CanopyShape::COLUMN,
        .canopy_radius = 1,
        .wood_material_key = "snt:birch_wood",
        .leaves_material_key = "snt:birch_leaves",
        .sapling_material_key = "snt:birch_sapling",
        .ticks_to_young = 20000,
        .ticks_to_mature = 40000,
        .wood_color_r = 0.80f,
        .wood_color_g = 0.78f,
        .wood_color_b = 0.70f,
        .leaves_color_r = 0.25f,
        .leaves_color_g = 0.55f,
        .leaves_color_b = 0.18f,
        .autumn_color_r = 0.85f,
        .autumn_color_g = 0.75f,
        .autumn_color_b = 0.15f,
    });
    add_tree_species(config, {
        .species_key = "spruce",
        .title_key = "tree.spruce",
        .temperature_min = -1.0f,
        .temperature_max = -0.1f,
        .humidity_min = 0.0f,
        .humidity_max = 1.0f,
        .density_weight = 1.0f,
        .min_trunk_height = 4,
        .max_trunk_height = 7,
        .canopy_shape = CanopyShape::CONE,
        .canopy_radius = 2,
        .wood_material_key = "snt:spruce_wood",
        .leaves_material_key = "snt:spruce_leaves",
        .sapling_material_key = "snt:spruce_sapling",
        .is_evergreen = true,
        .ticks_to_young = 30000,
        .ticks_to_mature = 60000,
        .wood_color_r = 0.35f,
        .wood_color_g = 0.22f,
        .wood_color_b = 0.10f,
        .leaves_color_r = 0.10f,
        .leaves_color_g = 0.30f,
        .leaves_color_b = 0.12f,
        .autumn_color_r = 0.10f,
        .autumn_color_g = 0.30f,
        .autumn_color_b = 0.12f,
    });
    add_tree_species(config, {
        .species_key = "acacia",
        .title_key = "tree.acacia",
        .temperature_min = 0.5f,
        .temperature_max = 1.0f,
        .humidity_min = -0.5f,
        .humidity_max = 0.5f,
        .density_weight = 0.7f,
        .min_trunk_height = 3,
        .max_trunk_height = 5,
        .canopy_shape = CanopyShape::UMBRELLA,
        .canopy_radius = 3,
        .wood_material_key = "snt:acacia_wood",
        .leaves_material_key = "snt:acacia_leaves",
        .sapling_material_key = "snt:acacia_sapling",
        .ticks_to_young = 28000,
        .ticks_to_mature = 56000,
        .wood_color_r = 0.55f,
        .wood_color_g = 0.35f,
        .wood_color_b = 0.15f,
        .leaves_color_r = 0.30f,
        .leaves_color_g = 0.55f,
        .leaves_color_b = 0.12f,
        .autumn_color_r = 0.70f,
        .autumn_color_g = 0.55f,
        .autumn_color_b = 0.10f,
    });
    add_tree_species(config, {
        .species_key = "maple",
        .title_key = "tree.maple",
        .temperature_min = -0.3f,
        .temperature_max = 0.4f,
        .humidity_min = 0.1f,
        .humidity_max = 1.0f,
        .density_weight = 0.6f,
        .min_trunk_height = 3,
        .max_trunk_height = 6,
        .canopy_shape = CanopyShape::SPHERE,
        .canopy_radius = 2,
        .wood_material_key = "snt:maple_wood",
        .leaves_material_key = "snt:maple_leaves",
        .sapling_material_key = "snt:maple_sapling",
        .ticks_to_young = 26000,
        .ticks_to_mature = 52000,
        .wood_color_r = 0.42f,
        .wood_color_g = 0.25f,
        .wood_color_b = 0.10f,
        .leaves_color_r = 0.20f,
        .leaves_color_g = 0.50f,
        .leaves_color_b = 0.15f,
        .autumn_color_r = 0.90f,
        .autumn_color_g = 0.25f,
        .autumn_color_b = 0.10f,
    });
    add_tree_species(config, {
        .species_key = "sequoia",
        .title_key = "tree.sequoia",
        .temperature_min = 0.1f,
        .temperature_max = 0.6f,
        .humidity_min = 0.3f,
        .humidity_max = 1.0f,
        .density_weight = 0.4f,
        .min_trunk_height = 7,
        .max_trunk_height = 12,
        .canopy_shape = CanopyShape::CONE,
        .canopy_radius = 3,
        .wood_material_key = "snt:sequoia_wood",
        .leaves_material_key = "snt:sequoia_leaves",
        .sapling_material_key = "snt:sequoia_sapling",
        .is_evergreen = true,
        .ticks_to_young = 40000,
        .ticks_to_mature = 80000,
        .wood_color_r = 0.40f,
        .wood_color_g = 0.23f,
        .wood_color_b = 0.10f,
        .leaves_color_r = 0.12f,
        .leaves_color_g = 0.32f,
        .leaves_color_b = 0.10f,
        .autumn_color_r = 0.12f,
        .autumn_color_g = 0.32f,
        .autumn_color_b = 0.10f,
    });
    add_tree_species(config, {
        .species_key = "cherry",
        .title_key = "tree.cherry",
        .temperature_min = -0.1f,
        .temperature_max = 0.5f,
        .humidity_min = 0.2f,
        .humidity_max = 1.0f,
        .density_weight = 0.3f,
        .min_trunk_height = 2,
        .max_trunk_height = 4,
        .canopy_shape = CanopyShape::SPHERE,
        .canopy_radius = 2,
        .wood_material_key = "snt:cherry_wood",
        .leaves_material_key = "snt:cherry_leaves",
        .sapling_material_key = "snt:cherry_sapling",
        .ticks_to_young = 22000,
        .ticks_to_mature = 44000,
        .has_fruit = true,
        .fruit_item_key = "fruit.cherry",
        .fruit_season = 2,
        .wood_color_r = 0.50f,
        .wood_color_g = 0.28f,
        .wood_color_b = 0.22f,
        .leaves_color_r = 0.70f,
        .leaves_color_g = 0.35f,
        .leaves_color_b = 0.50f,
        .autumn_color_r = 0.80f,
        .autumn_color_g = 0.30f,
        .autumn_color_b = 0.25f,
    });
    add_tree_species(config, {
        .species_key = "olive",
        .title_key = "tree.olive",
        .temperature_min = 0.2f,
        .temperature_max = 0.8f,
        .humidity_min = -0.2f,
        .humidity_max = 0.6f,
        .density_weight = 0.3f,
        .min_trunk_height = 2,
        .max_trunk_height = 4,
        .canopy_shape = CanopyShape::SPHERE,
        .canopy_radius = 2,
        .wood_material_key = "snt:olive_wood",
        .leaves_material_key = "snt:olive_leaves",
        .sapling_material_key = "snt:olive_sapling",
        .is_evergreen = true,
        .ticks_to_young = 32000,
        .ticks_to_mature = 64000,
        .has_fruit = true,
        .fruit_item_key = "fruit.olive",
        .fruit_season = 1,
        .wood_color_r = 0.52f,
        .wood_color_g = 0.40f,
        .wood_color_b = 0.22f,
        .leaves_color_r = 0.25f,
        .leaves_color_g = 0.38f,
        .leaves_color_b = 0.15f,
        .autumn_color_r = 0.25f,
        .autumn_color_g = 0.38f,
        .autumn_color_b = 0.15f,
    });
}

void register_builtin_crop_species(WorldGenConfigSnapshot& config) {
    add_crop_species(config, {
        .species_key = "wheat",
        .title_key = "crop.wheat",
        .category = CropCategory::GRAIN,
        .temperature_min = -0.3f,
        .temperature_max = 0.6f,
        .humidity_min = -0.2f,
        .humidity_max = 0.6f,
        .plant_season = 0,
        .grow_season = 1,
        .harvest_season = 2,
        .ticks_seed_to_sprout = 3000,
        .ticks_sprout_to_growing = 6000,
        .ticks_growing_to_mature = 9000,
        .seed_item_key = "seed.wheat",
        .crop_item_key = "crop.wheat",
        .byproduct_item_key = "seed.wheat",
        .crop_min = 1,
        .crop_max = 2,
        .byproduct_count = 1,
        .stage_material_keys = {"snt:wheat_seed", "snt:wheat_sprout", "snt:wheat_growing", "snt:wheat_mature"},
        .fertility_sensitivity = 0.7f,
        .water_sensitivity = 0.7f,
        .wild_spawn = true,
        .wild_density_weight = 1.0f,
        .crop_color_r = 0.85f,
        .crop_color_g = 0.75f,
        .crop_color_b = 0.25f,
    });
    add_crop_species(config, {
        .species_key = "carrot",
        .title_key = "crop.carrot",
        .category = CropCategory::ROOT,
        .temperature_min = -0.2f,
        .temperature_max = 0.7f,
        .humidity_min = -0.1f,
        .humidity_max = 0.7f,
        .plant_season = 0,
        .grow_season = 1,
        .harvest_season = 2,
        .ticks_seed_to_sprout = 2500,
        .ticks_sprout_to_growing = 5000,
        .ticks_growing_to_mature = 8000,
        .seed_item_key = "seed.carrot",
        .crop_item_key = "crop.carrot",
        .crop_min = 1,
        .crop_max = 3,
        .byproduct_count = 0,
        .stage_material_keys = {"snt:carrot_seed", "snt:carrot_sprout", "snt:carrot_growing", "snt:carrot_mature"},
        .fertility_sensitivity = 0.5f,
        .water_sensitivity = 0.6f,
        .wild_spawn = true,
        .wild_density_weight = 0.8f,
        .crop_color_r = 0.75f,
        .crop_color_g = 0.45f,
        .crop_color_b = 0.15f,
    });
    add_crop_species(config, {
        .species_key = "potato",
        .title_key = "crop.potato",
        .category = CropCategory::ROOT,
        .temperature_min = -0.4f,
        .temperature_max = 0.5f,
        .humidity_min = -0.2f,
        .humidity_max = 0.8f,
        .plant_season = 0,
        .grow_season = 1,
        .harvest_season = 2,
        .ticks_seed_to_sprout = 3000,
        .ticks_sprout_to_growing = 5500,
        .ticks_growing_to_mature = 8500,
        .seed_item_key = "seed.potato",
        .crop_item_key = "crop.potato",
        .byproduct_item_key = "seed.potato",
        .crop_min = 1,
        .crop_max = 3,
        .byproduct_count = 1,
        .stage_material_keys = {"snt:potato_seed", "snt:potato_sprout", "snt:potato_growing", "snt:potato_mature"},
        .fertility_sensitivity = 0.6f,
        .water_sensitivity = 0.5f,
        .wild_density_weight = 0.5f,
        .crop_color_r = 0.55f,
        .crop_color_g = 0.45f,
        .crop_color_b = 0.20f,
    });
    add_crop_species(config, {
        .species_key = "cotton",
        .title_key = "crop.cotton",
        .category = CropCategory::FIBER,
        .temperature_min = 0.2f,
        .temperature_max = 0.8f,
        .humidity_min = -0.1f,
        .humidity_max = 0.5f,
        .plant_season = 1,
        .grow_season = 1,
        .harvest_season = 2,
        .ticks_seed_to_sprout = 3500,
        .ticks_sprout_to_growing = 7000,
        .ticks_growing_to_mature = 10000,
        .seed_item_key = "seed.cotton",
        .crop_item_key = "crop.cotton",
        .byproduct_item_key = "seed.cotton",
        .crop_min = 1,
        .crop_max = 2,
        .byproduct_count = 1,
        .stage_material_keys = {"snt:cotton_seed", "snt:cotton_sprout", "snt:cotton_growing", "snt:cotton_mature"},
        .fertility_sensitivity = 0.8f,
        .water_sensitivity = 0.7f,
        .wild_density_weight = 0.3f,
        .crop_color_r = 0.92f,
        .crop_color_g = 0.90f,
        .crop_color_b = 0.85f,
    });
    add_crop_species(config, {
        .species_key = "herb",
        .title_key = "crop.herb",
        .category = CropCategory::HERB,
        .temperature_min = -0.3f,
        .temperature_max = 0.5f,
        .humidity_min = 0.0f,
        .humidity_max = 0.8f,
        .ticks_seed_to_sprout = 2000,
        .ticks_sprout_to_growing = 4000,
        .ticks_growing_to_mature = 6000,
        .seed_item_key = "seed.herb",
        .crop_item_key = "crop.herb",
        .byproduct_item_key = "seed.herb",
        .crop_min = 1,
        .crop_max = 2,
        .byproduct_count = 1,
        .repeat_harvest = true,
        .regrow_ticks = 5000,
        .stage_material_keys = {"snt:herb_seed", "snt:herb_sprout", "snt:herb_growing", "snt:herb_mature"},
        .fertility_sensitivity = 0.4f,
        .water_sensitivity = 0.5f,
        .wild_spawn = true,
        .wild_density_weight = 1.2f,
        .crop_color_r = 0.40f,
        .crop_color_g = 0.55f,
        .crop_color_b = 0.25f,
    });
    add_crop_species(config, {
        .species_key = "pumpkin",
        .title_key = "crop.pumpkin",
        .category = CropCategory::FRUIT,
        .temperature_min = 0.3f,
        .temperature_max = 0.9f,
        .humidity_min = 0.0f,
        .humidity_max = 0.6f,
        .plant_season = 1,
        .grow_season = 1,
        .harvest_season = 2,
        .ticks_seed_to_sprout = 4000,
        .ticks_sprout_to_growing = 8000,
        .ticks_growing_to_mature = 12000,
        .seed_item_key = "seed.pumpkin",
        .crop_item_key = "crop.pumpkin",
        .byproduct_item_key = "seed.pumpkin",
        .crop_min = 1,
        .crop_max = 2,
        .byproduct_count = 1,
        .stage_material_keys = {"snt:pumpkin_seed", "snt:pumpkin_sprout", "snt:pumpkin_growing", "snt:pumpkin_mature"},
        .fertility_sensitivity = 0.7f,
        .water_sensitivity = 0.8f,
        .wild_density_weight = 0.4f,
        .crop_color_r = 0.90f,
        .crop_color_g = 0.55f,
        .crop_color_b = 0.15f,
    });
}

}  // namespace

void register_builtin_terrain_content(WorldGenConfigSnapshot& config) {
    register_builtin_terrain_material_catalog(config);
    config.role_keys = {
        .air = "snt:air",
        .stone = "snt:stone",
        .dirt = "snt:dirt",
        .sand = "snt:sand",
        .water = "snt:water",
        .lava = "snt:lava",
        .ore_iron = "snt:ore_iron",
        .ore_copper = "snt:ore_copper",
        .ore_coal = "snt:ore_coal",
        .wood = "snt:wood",
        .leaves = "snt:leaves",
        .deepstone = "snt:deepstone",
        .core_barrier = "snt:core_barrier",
        .snow = "snt:snow",
        .ice = "snt:ice",
    };
    config.runtime_material_keys = {
        .ladder = "snt:ladder",
        .workbench = "snt:workbench",
        .fence = "snt:fence",
        .farmland = "snt:farmland",
        .bloomery = "snt:runtime.machine.bloomery",
    };
    register_builtin_tree_species(config);
    register_builtin_crop_species(config);
    SNT_LOG_INFO("Registered built-in terrain content: materials=%zu trees=%zu crops=%zu",
                 config.materials.size(), config.tree_species.size(), config.crop_species.size());
}

namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Expected<TerrainMaterialId> resolve_material_id(
    const WorldGenConfigSnapshot& config, std::string_view key) {
    const auto found = config.material_ids_by_key.find(std::string(key));
    if (found == config.material_ids_by_key.end()) {
        return invalid_argument("Built-in terrain content refers to an unregistered material key: " +
                                std::string(key));
    }
    return found->second;
}

struct OreVeinSpec {
    std::string_view key;
    std::string_view host;
    std::string_view primary;
    std::string_view secondary;
    std::string_view between;
    std::string_view sporadic;
    float depth_min;
    float depth_max;
    float radius;
    float density;
    float weight;
};

constexpr std::array<OreVeinSpec, 31> kBuiltinOreVeinSpecs = {{
    {"snt:vein_coal", "snt:stone", "snt:ore_coal", "snt:ore_graphite", "snt:ore_sulfur", "snt:ore_diamond", 0.0f, 40.0f, 20.0f, 0.50f, 1.20f},
    {"snt:vein_salt_sulfur", "snt:stone", "snt:ore_salt", "snt:ore_sulfur", "snt:ore_calcium", "snt:ore_fluorite", 0.0f, 30.0f, 18.0f, 0.50f, 1.00f},
    {"snt:vein_cassiterite", "snt:stone", "snt:ore_cassiterite", "snt:ore_tin", "snt:ore_iron", "snt:ore_bismuth", 0.0f, 40.0f, 14.0f, 0.55f, 0.90f},
    {"snt:vein_light_metals", "snt:stone", "snt:ore_lithium", "snt:ore_magnesium", "snt:ore_boron", "snt:ore_beryllium", 0.0f, 40.0f, 14.0f, 0.45f, 0.70f},
    {"snt:vein_potash_phosphate", "snt:stone", "snt:ore_potassium", "snt:ore_phosphorus", "snt:ore_calcium", "snt:ore_iodine", 0.0f, 30.0f, 16.0f, 0.45f, 0.80f},
    {"snt:vein_bromine_iodine", "snt:stone", "snt:ore_bromine", "snt:ore_iodine", "snt:ore_salt", "snt:ore_cesium", 0.0f, 25.0f, 10.0f, 0.40f, 0.50f},
    {"snt:vein_bauxite", "snt:stone", "snt:ore_bauxite", "snt:ore_iron", "snt:ore_titanium", "snt:ore_gallium", 0.0f, 35.0f, 16.0f, 0.50f, 0.80f},
    {"snt:vein_iron", "snt:stone", "snt:ore_magnetite", "snt:ore_iron", "snt:ore_pyrite", "snt:ore_gold", 0.0f, 60.0f, 22.0f, 0.60f, 1.50f},
    {"snt:vein_copper", "snt:stone", "snt:ore_chalcopyrite", "snt:ore_copper", "snt:ore_iron", "snt:ore_gold", 0.0f, 50.0f, 18.0f, 0.55f, 1.20f},
    {"snt:vein_sphalerite_galena", "snt:stone", "snt:ore_sphalerite", "snt:ore_galena", "snt:ore_zinc", "snt:ore_silver", 5.0f, 50.0f, 16.0f, 0.55f, 1.00f},
    {"snt:vein_nickel", "snt:stone", "snt:ore_pentlandite", "snt:ore_nickel", "snt:ore_cobalt", "snt:ore_platinum", 10.0f, 60.0f, 16.0f, 0.50f, 0.80f},
    {"snt:vein_manganese", "snt:stone", "snt:ore_manganese", "snt:ore_iron", "snt:ore_bauxite", "snt:ore_chromium", 10.0f, 55.0f, 12.0f, 0.50f, 0.60f},
    {"snt:vein_cinnabar", "snt:stone", "snt:ore_cinnabar", "snt:ore_sulfur", "snt:ore_selenium", "snt:ore_tellurium", 10.0f, 50.0f, 10.0f, 0.40f, 0.40f},
    {"snt:vein_cadmium_indium", "snt:stone", "snt:ore_zinc", "snt:ore_cadmium", "snt:ore_indium", "snt:ore_gallium", 5.0f, 40.0f, 10.0f, 0.40f, 0.40f},
    {"snt:vein_bismuth_antimony", "snt:stone", "snt:ore_bismuth", "snt:ore_antimony", "snt:ore_galena", "snt:ore_tellurium", 15.0f, 55.0f, 12.0f, 0.45f, 0.50f},
    {"snt:vein_arsenic_pyrite", "snt:stone", "snt:ore_arsenic", "snt:ore_pyrite", "snt:ore_cobalt", "snt:ore_gold", 10.0f, 50.0f, 12.0f, 0.45f, 0.50f},
    {"snt:vein_thallium_alkali", "snt:stone", "snt:ore_thallium", "snt:ore_rubidium", "snt:ore_cesium", "snt:ore_potassium", 15.0f, 50.0f, 8.0f, 0.35f, 0.30f},
    {"snt:vein_scandium_strontium_barium", "snt:stone", "snt:ore_scandium", "snt:ore_strontium", "snt:ore_barium", "snt:ore_gallium", 15.0f, 55.0f, 10.0f, 0.40f, 0.40f},
    {"snt:vein_chromium_vanadium", "snt:stone", "snt:ore_chromium", "snt:ore_vanadium", "snt:ore_magnetite", "snt:ore_platinum", 15.0f, 60.0f, 12.0f, 0.45f, 0.50f},
    {"snt:vein_gallium_germanium", "snt:stone", "snt:ore_gallium", "snt:ore_germanium", "snt:ore_zinc", "snt:ore_indium", 10.0f, 45.0f, 8.0f, 0.35f, 0.30f},
    {"snt:vein_ilmenite_titanium", "snt:stone", "snt:ore_ilmenite", "snt:ore_titanium", "snt:ore_iron", "snt:ore_vanadium", 20.0f, 80.0f, 14.0f, 0.50f, 0.70f},
    {"snt:vein_cobalt", "snt:stone", "snt:ore_cobalt", "snt:ore_nickel", "snt:ore_arsenic", "snt:ore_antimony", 20.0f, 70.0f, 12.0f, 0.50f, 0.60f},
    {"snt:vein_hafnium_zircon", "snt:stone", "snt:ore_zirconium", "snt:ore_hafnium", "snt:ore_titanium", "snt:ore_germanium", 20.0f, 70.0f, 10.0f, 0.40f, 0.40f},
    {"snt:vein_molybdenum_rhenium", "snt:stone", "snt:ore_molybdenum", "snt:ore_rhenium", "snt:ore_osmium", "snt:ore_iridium", 30.0f, 90.0f, 10.0f, 0.40f, 0.30f},
    {"snt:vein_niobium_tantalum", "snt:stone", "snt:ore_niobium", "snt:ore_tantalum", "snt:ore_yttrium", "snt:ore_tungsten", 25.0f, 80.0f, 10.0f, 0.40f, 0.30f},
    {"snt:vein_tungsten", "snt:stone", "snt:ore_tungsten", "snt:ore_titanium", "snt:ore_molybdenum", "snt:ore_rhenium", 40.0f, 100.0f, 12.0f, 0.45f, 0.50f},
    {"snt:vein_uranium_thorium", "snt:stone", "snt:ore_uranium", "snt:ore_thorium", "snt:ore_lead", "snt:ore_rare_earth", 40.0f, 100.0f, 10.0f, 0.35f, 0.30f},
    {"snt:vein_rare_earth", "snt:stone", "snt:ore_rare_earth", "snt:ore_yttrium", "snt:ore_thorium", "snt:ore_zirconium", 35.0f, 90.0f, 10.0f, 0.40f, 0.25f},
    {"snt:vein_gemstone", "snt:stone", "snt:ore_diamond", "snt:ore_ruby", "snt:ore_sapphire", "snt:ore_emerald", 35.0f, 100.0f, 10.0f, 0.30f, 0.25f},
    {"snt:vein_pgm", "snt:stone", "snt:ore_platinum", "snt:ore_palladium", "snt:ore_rhodium", "snt:ore_iridium", 45.0f, 100.0f, 10.0f, 0.35f, 0.20f},
    {"snt:vein_ruthenium_osmium", "snt:stone", "snt:ore_ruthenium", "snt:ore_osmium", "snt:ore_iridium", "snt:ore_platinum", 50.0f, 100.0f, 8.0f, 0.30f, 0.15f},
}};

[[nodiscard]] snt::core::Expected<void> append_ore_vein_groups(
    WorldGenConfigSnapshot& config, std::string_view dimension_id) {
    for (const OreVeinSpec& spec : kBuiltinOreVeinSpecs) {
        const auto host = resolve_material_id(config, spec.host);
        if (!host) return host.error();
        const auto primary = resolve_material_id(config, spec.primary);
        if (!primary) return primary.error();
        const auto secondary = resolve_material_id(config, spec.secondary);
        if (!secondary) return secondary.error();
        const auto between = resolve_material_id(config, spec.between);
        if (!between) return between.error();
        const auto sporadic = resolve_material_id(config, spec.sporadic);
        if (!sporadic) return sporadic.error();
        config.ore_vein_groups.push_back({
            .key = std::string(spec.key),
            .dimension_id = std::string(dimension_id),
            .host_material = *host,
            .primary_ore = *primary,
            .secondary_ore = *secondary,
            .between_ore = *between,
            .sporadic_ore = *sporadic,
            .depth_min = spec.depth_min,
            .depth_max = spec.depth_max,
            .radius = spec.radius,
            .density = spec.density,
            .weight = spec.weight,
        });
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> append_rock_layer(
    WorldGenConfigSnapshot& config, std::string_view key, std::string_view dimension_id,
    std::string_view rock_material_key, float noise_scale, int noise_octaves,
    float noise_min, float noise_max, float depth_min, float depth_max,
    float hardness_multiplier, float collapse_chance,
    std::initializer_list<std::string_view> associated_ore_keys) {
    const auto rock_material = resolve_material_id(config, rock_material_key);
    if (!rock_material) return rock_material.error();

    RockLayerRule rule = {
        .key = std::string(key),
        .dimension_id = std::string(dimension_id),
        .rock_material = *rock_material,
        .noise_scale = noise_scale,
        .noise_octaves = noise_octaves,
        .noise_min = noise_min,
        .noise_max = noise_max,
        .depth_min = depth_min,
        .depth_max = depth_max,
        .hardness_multiplier = hardness_multiplier,
        .collapse_chance = std::clamp(collapse_chance, 0.0f, 1.0f),
    };
    rule.associated_ores.reserve(associated_ore_keys.size());
    for (const std::string_view ore_key : associated_ore_keys) {
        const auto ore = resolve_material_id(config, ore_key);
        if (!ore) return ore.error();
        rule.associated_ores.push_back(*ore);
    }
    config.rock_layer_rules.push_back(std::move(rule));
    return {};
}

}  // namespace

snt::core::Expected<void> append_builtin_terrain_planet_content(
    WorldGenConfigSnapshot& config, const BuiltinTerrainPlanetInput& input) {
    const PlanetConfig& planet = input.planet;
    if (config.material_ids_by_key.empty()) {
        return invalid_argument(
            "Built-in planet terrain rules require finalize_world_gen_config() first");
    }
    if (planet.dimension_id.empty() || planet.planet_radius <= 0.0f ||
        !std::isfinite(planet.planet_radius) || !std::isfinite(input.gravity_multiplier) ||
        input.gravity_multiplier <= 0.0f) {
        return invalid_argument("Built-in planet terrain input has invalid dimension, radius, or gravity");
    }
    if (planet.atmosphere_type < ATMO_NONE || planet.atmosphere_type > ATMO_CORROSIVE) {
        return invalid_argument("Built-in planet terrain input has an unsupported atmosphere type");
    }
    if (config.find_base_rule(planet.dimension_id) != nullptr ||
        config.find_planet_config(planet.dimension_id) != nullptr) {
        return invalid_argument("Built-in planet terrain content already exists for dimension '" +
                                planet.dimension_id + "'");
    }

    const auto air = resolve_material_id(config, "snt:air");
    if (!air) return air.error();
    const auto dirt = resolve_material_id(config, "snt:dirt");
    if (!dirt) return dirt.error();
    const auto stone = resolve_material_id(config, "snt:stone");
    if (!stone) return stone.error();
    const auto sand = resolve_material_id(config, "snt:sand");
    if (!sand) return sand.error();
    const auto water = resolve_material_id(config, "snt:water");
    if (!water) return water.error();

    const size_t old_base_rule_count = config.base_terrain_rules.size();
    const size_t old_biome_rule_count = config.biome_rules.size();
    const size_t old_rock_layer_count = config.rock_layer_rules.size();
    const size_t old_ore_vein_count = config.ore_vein_groups.size();
    const auto rollback = [&config, old_base_rule_count, old_biome_rule_count,
                          old_rock_layer_count, old_ore_vein_count]() {
        config.base_terrain_rules.resize(old_base_rule_count);
        config.biome_rules.resize(old_biome_rule_count);
        config.rock_layer_rules.resize(old_rock_layer_count);
        config.ore_vein_groups.resize(old_ore_vein_count);
    };

    const std::string& dimension_id = planet.dimension_id;
    const float gravity = input.gravity_multiplier;
    const bool has_water = planet.sea_level_fraction > 0.01f;
    const bool thin_or_no_atmosphere =
        planet.atmosphere_type == ATMO_NONE || planet.atmosphere_type == ATMO_THIN;
    const bool toxic_or_corrosive =
        planet.atmosphere_type == ATMO_TOXIC || planet.atmosphere_type == ATMO_CORROSIVE;

    config.base_terrain_rules.push_back({
        .dimension_id = dimension_id,
        .mode = "surface_elevation",
        .default_material = *dirt,
        .low_elevation_material = has_water ? *water : *stone,
        .high_elevation_material = *stone,
        .cave_air_material = *air,
        .elevation_scale = 0.02f,
        .elevation_octaves = 4,
        .detail_scale = 0.05f,
        .detail_octaves = 3,
        .water_elevation_max = has_water ? -0.25f : -999.0f,
        .water_detail_max = has_water ? 0.30f : -999.0f,
        .stone_elevation_abs_min = gravity < 1.5f ? 0.55f : 0.45f,
    });
    config.biome_rules.push_back({
        .key = "snt:desert_sand",
        .dimension_id = dimension_id,
        .source_material = *dirt,
        .result_material = *sand,
        .temperature_min = thin_or_no_atmosphere ? 0.10f : 0.30f,
        .humidity_max = -0.20f,
    });
    if (has_water) {
        config.biome_rules.push_back({
            .key = "snt:beach_sand",
            .dimension_id = dimension_id,
            .source_material = *dirt,
            .result_material = *sand,
            .requires_near_material = true,
            .near_material = *water,
            .near_radius = 2,
        });
    }
    config.biome_rules.push_back({
        .key = "snt:rocky_highlands",
        .dimension_id = dimension_id,
        .source_material = *dirt,
        .result_material = *stone,
        .temperature_max = toxic_or_corrosive ? -0.10f : -0.40f,
        .humidity_max = -0.10f,
    });
    if (toxic_or_corrosive) {
        config.biome_rules.push_back({
            .key = "snt:barren_wasteland",
            .dimension_id = dimension_id,
            .source_material = *dirt,
            .result_material = *stone,
            .temperature_min = -0.10f,
            .temperature_max = 0.30f,
            .humidity_max = 0.0f,
        });
    }

    std::string_view primary_rock = "snt:granite_rock";
    std::string_view secondary_rock = "snt:basalt_rock";
    if (planet.atmosphere_type == ATMO_NONE) {
        primary_rock = "snt:anorthosite_rock";
        secondary_rock = "snt:regolith_rock";
    } else if (planet.atmosphere_type == ATMO_THIN) {
        primary_rock = "snt:regolith_rock";
        secondary_rock = "snt:granite_rock";
    } else if (gravity >= 1.8f) {
        primary_rock = "snt:komatiite_rock";
        secondary_rock = "snt:basalt_rock";
    } else if (has_water && gravity < 1.2f) {
        primary_rock = "snt:granite_rock";
        secondary_rock = "snt:shale_rock";
    } else if (!has_water && planet.atmosphere_type == ATMO_BREATHABLE) {
        primary_rock = "snt:sandstone_rock";
        secondary_rock = "snt:marble_rock";
    } else if (toxic_or_corrosive) {
        primary_rock = "snt:basalt_rock";
        secondary_rock = "snt:marble_rock";
    }

    const float crust_depth = std::min(planet.planet_radius * 0.20f, 100.0f);
    const float deep_start = crust_depth * 0.60f;
    auto result = append_rock_layer(config, "snt:primary_rock", dimension_id, primary_rock,
                                    0.005f, 3, -1.0f, 0.0f, 0.0f, crust_depth, 1.0f,
                                    0.30f * gravity,
                                    planet.planet_radius >= 200.0f
                                        ? std::initializer_list<std::string_view>{
                                              "snt:ore_iron", "snt:ore_copper",
                                              "snt:ore_chalcopyrite", "snt:ore_cassiterite",
                                              "snt:ore_sphalerite", "snt:ore_galena",
                                              "snt:ore_magnetite", "snt:ore_bauxite",
                                              "snt:ore_salt", "snt:ore_sulfur",
                                              "snt:ore_tin", "snt:ore_zinc",
                                              "snt:ore_pyrite", "snt:ore_fluorite",
                                              "snt:ore_lithium", "snt:ore_magnesium",
                                              "snt:ore_phosphorus", "snt:ore_potassium",
                                              "snt:ore_calcium", "snt:ore_boron",
                                              "snt:ore_bromine", "snt:ore_strontium",
                                              "snt:ore_zirconium", "snt:ore_cadmium",
                                              "snt:ore_iodine", "snt:ore_barium",
                                              "snt:ore_thallium", "snt:ore_bismuth"}
                                        : std::initializer_list<std::string_view>{
                                              "snt:ore_iron", "snt:ore_chalcopyrite",
                                              "snt:ore_cassiterite", "snt:ore_salt",
                                              "snt:ore_tin", "snt:ore_pyrite",
                                              "snt:ore_lithium", "snt:ore_magnesium",
                                              "snt:ore_calcium", "snt:ore_boron"});
    if (!result) {
        rollback();
        return result.error();
    }
    if (planet.planet_radius >= 250.0f) {
        result = append_rock_layer(config, "snt:secondary_rock", dimension_id, secondary_rock,
                                   0.005f, 3, 0.0f, 1.0f, 0.0f, crust_depth, 1.2f,
                                   0.25f * gravity,
                                   {"snt:ore_iron", "snt:ore_magnetite",
                                    "snt:ore_pentlandite", "snt:ore_ilmenite",
                                    "snt:ore_nickel", "snt:ore_cinnabar",
                                    "snt:ore_manganese", "snt:ore_beryllium",
                                    "snt:ore_scandium", "snt:ore_vanadium",
                                    "snt:ore_chromium", "snt:ore_gallium",
                                    "snt:ore_germanium", "snt:ore_arsenic",
                                    "snt:ore_selenium", "snt:ore_rubidium",
                                    "snt:ore_yttrium", "snt:ore_niobium",
                                    "snt:ore_molybdenum", "snt:ore_indium",
                                    "snt:ore_antimony", "snt:ore_tellurium",
                                    "snt:ore_cesium", "snt:ore_hafnium",
                                    "snt:ore_rhenium"});
        if (!result) {
            rollback();
            return result.error();
        }
    }
    result = append_rock_layer(config, "snt:deeprock", dimension_id, "snt:deepstone",
                               0.003f, 2, -1.0f, 1.0f, deep_start, 10000.0f, 1.5f,
                               0.50f * gravity,
                               {"snt:ore_tungsten", "snt:ore_titanium",
                                "snt:ore_uranium", "snt:ore_platinum",
                                "snt:ore_cobalt", "snt:ore_diamond",
                                "snt:ore_ruby", "snt:ore_sapphire",
                                "snt:ore_emerald", "snt:ore_graphite",
                                "snt:ore_ruthenium", "snt:ore_rhodium",
                                "snt:ore_palladium", "snt:ore_osmium",
                                "snt:ore_iridium", "snt:ore_tantalum",
                                "snt:ore_rare_earth", "snt:ore_thorium"});
    if (!result) {
        rollback();
        return result.error();
    }
    result = append_ore_vein_groups(config, dimension_id);
    if (!result) {
        rollback();
        return result.error();
    }

    config.planet_configs.push_back(planet);
    config.content_hash = hash_world_gen_config(config);
    SNT_LOG_INFO("Registered built-in terrain rules for planet '%s': biomes=%zu rocks=%zu veins=%zu",
                 dimension_id.c_str(), config.biome_rules.size() - old_biome_rule_count,
                 config.rock_layer_rules.size() - old_rock_layer_count,
                 config.ore_vein_groups.size() - old_ore_vein_count);
    return {};
}

}  // namespace snt::game
