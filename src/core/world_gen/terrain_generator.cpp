#include "terrain_generator.hpp"

#include <cmath>
#include <random>
#include <vector>

namespace science_and_theology {

TerrainGenerator::TerrainGenerator(WorldSeed seed)
    : world_seed_(seed) {}

ChunkData TerrainGenerator::generate_chunk(
    const std::string& layer_id, int chunk_x, int chunk_y) {
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.state = ChunkState::GENERATED;
    chunk.terrain.resize(ChunkData::kChunkSize, ChunkData::kChunkSize);

    pass_base_terrain(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_biome(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_ore(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_structure(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_object(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_gameplay(layer_id, chunk_x, chunk_y, chunk.terrain);

    return chunk;
}

void TerrainGenerator::pass_base_terrain(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    // Each layer gets its own noise generator via sub-seed.
    NoiseGenerator elevation_noise(
        world_seed_.chunk_seed(
            static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
            chunk_x, chunk_y));
    NoiseGenerator detail_noise(
        world_seed_.chunk_seed(
            static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
            chunk_x, chunk_y));

    for (int y = 0; y < ChunkData::kChunkSize; ++y) {
        for (int x = 0; x < ChunkData::kChunkSize; ++x) {
            // Compute global cell position.
            int global_x = chunk_x * ChunkData::kChunkSize + x;
            int global_y = chunk_y * ChunkData::kChunkSize + y;

            if (layer_id == "surface") {
                // Large-scale elevation: dirt plains with water bodies.
                float elevation = elevation_noise.noise_2d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    0.02f, 4);

                float feature = detail_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 10000),
                    static_cast<float>(global_y + 10000),
                    0.05f, 3);

                // Water where elevation is low and feature supports it.
                bool is_water = (elevation < -0.25f) && (feature < 0.3f);

                // Stone cliffs where elevation is very low or very high.
                bool is_stone = (elevation < -0.55f) || (elevation > 0.55f);

                if (is_water) {
                    set_cell(terrain, x, y, TerrainMaterial::WATER);
                } else if (is_stone) {
                    set_cell(terrain, x, y, TerrainMaterial::STONE);
                } else {
                    set_cell(terrain, x, y, TerrainMaterial::DIRT);
                }
            } else if (layer_id == "underground") {
                // Underground: solid stone with cave tunnels (air pockets).
                float cave_noise = elevation_noise.noise_2d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    0.04f, 4);

                // Caves: regions where noise is above threshold.
                // Higher threshold = fewer caves.
                float cave_threshold = 0.35f;

                // Make caves more open near chunk center, tighter at edges.
                float center_dist_x = std::abs(x - ChunkData::kChunkSize / 2.0f);
                float center_dist_y = std::abs(y - ChunkData::kChunkSize / 2.0f);
                float edge_factor = std::max(center_dist_x, center_dist_y)
                    / (ChunkData::kChunkSize / 2.0f);
                cave_threshold += edge_factor * 0.25f;

                if (cave_noise > cave_threshold) {
                    set_cell(terrain, x, y, TerrainMaterial::AIR);
                } else {
                    set_cell(terrain, x, y, TerrainMaterial::STONE);
                }
            } else {
                // Unknown layer: fill with stone.
                set_cell(terrain, x, y, TerrainMaterial::STONE);
            }
        }
    }
}

void TerrainGenerator::pass_biome(
    const std::string& /*layer_id*/,
    int /*chunk_x*/, int /*chunk_y*/,
    TerrainData& /*terrain*/) {
    // Stub: biome adjustments not yet implemented.
    // Future: adjust dirt→sand near water, stone→dirt in forest zones, etc.
}

void TerrainGenerator::pass_ore(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    NoiseGenerator ore_noise(
        world_seed_.chunk_seed(
            static_cast<uint32_t>(GenerationPass::ORE),
            chunk_x, chunk_y));
    NoiseGenerator vein_noise(
        world_seed_.chunk_seed(
            static_cast<uint32_t>(GenerationPass::ORE) + 1,
            chunk_x, chunk_y));

    for (int y = 0; y < ChunkData::kChunkSize; ++y) {
        for (int x = 0; x < ChunkData::kChunkSize; ++x) {
            TerrainCell& cell = terrain.cell_at(x, y);
            if (!is_stone_cell(cell)) {
                continue;
            }

            int global_x = chunk_x * ChunkData::kChunkSize + x;
            int global_y = chunk_y * ChunkData::kChunkSize + y;

            // Ore density: higher where noise is high.
            float ore_density = ore_noise.noise_2d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                0.08f, 3);

            // Vein shape: small clusters using higher-frequency noise.
            float vein_shape = vein_noise.noise_2d_scaled(
                static_cast<float>(global_x + 5000),
                static_cast<float>(global_y + 5000),
                0.15f, 2);

            // Each ore type has a density window.
            float combined = ore_density * 0.5f + vein_shape * 0.5f;

            if (combined > 0.5f) {
                set_cell(terrain, x, y, TerrainMaterial::ORE_IRON);
            } else if (combined > 0.25f && combined <= 0.5f) {
                set_cell(terrain, x, y, TerrainMaterial::ORE_COPPER);
            } else if (combined > 0.05f && combined <= 0.25f) {
                set_cell(terrain, x, y, TerrainMaterial::ORE_COAL);
            }
        }
    }
}

void TerrainGenerator::pass_structure(
    const std::string& /*layer_id*/,
    int /*chunk_x*/, int /*chunk_y*/,
    TerrainData& /*terrain*/) {
    // Stub: large structures not yet implemented.
    // Future: ruins, large cave rooms, boss arenas, etc.
}

void TerrainGenerator::pass_object(
    const std::string& /*layer_id*/,
    int /*chunk_x*/, int /*chunk_y*/,
    TerrainData& /*terrain*/) {
    // Stub: small objects not yet implemented.
    // Future: altars, chests, bridges, gates, pumps, etc.
}

void TerrainGenerator::pass_gameplay(
    const std::string& /*layer_id*/,
    int /*chunk_x*/, int /*chunk_y*/,
    TerrainData& /*terrain*/) {
    // Stub: gameplay elements not yet implemented.
    // Future: connector placement, spawn points, mechanism triggers.
}

void TerrainGenerator::set_cell(
    TerrainData& terrain, int x, int y, TerrainMaterial material) {
    TerrainCell& cell = terrain.cell_at(x, y);
    cell.material = material;
    cell.flags = kTerrainMaterialFlags[
        static_cast<size_t>(material)];
}

bool TerrainGenerator::is_stone_cell(const TerrainCell& cell) const {
    return cell.material == TerrainMaterial::STONE;
}

float TerrainGenerator::cell_noise(
    const NoiseGenerator& noise, int local_x, int local_y,
    int chunk_x, int chunk_y, float scale) const {
    float global_x = static_cast<float>(
        chunk_x * ChunkData::kChunkSize + local_x);
    float global_y = static_cast<float>(
        chunk_y * ChunkData::kChunkSize + local_y);
    return noise.noise_2d_scaled(global_x, global_y, scale, 4);
}

} // namespace science_and_theology