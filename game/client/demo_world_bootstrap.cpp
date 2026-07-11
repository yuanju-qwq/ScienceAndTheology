// ScienceAndTheology development terrain bootstrap.

#define SNT_LOG_CHANNEL "game_session"
#include "demo_world_bootstrap.h"

#include "core/log.h"
#include "data/defs/chunk_data.h"
#include "data/defs/world_seed.h"
#include "data/world/chunk_registry.h"
#include "data/world_gen/terrain_generator.h"
#include "data/world_gen/world_gen_config.h"
#include "voxel/chunk_render_system.h"

#include <memory>

namespace snt::game {
namespace {

std::shared_ptr<const snt::data::WorldGenConfigSnapshot> make_demo_world_gen_config() {
    using namespace snt::data;

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

void paint_demo_surface_variants(snt::data::ChunkData& chunk,
                                 const snt::data::WorldGenConfigSnapshot& config) {
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            for (int y = chunk.terrain.size_y - 1; y >= 0; --y) {
                auto& cell = chunk.terrain.cell_at(x, y, z);
                if (static_cast<snt::data::TerrainMaterialId>(cell.material) == config.roles.air) continue;

                const int world_x = chunk.chunk_x * snt::data::ChunkData::kChunkSize + x;
                const int world_z = chunk.chunk_z * snt::data::ChunkData::kChunkSize + z;
                const int band = ((world_x / 8) + (world_z / 8)) & 3;
                if (band == 1) {
                    cell.material = config.material_ids_by_key.at("dirt");
                } else if (band == 2) {
                    cell.material = config.material_ids_by_key.at("sand");
                } else if (band == 3) {
                    cell.material = config.material_ids_by_key.at("snow");
                }
                break;
            }
        }
    }
}

size_t count_non_air_cells(const snt::data::ChunkData& chunk,
                           snt::data::TerrainMaterialId air_material) {
    size_t non_air = 0;
    for (const auto& cell : chunk.terrain.cells) {
        if (static_cast<snt::data::TerrainMaterialId>(cell.material) != air_material) {
            ++non_air;
        }
    }
    return non_air;
}

}  // namespace

snt::core::Expected<void> bootstrap_demo_world(const GameDemoConfig& config,
                                                snt::data::ChunkRegistry& chunk_registry,
                                                snt::voxel::ChunkRenderSystem& chunk_render_system) {
    if (!config.bootstrap_chunks) {
        SNT_LOG_INFO("Demo chunk bootstrap disabled");
        return {};
    }

    auto world_config = make_demo_world_gen_config();
    snt::data::TerrainGenerator terrain_generator(snt::data::WorldSeed(config.seed), world_config);
    constexpr int32_t kDemoChunks[][3] = {{0, 0, 0}, {0, -1, 0}};

    for (const auto [chunk_x, chunk_y, chunk_z] : kDemoChunks) {
        snt::data::ChunkData chunk = terrain_generator.generate_chunk(
            "overworld", chunk_x, chunk_y, chunk_z);
        paint_demo_surface_variants(chunk, *world_config);
        const size_t non_air = count_non_air_cells(chunk, world_config->roles.air);
        const size_t total = chunk.terrain.cells.size();
        chunk_registry.set_chunk("overworld", chunk_x, chunk_y, chunk_z, std::move(chunk));
        chunk_render_system.mark_dirty(snt::data::ChunkKey("overworld", chunk_x, chunk_y, chunk_z));
        SNT_LOG_INFO("Demo chunk generated at (%d,%d,%d), non_air=%zu/%zu",
                     chunk_x, chunk_y, chunk_z, non_air, total);
    }

    return {};
}

}  // namespace snt::game
