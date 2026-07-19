// ScienceAndTheology development terrain bootstrap.

#define SNT_LOG_CHANNEL "game_session"
#include "demo_world_bootstrap.h"

#include "core/error.h"
#include "core/log.h"
#include "game/world/game_chunk.h"
#include "game/worldgen/terrain_generator.h"
#include "game/worldgen/world_gen_config.h"
#include "game/worldgen/world_seed.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

namespace snt::game {
namespace {

void paint_demo_surface_variants(snt::voxel::VoxelChunk& chunk,
                                 const WorldGenConfigSnapshot& config) {
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            for (int y = chunk.terrain.size_y - 1; y >= 0; --y) {
                auto& cell = chunk.terrain.cell_at(x, y, z);
                if (static_cast<TerrainMaterialId>(cell.material) == config.roles.air) continue;

                const int world_x = chunk.chunk_x * snt::voxel::VoxelChunk::kChunkSize + x;
                const int world_z = chunk.chunk_z * snt::voxel::VoxelChunk::kChunkSize + z;
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

size_t count_non_air_cells(const snt::voxel::VoxelChunk& chunk,
                           TerrainMaterialId air_material) {
    size_t non_air = 0;
    for (const auto& cell : chunk.terrain.cells) {
        if (static_cast<TerrainMaterialId>(cell.material) != air_material) {
            ++non_air;
        }
    }
    return non_air;
}

}  // namespace

snt::core::Expected<void> bootstrap_demo_world(const GameDemoConfig& config,
                                                snt::voxel::ChunkRegistry& chunk_registry,
                                                GameChunkSidecarRegistry& sidecars,
                                                std::shared_ptr<const WorldGenConfigSnapshot>
                                                    worldgen_config) {
    if (!config.bootstrap_chunks) {
        SNT_LOG_INFO("Demo chunk bootstrap disabled");
        return {};
    }
    if (!worldgen_config) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidArgument,
                                "Demo world bootstrap requires a world-generation snapshot"};
    }

    TerrainGenerator terrain_generator(WorldSeed(config.seed), worldgen_config);
    constexpr int32_t kDemoChunks[][3] = {{0, 0, 0}, {0, -1, 0}};

    for (const auto [chunk_x, chunk_y, chunk_z] : kDemoChunks) {
        GameChunk chunk = terrain_generator.generate_chunk(
            "overworld", chunk_x, chunk_y, chunk_z);
        paint_demo_surface_variants(chunk, *worldgen_config);
        const size_t non_air = count_non_air_cells(chunk, worldgen_config->roles.air);
        const size_t total = chunk.terrain.cells.size();
        const snt::voxel::ChunkKey key("overworld", chunk_x, chunk_y, chunk_z);
        snt::voxel::VoxelChunk voxel_chunk = std::move(chunk.voxel_chunk());
        sidecars.set(key, std::move(chunk.sidecar()));
        chunk_registry.set_chunk("overworld", chunk_x, chunk_y, chunk_z,
                                 std::move(voxel_chunk));
        SNT_LOG_INFO("Demo chunk generated at (%d,%d,%d), non_air=%zu/%zu",
                     chunk_x, chunk_y, chunk_z, non_air, total);
    }

    return {};
}

}  // namespace snt::game
