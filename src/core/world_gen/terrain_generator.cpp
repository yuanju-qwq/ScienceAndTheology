#include "terrain_generator.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace science_and_theology {

namespace {

int global_coord(int chunk_coord, int local_coord) {
    return chunk_coord * ChunkData::kChunkSize + local_coord;
}

bool is_exposed_to_air(const TerrainData& terrain, int x, int y, int z,
                       TerrainMaterialId air) {
    if (y + 1 >= terrain.size_y) {
        return true;
    }
    return static_cast<TerrainMaterialId>(
        terrain.cell_at(x, y + 1, z).material) == air;
}

std::string normalize_dimension_id(const std::string& id) {
    if (id == "surface" || id.empty()) {
        return "overworld";
    }
    return id;
}

} // namespace

TerrainGenerator::TerrainGenerator(
    WorldSeed seed,
    std::shared_ptr<const WorldGenConfigSnapshot> config)
    : world_seed_(seed),
      config_(config ? std::move(config) : make_empty_world_gen_config()) {}

ChunkData TerrainGenerator::generate_chunk(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z) {
    const std::string normalized_dimension = normalize_dimension_id(dimension_id);
    ChunkData chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    chunk.chunk_z = chunk_z;
    chunk.state = ChunkState::GENERATED;
    chunk.terrain.resize(
        ChunkData::kChunkSize, ChunkData::kChunkSize, ChunkData::kChunkSize);

    pass_base_terrain(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_biome(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_ore(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_surface_objects(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk.terrain);
    pass_gameplay(normalized_dimension, chunk_x, chunk_y, chunk_z, chunk);

    return chunk;
}

void TerrainGenerator::pass_base_terrain(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    const auto mat = materials();
    const auto* rule = config_->find_base_rule(dimension_id);
    if (rule == nullptr) {
        for (int y = 0; y < terrain.size_y; ++y) {
            for (int z = 0; z < terrain.size_z; ++z) {
                for (int x = 0; x < terrain.size_x; ++x) {
                    set_cell_id(terrain, x, y, z, mat.air);
                }
            }
        }
        return;
    }

    NoiseGenerator elevation_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN),
        chunk_x, chunk_y, chunk_z));
    NoiseGenerator detail_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 1,
        chunk_x, chunk_y, chunk_z));
    NoiseGenerator cave_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BASE_TERRAIN) + 2,
        chunk_x, chunk_y, chunk_z));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);
                const int surface_height =
                    surface_height_at(elevation_noise, global_x, global_z, *rule);

                TerrainMaterialId material = mat.air;
                if (global_y > surface_height) {
                    if (global_y <= 0 && surface_height < 0) {
                        material = rule->low_elevation_material != 0
                            ? rule->low_elevation_material
                            : mat.water;
                    } else {
                        material = mat.air;
                    }
                } else if (global_y == surface_height) {
                    const float detail = detail_noise.noise_2d_scaled(
                        static_cast<float>(global_x + 10000),
                        static_cast<float>(global_z + 10000),
                        rule->detail_scale, rule->detail_octaves);
                    if (surface_height >= 8 || std::abs(detail) > rule->stone_elevation_abs_min) {
                        material = rule->high_elevation_material != 0
                            ? rule->high_elevation_material
                            : mat.stone;
                    } else {
                        material = rule->default_material != 0
                            ? rule->default_material
                            : mat.dirt;
                    }
                } else if (global_y >= surface_height - 3 && global_y >= -8) {
                    material = rule->default_material != 0
                        ? rule->default_material
                        : mat.dirt;
                } else {
                    material = rule->high_elevation_material != 0
                        ? rule->high_elevation_material
                        : mat.stone;
                }

                if (material != mat.air && global_y < surface_height - 3) {
                    const float cave = cave_noise_at(
                        cave_noise, global_x, global_y, global_z, *rule);
                    float threshold = rule->cave_threshold;
                    threshold += std::clamp((-global_y - 8) / 64.0f, 0.0f, 0.18f);
                    if (cave > threshold) {
                        material = global_y < -24 ? mat.lava : mat.air;
                    }
                }

                set_cell_id(terrain, x, y, z, material);
            }
        }
    }
}

void TerrainGenerator::pass_biome(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    std::vector<const BiomeRule*> rules;
    for (const auto& rule : config_->biome_rules) {
        if (rule.dimension_id == dimension_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

    const auto mat = materials();
    NoiseGenerator temp_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BIOME), chunk_x, chunk_y, chunk_z));
    NoiseGenerator humidity_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::BIOME) + 1, chunk_x, chunk_y, chunk_z));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                TerrainCell& cell = terrain.cell_at(x, y, z);
                if (!is_exposed_to_air(terrain, x, y, z, mat.air)) {
                    continue;
                }

                const int global_x = global_coord(chunk_x, x);
                const int global_z = global_coord(chunk_z, z);
                const float temperature = temp_noise.noise_2d_scaled(
                    static_cast<float>(global_x),
                    static_cast<float>(global_z),
                    0.015f, 3);
                const float humidity = humidity_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 2000),
                    static_cast<float>(global_z + 2000),
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
                        !has_near_material(
                            terrain, x, y, z, rule->near_material, rule->near_radius)) {
                        continue;
                    }
                    if (rule->requires_floor_support &&
                        !has_floor_support(terrain, x, y, z, rule->support_material)) {
                        continue;
                    }
                    if (rule->detail_threshold > -1.0f) {
                        const float detail = humidity_noise.noise_2d_scaled(
                            static_cast<float>(global_x + 4000),
                            static_cast<float>(global_z + 4000),
                            rule->detail_scale,
                            rule->detail_octaves);
                        if (detail <= rule->detail_threshold) {
                            continue;
                        }
                    }
                    set_cell_id(terrain, x, y, z, rule->result_material);
                    break;
                }
            }
        }
    }
}

void TerrainGenerator::pass_ore(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    std::vector<const OreVeinRule*> rules;
    for (const auto& rule : config_->ore_vein_rules) {
        if (rule.dimension_id == dimension_id) {
            rules.push_back(&rule);
        }
    }
    if (rules.empty()) {
        return;
    }

    NoiseGenerator ore_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::ORE), chunk_x, chunk_y, chunk_z));
    NoiseGenerator vein_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::ORE) + 1, chunk_x, chunk_y, chunk_z));

    for (int y = 0; y < terrain.size_y; ++y) {
        for (int z = 0; z < terrain.size_z; ++z) {
            for (int x = 0; x < terrain.size_x; ++x) {
                TerrainCell& cell = terrain.cell_at(x, y, z);
                const int global_x = global_coord(chunk_x, x);
                const int global_y = global_coord(chunk_y, y);
                const int global_z = global_coord(chunk_z, z);

                const float ore_density = ore_noise.noise_2d_scaled(
                    static_cast<float>(global_x + global_y * 11),
                    static_cast<float>(global_z - global_y * 7),
                    0.08f, 3);
                const float vein_shape = vein_noise.noise_2d_scaled(
                    static_cast<float>(global_x + 5000),
                    static_cast<float>(global_z + global_y * 13 + 5000),
                    0.15f, 2);
                const float depth_bonus = std::clamp((-global_y) / 64.0f, 0.0f, 0.28f);
                const float combined = ore_density * 0.45f + vein_shape * 0.45f + depth_bonus;

                for (const OreVeinRule* rule : rules) {
                    if (!is_material(cell, rule->host_material)) {
                        continue;
                    }
                    if (combined > rule->combined_min && combined <= rule->combined_max) {
                        set_cell_id(terrain, x, y, z, rule->ore_material);
                        break;
                    }
                }
            }
        }
    }
}

void TerrainGenerator::pass_surface_objects(
    const std::string& dimension_id, int chunk_x, int chunk_y, int chunk_z,
    TerrainData& terrain) {
    if (dimension_id != "overworld") {
        return;
    }

    const auto mat = materials();
    NoiseGenerator tree_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT), chunk_x, chunk_y, chunk_z));
    NoiseGenerator canopy_noise(world_seed_.chunk_seed(
        static_cast<uint32_t>(GenerationPass::OBJECT) + 1, chunk_x, chunk_y, chunk_z));

    for (int z = 0; z < terrain.size_z; ++z) {
        for (int x = 0; x < terrain.size_x; ++x) {
            int ground_y = -1;
            for (int y = terrain.size_y - 1; y >= 0; --y) {
                const TerrainCell& cell = terrain.cell_at(x, y, z);
                if (is_material(cell, mat.dirt) && is_exposed_to_air(terrain, x, y, z, mat.air)) {
                    ground_y = y;
                    break;
                }
            }
            if (ground_y < 0 || ground_y + 5 >= terrain.size_y) {
                continue;
            }

            const int global_x = global_coord(chunk_x, x);
            const int global_z = global_coord(chunk_z, z);
            const float tree_density = tree_noise.noise_2d_scaled(
                static_cast<float>(global_x),
                static_cast<float>(global_z),
                0.12f, 3);
            if (tree_density < 0.54f) {
                continue;
            }

            bool near_water = false;
            for (int dz = -2; dz <= 2 && !near_water; ++dz) {
                for (int dx = -2; dx <= 2 && !near_water; ++dx) {
                    for (int dy = -1; dy <= 1 && !near_water; ++dy) {
                        const int nx = x + dx;
                        const int ny = ground_y + dy;
                        const int nz = z + dz;
                        if (terrain.is_valid_cell(nx, ny, nz) &&
                            is_material(terrain.cell_at(nx, ny, nz), mat.water)) {
                            near_water = true;
                        }
                    }
                }
            }
            if (near_water) {
                continue;
            }

            for (int trunk = 1; trunk <= 3; ++trunk) {
                set_cell_id(terrain, x, ground_y + trunk, z, mat.wood);
            }

            const int canopy_y = ground_y + 4;
            const float canopy_size = canopy_noise.noise_2d_scaled(
                static_cast<float>(global_x + 3000),
                static_cast<float>(global_z + 3000),
                0.1f, 2);
            const int radius = canopy_size > 0.5f ? 2 : 1;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) + std::abs(dy) > radius + 1) {
                            continue;
                        }
                        const int nx = x + dx;
                        const int ny = canopy_y + dy;
                        const int nz = z + dz;
                        if (!terrain.is_valid_cell(nx, ny, nz)) {
                            continue;
                        }
                        if (!is_material(terrain.cell_at(nx, ny, nz), mat.air)) {
                            continue;
                        }
                        set_cell_id(terrain, nx, ny, nz, mat.leaves);
                    }
                }
            }
        }
    }
}

void TerrainGenerator::pass_gameplay(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    ChunkData& chunk) {
    (void)dimension_id;
    (void)chunk_x;
    (void)chunk_y;
    (void)chunk_z;
    (void)chunk;
}

void TerrainGenerator::set_cell(
    TerrainData& terrain, int x, int y, int z, TerrainMaterial material) {
    set_cell_id(terrain, x, y, z, static_cast<TerrainMaterialId>(material));
}

void TerrainGenerator::set_cell_id(
    TerrainData& terrain, int x, int y, int z, TerrainMaterialId material) {
    TerrainCell& cell = terrain.cell_at(x, y, z);
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
    const TerrainData& terrain, int x, int y, int z,
    TerrainMaterialId material, int radius) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                const int nz = z + dz;
                if (terrain.is_valid_cell(nx, ny, nz) &&
                    is_material(terrain.cell_at(nx, ny, nz), material)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool TerrainGenerator::has_floor_support(
    const TerrainData& terrain, int x, int y, int z,
    TerrainMaterialId support_material) const {
    for (int dy = 1; dy <= 2; ++dy) {
        const int ny = y - dy;
        if (terrain.is_valid_cell(x, ny, z) &&
            is_material(terrain.cell_at(x, ny, z), support_material)) {
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

int TerrainGenerator::surface_height_at(
    const NoiseGenerator& elevation_noise,
    int global_x, int global_z,
    const BaseTerrainRule& rule) const {
    if (std::abs(global_x) <= 4 && std::abs(global_z) <= 4) {
        return 0;
    }

    const float elevation = elevation_noise.noise_2d_scaled(
        static_cast<float>(global_x),
        static_cast<float>(global_z),
        rule.elevation_scale, rule.elevation_octaves);
    const float broad = elevation_noise.noise_2d_scaled(
        static_cast<float>(global_x + 60000),
        static_cast<float>(global_z - 60000),
        rule.elevation_scale * 0.35f, std::max(1, rule.elevation_octaves - 1));

    return static_cast<int>(std::round(elevation * 9.0f + broad * 14.0f));
}

float TerrainGenerator::cave_noise_at(
    const NoiseGenerator& cave_noise,
    int global_x, int global_y, int global_z,
    const BaseTerrainRule& rule) const {
    const float horizontal = cave_noise.noise_2d_scaled(
        static_cast<float>(global_x),
        static_cast<float>(global_z),
        rule.cave_scale, rule.cave_octaves);
    const float vertical = cave_noise.noise_2d_scaled(
        static_cast<float>(global_x + global_y * 17 + 30000),
        static_cast<float>(global_z - global_y * 19 - 30000),
        rule.cave_scale * 1.35f, rule.cave_octaves);
    return horizontal * 0.55f + vertical * 0.45f;
}

} // namespace science_and_theology
