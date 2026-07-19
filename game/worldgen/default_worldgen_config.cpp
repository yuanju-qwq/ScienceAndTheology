// Current game-owned world-generation default implementation.

#include "game/worldgen/default_worldgen_config.h"

namespace snt::game {

std::shared_ptr<const WorldGenConfigSnapshot> make_default_game_worldgen_config() {
    auto config = std::make_shared<WorldGenConfigSnapshot>();
    const auto add_material = [&config](TerrainMaterialId id, const char* key, uint32_t flags) {
        TerrainMaterialDef material;
        material.id = id;
        material.key = key;
        material.flags = flags;
        config->materials.push_back(material);
        config->material_ids_by_key[material.key] = material.id;
        config->material_keys_by_id[material.id] = material.key;
    };

    add_material(0, "air", 0);
    add_material(1, "stone", TF_SOLID | TF_MINEABLE | TF_WALKABLE);
    add_material(2, "dirt", TF_SOLID | TF_MINEABLE | TF_WALKABLE);
    add_material(3, "sand", TF_SOLID | TF_MINEABLE | TF_WALKABLE | TF_GRAVITY_FALL);
    add_material(4, "snow", TF_SOLID | TF_MINEABLE | TF_WALKABLE);

    config->roles.air = 0;
    config->roles.stone = 1;
    config->roles.dirt = 2;

    BaseTerrainRule rule;
    rule.dimension_id = "overworld";
    rule.default_material = config->roles.stone;
    rule.high_elevation_material = config->roles.stone;
    rule.cave_threshold = 10.0f;
    config->base_terrain_rules.push_back(rule);
    config->content_hash = hash_world_gen_config(*config);
    return config;
}

}  // namespace snt::game
