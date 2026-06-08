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
    pass_tree(layer_id, chunk_x, chunk_y, chunk.terrain);
    pass_gameplay(layer_id, chunk_x, chunk_y, chunk);

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
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    // Temperature and humidity define biome regions.
    NoiseGenerator temp_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BIOME), chunk_x, chunk_y));
    NoiseGenerator humidity_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BIOME) + 1, chunk_x, chunk_y));

    for (int y = 0; y < ChunkData::kChunkSize; ++y) {
        for (int x = 0; x < ChunkData::kChunkSize; ++x) {
            TerrainCell& cell = terrain.cell_at(x, y);
            int global_x = chunk_x * ChunkData::kChunkSize + x;
            int global_y = chunk_y * ChunkData::kChunkSize + y;

            float temperature = temp_noise.noise_2d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                0.015f, 3);
            float humidity = humidity_noise.noise_2d_scaled(
                static_cast<float>(global_x + 2000),
                static_cast<float>(global_y + 2000),
                0.02f, 3);

            if (layer_id == "surface") {
                // Only adjust walkable (DIRT/SAND) or STONE cells.
                if (cell.material == TerrainMaterial::DIRT) {
                    // Desert: hot and dry → replace with SAND.
                    if (temperature > 0.3f && humidity < -0.2f) {
                        set_cell(terrain, x, y, TerrainMaterial::SAND);
                        continue;
                    }
                    // Beach: moderate temperature near water (check neighbors).
                    bool near_water = false;
                    for (int dy = -2; dy <= 2 && !near_water; ++dy) {
                        for (int dx = -2; dx <= 2 && !near_water; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny) &&
                                terrain.cell_at(nx, ny).material == TerrainMaterial::WATER) {
                                near_water = true;
                            }
                        }
                    }
                    if (near_water) {
                        set_cell(terrain, x, y, TerrainMaterial::SAND);
                        continue;
                    }
                    // Rocky highlands: cold + low humidity → STONE outcrops.
                    if (temperature < -0.4f && humidity < -0.1f) {
                        set_cell(terrain, x, y, TerrainMaterial::STONE);
                        continue;
                    }
                    // Fertile: warm + humid → leave as DIRT (default).
                }
            } else if (layer_id == "underground") {
                // Biome adjustments for underground:
                // Dirt floors in caves where temperature is moderate.
                if (cell.material == TerrainMaterial::AIR) {
                    // Check if this cave floor has stone nearby → leave as air.
                    bool is_floor_candidate = false;
                    for (int dy = 1; dy <= 2; ++dy) {
                        int ny = y + dy;
                        if (terrain.is_valid_cell(x, ny) &&
                            terrain.cell_at(x, ny).is_solid()) {
                            is_floor_candidate = true;
                            break;
                        }
                    }
                    // Place dirt on some cave floors for walkable paths.
                    if (is_floor_candidate && temperature > 0.1f &&
                        humidity > 0.0f) {
                        float floor_noise = humidity_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 4000),
                            static_cast<float>(global_y + 4000),
                            0.1f, 2);
                        if (floor_noise > 0.2f) {
                            set_cell(terrain, x, y, TerrainMaterial::DIRT);
                        }
                    }
                }
                // Sand patches in dry underground caves.
                if (cell.material == TerrainMaterial::AIR) {
                    bool below_stone = false;
                    for (int dy = -1; dy <= 1 && !below_stone; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny) &&
                                terrain.cell_at(nx, ny).material == TerrainMaterial::STONE &&
                                y + dy > y) {
                                below_stone = true;
                                break;
                            }
                        }
                    }
                    if (below_stone && temperature < -0.2f && humidity < -0.1f) {
                        float sand_noise = temp_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 6000),
                            static_cast<float>(global_y + 6000),
                            0.08f, 2);
                        if (sand_noise > 0.3f) {
                            set_cell(terrain, x, y, TerrainMaterial::SAND);
                        }
                    }
                }
            }
        }
    }
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
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    NoiseGenerator struct_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::STRUCTURE), chunk_x, chunk_y));
    NoiseGenerator detail_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::STRUCTURE) + 1, chunk_x, chunk_y));

    // Cross-layer noise: both surface and underground use the same seed
    // to deterministically agree on where features should align.
    NoiseGenerator cross_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::STRUCTURE) + 10, chunk_x, chunk_y));

    // Scan for structure anchor points.
    for (int y = 0; y < ChunkData::kChunkSize; ++y) {
        for (int x = 0; x < ChunkData::kChunkSize; ++x) {
            int global_x = chunk_x * ChunkData::kChunkSize + x;
            int global_y = chunk_y * ChunkData::kChunkSize + y;

            float anchor = struct_noise.noise_2d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                0.03f, 2);
            float placement = detail_noise.noise_2d_scaled(
                static_cast<float>(global_x + 3000),
                static_cast<float>(global_y + 3000),
                0.06f, 2);

            if (layer_id == "surface") {
                // Stone hill formations where anchor noise is high.
                if (anchor > 0.55f && placement > 0.3f &&
                    terrain.cell_at(x, y).material == TerrainMaterial::DIRT) {
                    int radius = static_cast<int>(2 + (anchor - 0.55f) * 4);
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            float dist = std::sqrt(
                                static_cast<float>(dx * dx + dy * dy));
                            float max_dist = static_cast<float>(radius);
                            if (dist > max_dist) continue;

                            int nx = x + dx;
                            int ny = y + dy;
                            if (!terrain.is_valid_cell(nx, ny)) continue;

                            // Inner ring → stone, outer → keep existing.
                            TerrainCell& nc = terrain.cell_at(nx, ny);
                            if (nc.material == TerrainMaterial::DIRT ||
                                nc.material == TerrainMaterial::SAND) {
                                if (dist < max_dist * 0.5f) {
                                    set_cell(terrain, nx, ny, TerrainMaterial::STONE);
                                } else if (dist < max_dist * 0.8f) {
                                    // Transition zone: stone with some dirt.
                                    float transition = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + dx * 7 + 5000),
                                        static_cast<float>(global_y + dy * 7 + 5000),
                                        0.15f, 1);
                                    if (transition > 0.0f) {
                                        set_cell(terrain, nx, ny, TerrainMaterial::STONE);
                                    }
                                }
                            }
                        }
                    }
                }

                // Cliff edges: carve stone into tall vertical faces near water.
                if (terrain.cell_at(x, y).material == TerrainMaterial::WATER) {
                    // Look upward for dirt/stone to turn into cliff.
                    for (int dy = 1; dy <= 3; ++dy) {
                        int ny = y - dy;
                        if (!terrain.is_valid_cell(x, ny)) break;
                        TerrainCell& nc = terrain.cell_at(x, ny);
                        if (nc.material == TerrainMaterial::DIRT &&
                            anchor > 0.3f && placement > 0.0f) {
                            set_cell(terrain, x, ny, TerrainMaterial::STONE);
                        }
                    }
                }

                // Cross-layer: surface sinkholes above underground caverns.
                if ((terrain.cell_at(x, y).material == TerrainMaterial::DIRT ||
                     terrain.cell_at(x, y).material == TerrainMaterial::SAND) &&
                    cross_noise.noise_2d_scaled(
                        static_cast<float>(global_x + 11000),
                        static_cast<float>(global_y + 11000),
                        0.06f, 2) > 0.6f) {
                    int radius = 1 + static_cast<int>(anchor * 2.5f);
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            float dist = std::sqrt(
                                static_cast<float>(dx * dx + dy * dy));
                            if (dist > static_cast<float>(radius) + 0.5f) continue;
                            int nx = x + dx;
                            int ny = y + dy;
                            if (!terrain.is_valid_cell(nx, ny)) continue;
                            TerrainCell& nc = terrain.cell_at(nx, ny);
                            if (nc.material != TerrainMaterial::DIRT &&
                                nc.material != TerrainMaterial::SAND) continue;
                            if (dist > radius * 0.4f) {
                                set_cell(terrain, nx, ny, TerrainMaterial::STONE);
                            } else {
                                set_cell(terrain, nx, ny, TerrainMaterial::AIR);
                            }
                        }
                    }
                }
            } else if (layer_id == "underground") {
                // Large cavern rooms: expand air pockets where anchor is high.
                if (terrain.cell_at(x, y).material == TerrainMaterial::AIR &&
                    anchor > 0.5f && placement > 0.2f) {
                    int carve_radius = static_cast<int>(2 + (anchor - 0.5f) * 5);
                    for (int dy = -carve_radius; dy <= carve_radius; ++dy) {
                        for (int dx = -carve_radius; dx <= carve_radius; ++dx) {
                            float dist = std::sqrt(
                                static_cast<float>(dx * dx + dy * dy));
                            if (dist > static_cast<float>(carve_radius)) continue;

                            int nx = x + dx;
                            int ny = y + dy;
                            if (!terrain.is_valid_cell(nx, ny)) continue;

                            TerrainCell& nc = terrain.cell_at(nx, ny);
                            if (nc.material == TerrainMaterial::STONE) {
                                set_cell(terrain, nx, ny, TerrainMaterial::AIR);
                            }
                        }
                    }
                }

                // Lava pools: deep underground, replace AIR bottom with LAVA.
                if (anchor > 0.4f && placement > 0.5f) {
                    // Find the bottom of an air pocket to place lava.
                    for (int dy = 1; dy <= 3; ++dy) {
                        int ny = y + dy;
                        if (!terrain.is_valid_cell(x, ny)) break;
                        TerrainCell& nc = terrain.cell_at(x, ny);
                        if (nc.material == TerrainMaterial::STONE) {
                            // This air cell is just above stone → lava pool floor.
                            for (int py = y; py >= y - 1 && py >= 0; --py) {
                                TerrainCell& pool_cell = terrain.cell_at(x, py);
                                if (pool_cell.material == TerrainMaterial::AIR) {
                                    float lava_chance = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + 7000),
                                        static_cast<float>(global_y + 7000),
                                        0.1f, 2);
                                    if (lava_chance > 0.4f) {
                                        set_cell(terrain, x, py, TerrainMaterial::LAVA);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }

                // Underground lakes: water pools instead of lava.
                if (anchor < -0.3f && placement > 0.3f) {
                    for (int dy = 1; dy <= 3; ++dy) {
                        int ny = y + dy;
                        if (!terrain.is_valid_cell(x, ny)) break;
                        TerrainCell& nc = terrain.cell_at(x, ny);
                        if (nc.material == TerrainMaterial::STONE) {
                            for (int py = y; py >= y - 1 && py >= 0; --py) {
                                TerrainCell& pool_cell = terrain.cell_at(x, py);
                                if (pool_cell.material == TerrainMaterial::AIR) {
                                    float water_chance = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + 9000),
                                        static_cast<float>(global_y + 9000),
                                        0.08f, 2);
                                    if (water_chance > 0.3f) {
                                        set_cell(terrain, x, py, TerrainMaterial::WATER);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }

                // Cross-layer: underground entrance chambers below surface connectors.
                if (y < 8 && terrain.cell_at(x, y).material == TerrainMaterial::STONE &&
                    cross_noise.noise_2d_scaled(
                        static_cast<float>(global_x + 13000),
                        static_cast<float>(global_y + 13000),
                        0.07f, 2) > 0.55f) {
                    int radius = 2 + static_cast<int>(anchor * 3.0f);
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            float dist = std::sqrt(
                                static_cast<float>(dx * dx + dy * dy));
                            if (dist > static_cast<float>(radius) + 0.5f) continue;
                            int nx = x + dx;
                            int ny = y + dy;
                            if (!terrain.is_valid_cell(nx, ny)) continue;
                            TerrainCell& nc = terrain.cell_at(nx, ny);
                            if (nc.material != TerrainMaterial::STONE) continue;
                            // Floor near bottom of chamber, walls elsewhere.
                            if (dy > 0 && dist < radius * 0.5f) {
                                set_cell(terrain, nx, ny, TerrainMaterial::DIRT);
                            } else {
                                set_cell(terrain, nx, ny, TerrainMaterial::AIR);
                            }
                        }
                    }
                }
            }
        }
    }
}

void TerrainGenerator::pass_object(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    NoiseGenerator obj_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT), chunk_x, chunk_y));
    NoiseGenerator place_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 1, chunk_x, chunk_y));

    // Track placements to avoid overlap.
    std::vector<std::pair<int, int>> used_positions;

    auto is_used = [&](int px, int py) -> bool {
        for (const auto& pos : used_positions) {
            if (std::abs(pos.first - px) <= 1 && std::abs(pos.second - py) <= 1) {
                return true;
            }
        }
        return false;
    };

    if (layer_id == "surface") {
        for (int y = 0; y < ChunkData::kChunkSize; ++y) {
            for (int x = 0; x < ChunkData::kChunkSize; ++x) {
                if (is_used(x, y)) continue;

                int global_x = chunk_x * ChunkData::kChunkSize + x;
                int global_y = chunk_y * ChunkData::kChunkSize + y;
                TerrainCell& cell = terrain.cell_at(x, y);

                // --- Rock clusters on walkable terrain ---
                if ((cell.material == TerrainMaterial::DIRT ||
                     cell.material == TerrainMaterial::SAND) &&
                    !cell.is_solid()) {
                    float rock_density = obj_noise.noise_2d_scaled(
                        static_cast<float>(global_x),
                        static_cast<float>(global_y),
                        0.1f, 2);
                    float rock_place = place_noise.noise_2d_scaled(
                        static_cast<float>(global_x + 2000),
                        static_cast<float>(global_y + 2000),
                        0.12f, 2);

                    if (rock_density > 0.5f && rock_place > 0.4f) {
                        // Place a 2x2 or 1x2 stone cluster.
                        int cluster_w = (rock_place > 0.7f) ? 2 : 1;
                        int cluster_h = (rock_density > 0.7f) ? 2 : 1;
                        for (int dy = 0; dy < cluster_h; ++dy) {
                            for (int dx = 0; dx < cluster_w; ++dx) {
                                int nx = x + dx;
                                int ny = y + dy;
                                if (!terrain.is_valid_cell(nx, ny)) continue;
                                TerrainCell& nc = terrain.cell_at(nx, ny);
                                if (nc.material == TerrainMaterial::DIRT ||
                                    nc.material == TerrainMaterial::SAND) {
                                    set_cell(terrain, nx, ny, TerrainMaterial::STONE);
                                    used_positions.push_back({nx, ny});
                                }
                            }
                        }
                    }
                }

                // --- Stone circles near water (ruin remains) ---
                if (cell.material == TerrainMaterial::DIRT) {
                    bool near_water = false;
                    for (int dy = -3; dy <= 3 && !near_water; ++dy) {
                        for (int dx = -3; dx <= 3 && !near_water; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny) &&
                                terrain.cell_at(nx, ny).material == TerrainMaterial::WATER) {
                                near_water = true;
                            }
                        }
                    }
                    if (near_water) {
                        float circle_noise = obj_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 4000),
                            static_cast<float>(global_y + 4000),
                            0.05f, 1);
                        if (circle_noise > 0.6f && !is_used(x, y)) {
                            // Place a ring of stone blocks.
                            for (int ring_dy = -1; ring_dy <= 1; ++ring_dy) {
                                for (int ring_dx = -1; ring_dx <= 1; ++ring_dx) {
                                    if (ring_dx == 0 && ring_dy == 0) continue;
                                    int nx = x + ring_dx;
                                    int ny = y + ring_dy;
                                    if (!terrain.is_valid_cell(nx, ny)) continue;
                                    TerrainCell& nc = terrain.cell_at(nx, ny);
                                    if (nc.material == TerrainMaterial::DIRT) {
                                        set_cell(terrain, nx, ny, TerrainMaterial::STONE);
                                        used_positions.push_back({nx, ny});
                                    }
                                }
                            }
                            // Center stays as dirt (walkable interior).
                            used_positions.push_back({x, y});
                        }
                    }
                }
            }
        }
    } else if (layer_id == "underground") {
        for (int y = 0; y < ChunkData::kChunkSize; ++y) {
            for (int x = 0; x < ChunkData::kChunkSize; ++x) {
                if (is_used(x, y)) continue;

                int global_x = chunk_x * ChunkData::kChunkSize + x;
                int global_y = chunk_y * ChunkData::kChunkSize + y;
                TerrainCell& cell = terrain.cell_at(x, y);

                // --- Stalactite pillars: stone from ceiling to floor ---
                if (cell.material == TerrainMaterial::AIR) {
                    // Check if there's stone above (ceiling) and below (floor).
                    bool has_ceiling = false;
                    bool has_floor = false;
                    int ceiling_dist = 0;
                    int floor_dist = 0;

                    for (int dy = -1; dy >= -4; --dy) {
                        int ny = y + dy;
                        if (!terrain.is_valid_cell(x, ny)) { has_ceiling = true; break; }
                        if (terrain.cell_at(x, ny).is_solid()) {
                            has_ceiling = true;
                            ceiling_dist = -dy;
                            break;
                        }
                    }
                    for (int dy = 1; dy <= 4; ++dy) {
                        int ny = y + dy;
                        if (!terrain.is_valid_cell(x, ny)) { has_floor = true; break; }
                        if (terrain.cell_at(x, ny).is_solid()) {
                            has_floor = true;
                            floor_dist = dy;
                            break;
                        }
                    }

                    // Place pillar if ceiling and floor are close (within 4 cells).
                    if (has_ceiling && has_floor &&
                        ceiling_dist + floor_dist <= 4 &&
                        ceiling_dist + floor_dist >= 2) {

                        float stalactite_noise = obj_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 6000),
                            static_cast<float>(global_y + 6000),
                            0.08f, 2);
                        if (stalactite_noise > 0.4f && !is_used(x, y)) {
                            // Fill from ceiling down to floor.
                            int top = y - ceiling_dist + 1;
                            int bottom = y + floor_dist - 1;
                            for (int py = top; py <= bottom; ++py) {
                                if (terrain.is_valid_cell(x, py)) {
                                    TerrainCell& pc = terrain.cell_at(x, py);
                                    if (pc.material == TerrainMaterial::AIR) {
                                        set_cell(terrain, x, py, TerrainMaterial::STONE);
                                        used_positions.push_back({x, py});
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Ore cluster decorations near exposed ore veins ---
                if (cell.material == TerrainMaterial::STONE) {
                    // Check if a nearby cell (within 2) already has ore.
                    bool near_ore = false;
                    for (int dy = -2; dy <= 2 && !near_ore; ++dy) {
                        for (int dx = -2; dx <= 2 && !near_ore; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny)) {
                                auto nm = terrain.cell_at(nx, ny).material;
                                if (nm == TerrainMaterial::ORE_IRON ||
                                    nm == TerrainMaterial::ORE_COPPER ||
                                    nm == TerrainMaterial::ORE_COAL) {
                                    near_ore = true;
                                }
                            }
                        }
                    }

                    if (near_ore && !is_used(x, y)) {
                        float extra_ore = obj_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 8000),
                            static_cast<float>(global_y + 8000),
                            0.12f, 1);
                        if (extra_ore > 0.5f) {
                            // Add a small ore cluster near existing ore.
                            float type = place_noise.noise_2d_scaled(
                                static_cast<float>(global_x + 10000),
                                static_cast<float>(global_y + 10000),
                                0.15f, 1);
                            TerrainMaterial ore_type = TerrainMaterial::ORE_COAL;
                            if (type > 0.3f) ore_type = TerrainMaterial::ORE_COPPER;
                            if (type > 0.6f) ore_type = TerrainMaterial::ORE_IRON;
                            set_cell(terrain, x, y, ore_type);
                            used_positions.push_back({x, y});
                        }
                    }
                }
            }
        }
    }
}

void TerrainGenerator::pass_gameplay(
    const std::string& layer_id,
    int chunk_x, int chunk_y,
    ChunkData& chunk) {
    NoiseGenerator placement_noise(
        world_seed_.chunk_seed(
            static_cast<uint32_t>(GenerationPass::GAMEPLAY),
            chunk_x, chunk_y));

    TerrainData& terrain = chunk.terrain;

    if (layer_id == "surface") {
        // Surface connectors: cave entrances and rifts.
        // Each chunk can have at most 2 connectors.
        int max_connectors = 2;
        int placed = 0;

        for (int y = 0; y < ChunkData::kChunkSize && placed < max_connectors; ++y) {
            for (int x = 0; x < ChunkData::kChunkSize && placed < max_connectors; ++x) {
                const TerrainCell& cell = terrain.cell_at(x, y);
                if (!cell.is_walkable()) {
                    continue;
                }

                // Check if there is stone nearby (within 3 cells) indicating a cave feature.
                bool near_stone = false;
                for (int dy = -3; dy <= 3 && !near_stone; ++dy) {
                    for (int dx = -3; dx <= 3 && !near_stone; ++dx) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (terrain.is_valid_cell(nx, ny)) {
                            const TerrainCell& neighbor = terrain.cell_at(nx, ny);
                            if (neighbor.material == TerrainMaterial::STONE) {
                                near_stone = true;
                            }
                        }
                    }
                }

                if (!near_stone) {
                    continue;
                }

                int global_x = chunk_x * ChunkData::kChunkSize + x;
                int global_y = chunk_y * ChunkData::kChunkSize + y;

                float placement_value = placement_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 7000),
                    static_cast<float>(global_y + 7000),
                    0.15f, 1);

                if (placement_value < 0.3f) {
                    continue;
                }

                // Determine connector type based on noise.
                float type_noise = placement_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 3000),
                    static_cast<float>(global_y + 3000),
                    0.25f, 1);

                ConnectorPlacement conn;
                conn.connector_id = world_seed_.connector_id(
                    layer_id, global_x, global_y, placed);
                conn.from_layer = "surface";
                conn.from_cell_x = global_x;
                conn.from_cell_y = global_y;
                conn.to_layer = "underground";
                conn.to_cell_x = global_x;
                conn.to_cell_y = global_y;

                if (type_noise > 0.0f) {
                    conn.connector_type = "cave_entrance";
                    conn.activation_mode = 0;  // INTERACT
                    conn.one_way = false;
                    conn.locked = false;
                } else {
                    conn.connector_type = "rift";
                    conn.activation_mode = 1;  // AUTO_ON_ENTER
                    conn.one_way = true;
                    conn.locked = false;
                }

                chunk.connectors.push_back(std::move(conn));
                ++placed;
            }
        }
    } else if (layer_id == "underground") {
        // Underground connectors: ruin gates and stair exits.
        int max_connectors = 2;
        int placed = 0;

        for (int y = 0; y < ChunkData::kChunkSize && placed < max_connectors; ++y) {
            for (int x = 0; x < ChunkData::kChunkSize && placed < max_connectors; ++x) {
                const TerrainCell& cell = terrain.cell_at(x, y);

                // Connectors only in cave air pockets.
                if (cell.material != TerrainMaterial::AIR) {
                    continue;
                }

                // Check if surrounded by stone (forms a room).
                int stone_neighbors = 0;
                int total_neighbors = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx;
                        int ny = y + dy;
                        if (terrain.is_valid_cell(nx, ny)) {
                            ++total_neighbors;
                            if (terrain.cell_at(nx, ny).is_solid()) {
                                ++stone_neighbors;
                            }
                        }
                    }
                }

                // Need enough stone around to form a gate/room.
                float stone_ratio = (total_neighbors > 0)
                    ? static_cast<float>(stone_neighbors) / static_cast<float>(total_neighbors)
                    : 0.0f;
                if (stone_ratio < 0.5f) {
                    continue;
                }

                int global_x = chunk_x * ChunkData::kChunkSize + x;
                int global_y = chunk_y * ChunkData::kChunkSize + y;

                float placement_value = placement_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 9000),
                    static_cast<float>(global_y + 9000),
                    0.15f, 1);

                if (placement_value < 0.3f) {
                    continue;
                }

                ConnectorPlacement conn;
                conn.connector_id = world_seed_.connector_id(
                    layer_id, global_x, global_y, placed);
                conn.from_layer = "underground";
                conn.from_cell_x = global_x;
                conn.from_cell_y = global_y;
                conn.to_layer = "surface";
                conn.to_cell_x = global_x;
                conn.to_cell_y = global_y;
                conn.connector_type = "ruin_gate";
                conn.activation_mode = 0;  // INTERACT
                conn.one_way = false;
                conn.locked = true;

                chunk.connectors.push_back(std::move(conn));
                ++placed;
            }
        }
    }
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

void TerrainGenerator::pass_tree(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    if (layer_id != "surface") return;

    NoiseGenerator tree_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 10, chunk_x, chunk_y));
    NoiseGenerator canopy_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 11, chunk_x, chunk_y));

    std::vector<std::pair<int, int>> used_positions;

    auto is_used = [&](int px, int py) -> bool {
        for (const auto& pos : used_positions) {
            if (std::abs(pos.first - px) <= 1 && std::abs(pos.second - py) <= 1) {
                return true;
            }
        }
        return false;
    };

    for (int y = 0; y < ChunkData::kChunkSize; ++y) {
        for (int x = 0; x < ChunkData::kChunkSize; ++x) {
            if (is_used(x, y)) continue;

            TerrainCell& cell = terrain.cell_at(x, y);
            if (cell.material != TerrainMaterial::DIRT) continue;

            int global_x = chunk_x * ChunkData::kChunkSize + x;
            int global_y = chunk_y * ChunkData::kChunkSize + y;

            // Tree density noise.
            float tree_density = tree_noise.noise_2d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_y),
                0.12f, 3);

            if (tree_density < 0.45f) continue;

            // Avoid placing trees near water.
            bool near_water = false;
            for (int dy = -2; dy <= 2 && !near_water; ++dy) {
                for (int dx = -2; dx <= 2 && !near_water; ++dx) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (terrain.is_valid_cell(nx, ny) &&
                        terrain.cell_at(nx, ny).material == TerrainMaterial::WATER) {
                        near_water = true;
                    }
                }
            }
            if (near_water) continue;

            // Place trunk.
            set_cell(terrain, x, y, TerrainMaterial::WOOD);
            used_positions.push_back({x, y});

            // Place canopy leaves around trunk.
            float canopy_size = canopy_noise.noise_2d_scaled(
                static_cast<float>(global_x + 3000),
                static_cast<float>(global_y + 3000),
                0.1f, 2);

            int radius = (canopy_size > 0.5f) ? 2 : 1;

            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dy == 0) continue;

                    int nx = x + dx;
                    int ny = y + dy;
                    if (!terrain.is_valid_cell(nx, ny)) continue;

                    TerrainCell& nc = terrain.cell_at(nx, ny);
                    if (nc.material != TerrainMaterial::DIRT &&
                        nc.material != TerrainMaterial::SAND) continue;

                    // Not all canopy cells fill in (gives natural shape).
                    float fill = canopy_noise.noise_2d_scaled(
                        static_cast<float>(global_x + dx * 7 + 5000),
                        static_cast<float>(global_y + dy * 7 + 5000),
                        0.2f, 1);

                    if (fill > 0.0f || (std::abs(dx) <= 1 && std::abs(dy) <= 1)) {
                        set_cell(terrain, nx, ny, TerrainMaterial::LEAVES);
                        used_positions.push_back({nx, ny});
                    }
                }
            }
        }
    }
}

} // namespace science_and_theology