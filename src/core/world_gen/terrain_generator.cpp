#include "terrain_generator.hpp"

#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace science_and_theology {

TerrainGenerator::TerrainGenerator(
    WorldSeed seed,
    std::shared_ptr<const WorldGenConfigSnapshot> config)
    : world_seed_(seed),
      config_(config ? std::move(config) : make_empty_world_gen_config()) {}

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
    const auto* rule = config_->find_base_rule(layer_id);
    if (rule == nullptr) {
        return;
    }

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

            if (rule->mode == "surface_elevation") {
                // Large-scale elevation: dirt plains with water bodies.
                float elevation = elevation_noise.noise_2d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    rule->elevation_scale, rule->elevation_octaves);

                float feature = detail_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 10000),
                    static_cast<float>(global_y + 10000),
                    rule->detail_scale, rule->detail_octaves);

                // Water where elevation is low and feature supports it.
                bool is_water = (elevation < rule->water_elevation_max) &&
                                (feature < rule->water_detail_max);

                // Stone cliffs where elevation is very low or very high.
                bool is_stone = (elevation < -rule->stone_elevation_abs_min) ||
                                (elevation > rule->stone_elevation_abs_min);

                if (is_water) {
                    set_cell_id(terrain, x, y, rule->low_elevation_material);
                } else if (is_stone) {
                    set_cell_id(terrain, x, y, rule->high_elevation_material);
                } else {
                    set_cell_id(terrain, x, y, rule->default_material);
                }
            } else if (rule->mode == "caves") {
                // Underground: solid stone with cave tunnels (air pockets).
                float cave_noise = elevation_noise.noise_2d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_y),
                    rule->cave_scale, rule->cave_octaves);

                // Caves: regions where noise is above threshold.
                // Higher threshold = fewer caves.
                float cave_threshold = rule->cave_threshold;

                // Make caves more open near chunk center, tighter at edges.
                float center_dist_x = std::abs(x - ChunkData::kChunkSize / 2.0f);
                float center_dist_y = std::abs(y - ChunkData::kChunkSize / 2.0f);
                float edge_factor = std::max(center_dist_x, center_dist_y)
                    / (ChunkData::kChunkSize / 2.0f);
                cave_threshold += edge_factor * rule->cave_edge_threshold_add;

                if (cave_noise > cave_threshold) {
                    set_cell_id(terrain, x, y, rule->cave_air_material);
                } else {
                    set_cell_id(terrain, x, y, rule->default_material);
                }
            } else {
                set_cell_id(terrain, x, y, rule->default_material);
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

    std::vector<const BiomeRule*> rules;
    for (const auto& rule : config_->biome_rules) {
        if (rule.layer_id == layer_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

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

            for (const BiomeRule* rule : rules) {
                if (!is_material(cell, rule->source_material)) {
                    continue;
                }
                if (temperature < rule->temperature_min ||
                    temperature > rule->temperature_max ||
                    humidity < rule->humidity_min ||
                    humidity > rule->humidity_max) {
                    continue;
                }
                if (rule->requires_near_material &&
                    !has_near_material(terrain, x, y, rule->near_material, rule->near_radius)) {
                    continue;
                }
                if (rule->requires_floor_support &&
                    !has_floor_support(terrain, x, y, rule->support_material)) {
                    continue;
                }
                if (rule->detail_threshold > -1.0f) {
                    float detail = humidity_noise.noise_2d_scaled(
                        static_cast<float>(global_x + 4000),
                        static_cast<float>(global_y + 4000),
                        rule->detail_scale,
                        rule->detail_octaves);
                    if (detail <= rule->detail_threshold) {
                        continue;
                    }
                }

                set_cell_id(terrain, x, y, rule->result_material);
                break;
            }
        }
    }
}

void TerrainGenerator::pass_ore(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    std::vector<const OreVeinRule*> rules;
    for (const auto& rule : config_->ore_vein_rules) {
        if (rule.layer_id == layer_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

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

            for (const OreVeinRule* rule : rules) {
                if (!is_material(cell, rule->host_material)) {
                    continue;
                }
                if (combined > rule->combined_min && combined <= rule->combined_max) {
                    set_cell_id(terrain, x, y, rule->ore_material);
                    break;
                }
            }
        }
    }
}

void TerrainGenerator::pass_structure(
    const std::string& layer_id, int chunk_x, int chunk_y,
    TerrainData& terrain) {
    const auto mat = materials();
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
                    is_material(terrain.cell_at(x, y), mat.dirt)) {
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
                            if (is_walkable_ground_cell(nc)) {
                                if (dist < max_dist * 0.5f) {
                                    set_cell_id(terrain, nx, ny, mat.stone);
                                } else if (dist < max_dist * 0.8f) {
                                    // Transition zone: stone with some dirt.
                                    float transition = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + dx * 7 + 5000),
                                        static_cast<float>(global_y + dy * 7 + 5000),
                                        0.15f, 1);
                                    if (transition > 0.0f) {
                                        set_cell_id(terrain, nx, ny, mat.stone);
                                    }
                                }
                            }
                        }
                    }
                }

                // Cliff edges: carve stone into tall vertical faces near water.
                if (is_material(terrain.cell_at(x, y), mat.water)) {
                    // Look upward for dirt/stone to turn into cliff.
                    for (int dy = 1; dy <= 3; ++dy) {
                        int ny = y - dy;
                        if (!terrain.is_valid_cell(x, ny)) break;
                        TerrainCell& nc = terrain.cell_at(x, ny);
                        if (is_material(nc, mat.dirt) &&
                            anchor > 0.3f && placement > 0.0f) {
                            set_cell_id(terrain, x, ny, mat.stone);
                        }
                    }
                }

                // Cross-layer: surface sinkholes above underground caverns.
                if (is_walkable_ground_cell(terrain.cell_at(x, y)) &&
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
                            if (!is_walkable_ground_cell(nc)) continue;
                            if (dist > radius * 0.4f) {
                                set_cell_id(terrain, nx, ny, mat.stone);
                            } else {
                                set_cell_id(terrain, nx, ny, mat.air);
                            }
                        }
                    }
                }
            } else if (layer_id == "underground") {
                // Large cavern rooms: expand air pockets where anchor is high.
                if (is_material(terrain.cell_at(x, y), mat.air) &&
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
                            if (is_material(nc, mat.stone)) {
                                set_cell_id(terrain, nx, ny, mat.air);
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
                        if (is_material(nc, mat.stone)) {
                            // This air cell is just above stone → lava pool floor.
                            for (int py = y; py >= y - 1 && py >= 0; --py) {
                                TerrainCell& pool_cell = terrain.cell_at(x, py);
                                if (is_material(pool_cell, mat.air)) {
                                    float lava_chance = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + 7000),
                                        static_cast<float>(global_y + 7000),
                                        0.1f, 2);
                                    if (lava_chance > 0.4f) {
                                        set_cell_id(terrain, x, py, mat.lava);
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
                        if (is_material(nc, mat.stone)) {
                            for (int py = y; py >= y - 1 && py >= 0; --py) {
                                TerrainCell& pool_cell = terrain.cell_at(x, py);
                                if (is_material(pool_cell, mat.air)) {
                                    float water_chance = detail_noise.noise_2d_scaled(
                                        static_cast<float>(global_x + 9000),
                                        static_cast<float>(global_y + 9000),
                                        0.08f, 2);
                                    if (water_chance > 0.3f) {
                                        set_cell_id(terrain, x, py, mat.water);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }

                // Cross-layer: underground entrance chambers below surface connectors.
                if (y < 8 && is_material(terrain.cell_at(x, y), mat.stone) &&
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
                            if (!is_material(nc, mat.stone)) continue;
                            // Floor near bottom of chamber, walls elsewhere.
                            if (dy > 0 && dist < radius * 0.5f) {
                                set_cell_id(terrain, nx, ny, mat.dirt);
                            } else {
                                set_cell_id(terrain, nx, ny, mat.air);
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
    const auto mat = materials();
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
                if (is_walkable_ground_cell(cell) && !cell.is_solid()) {
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
                                if (is_walkable_ground_cell(nc)) {
                                    set_cell_id(terrain, nx, ny, mat.stone);
                                    used_positions.push_back({nx, ny});
                                }
                            }
                        }
                    }
                }

                // --- Stone circles near water (ruin remains) ---
                if (is_material(cell, mat.dirt)) {
                    bool near_water = false;
                    for (int dy = -3; dy <= 3 && !near_water; ++dy) {
                        for (int dx = -3; dx <= 3 && !near_water; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny) &&
                                is_material(terrain.cell_at(nx, ny), mat.water)) {
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
                                    if (is_material(nc, mat.dirt)) {
                                        set_cell_id(terrain, nx, ny, mat.stone);
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
                if (is_material(cell, mat.air)) {
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
                                    if (is_material(pc, mat.air)) {
                                        set_cell_id(terrain, x, py, mat.stone);
                                        used_positions.push_back({x, py});
                                    }
                                }
                            }
                        }
                    }
                }

                // --- Ore cluster decorations near exposed ore veins ---
                if (is_material(cell, mat.stone)) {
                    // Check if a nearby cell (within 2) already has ore.
                    bool near_ore = false;
                    for (int dy = -2; dy <= 2 && !near_ore; ++dy) {
                        for (int dx = -2; dx <= 2 && !near_ore; ++dx) {
                            int nx = x + dx;
                            int ny = y + dy;
                            if (terrain.is_valid_cell(nx, ny)) {
                                auto nm = cell_material_id(terrain.cell_at(nx, ny));
                                if (nm == mat.ore_iron ||
                                    nm == mat.ore_copper ||
                                    nm == mat.ore_coal) {
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
                            TerrainMaterialId ore_type = mat.ore_coal;
                            if (type > 0.3f) ore_type = mat.ore_copper;
                            if (type > 0.6f) ore_type = mat.ore_iron;
                            set_cell_id(terrain, x, y, ore_type);
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
    const auto mat = materials();
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
                            if (is_material(neighbor, mat.stone)) {
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
                if (!is_material(cell, mat.air)) {
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

                const int64_t connector_id = conn.connector_id;
                chunk.connectors.push_back(std::move(conn));

                MechanismPlacement mechanism;
                mechanism.mechanism_id = world_seed_.mechanism_id(
                    layer_id, global_x, global_y, placed);
                mechanism.layer_id = "underground";
                mechanism.cell_x = global_x;
                mechanism.cell_y = global_y;
                mechanism.display_name = "Ruin Gate Sigil";
                mechanism.action_label = "Unlock Ruin Gate";
                mechanism.flag_name = mechanism.mechanism_id + "_active";
                mechanism.activation_mode = 0;  // INTERACT
                mechanism.one_shot = true;

                MechanismEffectPlacement effect;
                effect.effect_type = "connector_locked";
                effect.connector_id = connector_id;
                effect.when_active = false;
                effect.when_inactive = true;
                mechanism.effects.push_back(std::move(effect));

                chunk.mechanisms.push_back(std::move(mechanism));
                ++placed;
            }
        }
    }
}

void TerrainGenerator::set_cell(
    TerrainData& terrain, int x, int y, TerrainMaterial material) {
    set_cell_id(terrain, x, y, static_cast<TerrainMaterialId>(material));
}

void TerrainGenerator::set_cell_id(
    TerrainData& terrain, int x, int y, TerrainMaterialId material) {
    TerrainCell& cell = terrain.cell_at(x, y);
    cell.material = static_cast<TerrainMaterial>(material);
    cell.flags = flags_for_material_id(material);
}

uint32_t TerrainGenerator::flags_for_material(TerrainMaterial material) const {
    return flags_for_material_id(static_cast<TerrainMaterialId>(material));
}

uint32_t TerrainGenerator::flags_for_material_id(TerrainMaterialId material) const {
    return config_->flags_for_material(material);
}

TerrainMaterialId TerrainGenerator::cell_material_id(const TerrainCell& cell) const {
    return static_cast<TerrainMaterialId>(cell.material);
}

bool TerrainGenerator::is_stone_cell(const TerrainCell& cell) const {
    return is_material(cell, materials().stone);
}

bool TerrainGenerator::is_material(
    const TerrainCell& cell, TerrainMaterialId material) const {
    return cell_material_id(cell) == material;
}

bool TerrainGenerator::is_walkable_ground_cell(const TerrainCell& cell) const {
    return config_->is_walkable_ground(cell_material_id(cell));
}

bool TerrainGenerator::has_near_material(
    const TerrainData& terrain, int x, int y,
    TerrainMaterialId material, int radius) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            int ny = y + dy;
            if (terrain.is_valid_cell(nx, ny) &&
                is_material(terrain.cell_at(nx, ny), material)) {
                return true;
            }
        }
    }
    return false;
}

bool TerrainGenerator::has_floor_support(
    const TerrainData& terrain, int x, int y,
    TerrainMaterialId support_material) const {
    for (int dy = 1; dy <= 2; ++dy) {
        int ny = y + dy;
        if (terrain.is_valid_cell(x, ny) &&
            is_material(terrain.cell_at(x, ny), support_material)) {
            return true;
        }
    }
    return false;
}

TerrainGenerator::MaterialIds TerrainGenerator::materials() const {
    MaterialIds ids;
    ids.air = config_->roles.air;
    ids.stone = config_->roles.stone;
    ids.dirt = config_->roles.dirt;
    ids.sand = config_->roles.sand;
    ids.water = config_->roles.water;
    ids.lava = config_->roles.lava;
    ids.ore_iron = config_->roles.ore_iron;
    ids.ore_copper = config_->roles.ore_copper;
    ids.ore_coal = config_->roles.ore_coal;
    ids.wood = config_->roles.wood;
    ids.leaves = config_->roles.leaves;
    return ids;
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
    const auto mat = materials();

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
            if (!is_material(cell, mat.dirt)) continue;

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
                        is_material(terrain.cell_at(nx, ny), mat.water)) {
                        near_water = true;
                    }
                }
            }
            if (near_water) continue;

            // Place trunk.
            set_cell_id(terrain, x, y, mat.wood);
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
                    if (!is_walkable_ground_cell(nc)) continue;

                    // Not all canopy cells fill in (gives natural shape).
                    float fill = canopy_noise.noise_2d_scaled(
                        static_cast<float>(global_x + dx * 7 + 5000),
                        static_cast<float>(global_y + dy * 7 + 5000),
                        0.2f, 1);

                    if (fill > 0.0f || (std::abs(dx) <= 1 && std::abs(dy) <= 1)) {
                        set_cell_id(terrain, nx, ny, mat.leaves);
                        used_positions.push_back({nx, ny});
                    }
                }
            }
        }
    }
}

} // namespace science_and_theology
