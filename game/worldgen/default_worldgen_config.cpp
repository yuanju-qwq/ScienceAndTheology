// Current game-owned world-generation default implementation.

#define SNT_LOG_CHANNEL "game.worldgen_config"
#include "game/worldgen/default_worldgen_config.h"

#include "core/log.h"
#include "game/worldgen/legacy_terrain_material_catalog.h"

#include <utility>

namespace snt::game {
namespace {

void add_runtime_material(WorldGenConfigSnapshot& config, TerrainMaterialDef material) {
    config.materials.push_back(std::move(material));
}

}  // namespace

std::shared_ptr<const WorldGenConfigSnapshot> make_default_game_worldgen_config() {
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    register_migrated_legacy_terrain_material_catalog(*config);

    const uint32_t solid_mineable_walkable = TF_SOLID | TF_MINEABLE | TF_WALKABLE;
    add_runtime_material(*config, {
        .key = "snt:runtime.bed",
        .title_key = "terrain.runtime.bed",
        .flags = solid_mineable_walkable,
        .hardness = 1.0f,
        .drops = {{.item_key = "bed"}},
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.fire",
        .title_key = "terrain.runtime.fire",
        .flags = TF_MINEABLE,
        .hardness = 0.1f,
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.machine.furnace",
        .title_key = "terrain.runtime.machine.furnace",
        .flags = solid_mineable_walkable,
        .hardness = 3.0f,
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.machine.pit_kiln",
        .title_key = "terrain.runtime.machine.pit_kiln",
        .flags = solid_mineable_walkable,
        .hardness = 0.8f,
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.machine.charcoal_pit",
        .title_key = "terrain.runtime.machine.charcoal_pit",
        .flags = solid_mineable_walkable,
        .hardness = 0.6f,
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.machine.bloomery",
        .title_key = "terrain.runtime.machine.bloomery",
        .flags = solid_mineable_walkable,
        .hardness = 3.0f,
    });
    add_runtime_material(*config, {
        .key = "snt:runtime.machine.anvil",
        .title_key = "terrain.runtime.machine.anvil",
        .flags = solid_mineable_walkable,
        .hardness = 4.0f,
    });

    config->role_keys = {
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
    config->runtime_material_keys = {
        .ladder = "snt:ladder",
        .workbench = "snt:workbench",
        .fence = "snt:fence",
        .farmland = "snt:farmland",
        .bloomery = "snt:runtime.machine.bloomery",
    };
    if (auto result = finalize_world_gen_config(*config); !result) {
        SNT_LOG_ERROR("Failed to finalize default terrain catalog: %s",
                      result.error().format().c_str());
        return {};
    }

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.mode = "surface_elevation";
    rule.default_material = config->roles.dirt;
    rule.low_elevation_material = config->roles.water;
    rule.high_elevation_material = config->roles.stone;
    rule.cave_air_material = config->roles.air;
    config->base_terrain_rules.push_back(rule);
    config->content_hash = hash_world_gen_config(*config);
    SNT_LOG_INFO("Published default terrain catalog: %zu semantic keys, hash=%llu",
                 config->materials.size(),
                 static_cast<unsigned long long>(config->content_hash));
    return config;
}

}  // namespace snt::game
