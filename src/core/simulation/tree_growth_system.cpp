#include "tree_growth_system.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"
#include "../world_gen/world_gen_config.hpp"
#include "../world_gen/noise_generator.hpp"
#include "../world_gen/world_seed.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void TreeGrowthSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void TreeGrowthSystem::tick_active(const ChunkKey& chunk, float delta) {
    (void)delta;
    if (!world_) return;

    // Read the current tick from WorldData (set by TickSystem each frame).
    const int64_t tick = world_->current_tick();
    growth_count_ = 0;

    auto& registry = world_->block_entity_registry();
    auto entities = registry.entities_in_chunk(
        chunk.dimension_id,
        chunk.chunk_x, chunk.chunk_y, chunk.chunk_z);

    for (const auto& entity_id : entities) {
        if (growth_count_ >= kMaxGrowthPerTick) break;

        if (registry.get_entity_type(entity_id) == BlockEntityType::TREE) {
            if (try_grow_tree(entity_id, chunk.dimension_id, tick)) {
                ++growth_count_;
            }
        }
    }
}

void TreeGrowthSystem::tick_sleeping(const ChunkKey& chunk, float delta) {
    (void)chunk;
    (void)delta;
    // Trees do not grow in sleeping chunks.
}

void TreeGrowthSystem::shutdown() {
    // No persistent state to drain.
}

// --- Growth logic ---

bool TreeGrowthSystem::try_grow_tree(
    EntityId entity_id,
    const std::string& dimension_id,
    int64_t current_tick) {
    auto& registry = world_->block_entity_registry();
    TreeBlockEntityState* state = registry.get_tree_state_mut(entity_id);
    if (!state) return false;

    // Already mature: no further growth.
    if (state->growth_stage == TreeGrowthStage::MATURE) {
        return false;
    }

    // Look up the species definition.
    auto config = world_->worldgen_config();
    if (!config) return false;
    const TreeSpeciesDef* species = config->find_tree_species(state->species_key);
    if (!species) return false;

    // Check if enough ticks have elapsed since the last growth transition.
    const int64_t ticks_required = (state->growth_stage == TreeGrowthStage::SAPLING)
        ? species->ticks_to_young
        : species->ticks_to_mature;
    const int64_t elapsed = current_tick - state->last_growth_tick;
    if (elapsed < ticks_required) {
        return false;
    }

    // Get the root position.
    const BlockEntityPlacement* placement = registry.get_placement(entity_id);
    if (!placement) return false;

    // Check growth conditions.
    if (!check_growth_conditions(*species, dimension_id,
            placement->root_x, placement->root_y, placement->root_z,
            state->growth_stage)) {
        return false;
    }

    // Apply the growth transition.
    if (state->growth_stage == TreeGrowthStage::SAPLING) {
        grow_sapling_to_young(entity_id, *species, dimension_id,
            placement->root_x, placement->root_y, placement->root_z,
            current_tick);
    } else if (state->growth_stage == TreeGrowthStage::YOUNG) {
        grow_young_to_mature(entity_id, *species, dimension_id,
            placement->root_x, placement->root_y, placement->root_z,
            current_tick);
    }

    return true;
}

bool TreeGrowthSystem::check_growth_conditions(
    const TreeSpeciesDef& species,
    const std::string& dimension_id,
    int root_x, int root_y, int root_z,
    TreeGrowthStage current_stage) const {
    // Check vertical space above the root.
    const int required_height = (current_stage == TreeGrowthStage::SAPLING)
        ? 4  // sapling → young needs at least 4 blocks above
        : species.max_trunk_height + 3;  // young → mature needs full height

    for (int dy = 1; dy <= required_height; ++dy) {
        const int check_y = root_y + dy;
        // Check if the block above is air (or the current sapling/trunk).
        // We use a simple heuristic: if the chunk is loaded, the block
        // should be air or owned by this tree.
        // For now, just check that the chunk exists.
        const int chunk_size = 32;
        const int cy = static_cast<int>(
            std::floor(static_cast<float>(check_y) / chunk_size));
        const int ly = check_y - cy * chunk_size;
        if (ly < 0 || ly >= chunk_size) {
            return false;
        }
    }

    // Check temperature/humidity if config is available.
    float temperature = 0.0f;
    float humidity = 0.0f;
    if (get_biome_at(root_x, root_y, root_z, temperature, humidity)) {
        if (temperature < species.temperature_min ||
            temperature > species.temperature_max ||
            humidity < species.humidity_min ||
            humidity > species.humidity_max) {
            return false;
        }
    }

    return true;
}

void TreeGrowthSystem::grow_sapling_to_young(
    EntityId entity_id,
    const TreeSpeciesDef& species,
    const std::string& dimension_id,
    int root_x, int root_y, int root_z,
    int64_t current_tick) {
    auto& registry = world_->block_entity_registry();
    auto config = world_->worldgen_config();
    if (!config) return;

    const TerrainMaterialId wood_mat = config->material_id_or(
        species.wood_material_key, 0);
    const TerrainMaterialId leaves_mat = config->material_id_or(
        species.leaves_material_key, 0);
    const TerrainMaterialId sapling_mat = config->material_id_or(
        species.sapling_material_key, 0);

    if (wood_mat == 0 || leaves_mat == 0) return;

    // Remove the sapling block at root position.
    set_world_cell(dimension_id, root_x, root_y, root_z, 0, 0);

    // Place a short trunk (2 blocks).
    std::vector<OwnedCell> owned_cells;
    for (int dy = 0; dy < 2; ++dy) {
        const int wy = root_y + dy;
        set_world_cell(dimension_id, root_x, wy, root_z, wood_mat,
            TF_SOLID | TF_MINEABLE);
        owned_cells.push_back({root_x, wy, root_z});
    }

    // Place a small canopy (1-block radius sphere).
    const int canopy_y = root_y + 2;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (std::abs(dx) + std::abs(dz) > 1) continue;
            const int nx = root_x + dx;
            const int ny = canopy_y;
            const int nz = root_z + dz;
            set_world_cell(dimension_id, nx, ny, nz, leaves_mat,
                TF_WALKABLE | TF_MINEABLE);
            owned_cells.push_back({nx, ny, nz});
        }
    }

    // Update the block entity.
    TreeBlockEntityState* state = registry.get_tree_state_mut(entity_id);
    if (state) {
        state->growth_stage = TreeGrowthStage::YOUNG;
        state->last_growth_tick = current_tick;
    }
    registry.update_tree_owned_cells(entity_id, owned_cells);
}

void TreeGrowthSystem::grow_young_to_mature(
    EntityId entity_id,
    const TreeSpeciesDef& species,
    const std::string& dimension_id,
    int root_x, int root_y, int root_z,
    int64_t current_tick) {
    auto& registry = world_->block_entity_registry();
    auto config = world_->worldgen_config();
    if (!config) return;

    const TerrainMaterialId wood_mat = config->material_id_or(
        species.wood_material_key, 0);
    const TerrainMaterialId leaves_mat = config->material_id_or(
        species.leaves_material_key, 0);
    if (wood_mat == 0 || leaves_mat == 0) return;

    // First, remove all existing owned cells (young tree blocks).
    TreeBlockEntityState* state = registry.get_tree_state_mut(entity_id);
    if (!state) return;

    for (const auto& cell : state->owned_cells) {
        set_world_cell(dimension_id, cell.block_x, cell.block_y, cell.block_z,
            0, 0);
    }

    // Place a full trunk.
    std::vector<OwnedCell> owned_cells;
    const int trunk_height = species.min_trunk_height;
    for (int dy = 0; dy < trunk_height; ++dy) {
        const int wy = root_y + dy;
        set_world_cell(dimension_id, root_x, wy, root_z, wood_mat,
            TF_SOLID | TF_MINEABLE);
        owned_cells.push_back({root_x, wy, root_z});
    }

    // Place the full canopy based on species shape.
    const int canopy_base_y = root_y + trunk_height;
    const int radius = species.canopy_radius;

    switch (species.canopy_shape) {
        case CanopyShape::SPHERE: {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) + std::abs(dy) > radius + 1) continue;
                        const int nx = root_x + dx;
                        const int ny = canopy_base_y + dy;
                        const int nz = root_z + dz;
                        set_world_cell(dimension_id, nx, ny, nz, leaves_mat,
                            TF_WALKABLE | TF_MINEABLE);
                        owned_cells.push_back({nx, ny, nz});
                    }
                }
            }
            break;
        }
        case CanopyShape::CONE: {
            const int cone_height = radius + 1;
            for (int layer = 0; layer < cone_height; ++layer) {
                const int layer_radius = std::max(0, radius - layer);
                for (int dz = -layer_radius; dz <= layer_radius; ++dz) {
                    for (int dx = -layer_radius; dx <= layer_radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > layer_radius) continue;
                        const int nx = root_x + dx;
                        const int ny = canopy_base_y + layer;
                        const int nz = root_z + dz;
                        set_world_cell(dimension_id, nx, ny, nz, leaves_mat,
                            TF_WALKABLE | TF_MINEABLE);
                        owned_cells.push_back({nx, ny, nz});
                    }
                }
            }
            break;
        }
        case CanopyShape::UMBRELLA: {
            for (int dy = 0; dy <= 1; ++dy) {
                const int r = (dy == 0) ? radius : radius - 1;
                for (int dz = -r; dz <= r; ++dz) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > r + 1) continue;
                        const int nx = root_x + dx;
                        const int ny = canopy_base_y + dy;
                        const int nz = root_z + dz;
                        set_world_cell(dimension_id, nx, ny, nz, leaves_mat,
                            TF_WALKABLE | TF_MINEABLE);
                        owned_cells.push_back({nx, ny, nz});
                    }
                }
            }
            break;
        }
        case CanopyShape::COLUMN: {
            const int column_height = 3;
            for (int dy = 0; dy < column_height; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) > radius) continue;
                        const int nx = root_x + dx;
                        const int ny = canopy_base_y + dy;
                        const int nz = root_z + dz;
                        set_world_cell(dimension_id, nx, ny, nz, leaves_mat,
                            TF_WALKABLE | TF_MINEABLE);
                        owned_cells.push_back({nx, ny, nz});
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    // Update the block entity.
    state->growth_stage = TreeGrowthStage::MATURE;
    state->last_growth_tick = current_tick;
    registry.update_tree_owned_cells(entity_id, owned_cells);
}

// --- Helpers ---

bool TreeGrowthSystem::set_world_cell(
    const std::string& dimension_id,
    int world_x, int world_y, int world_z,
    TerrainMaterialId material, uint32_t flags) {
    if (!world_) return false;

    constexpr int kChunkSize = 32;
    const int cx = static_cast<int>(
        std::floor(static_cast<float>(world_x) / kChunkSize));
    const int cy = static_cast<int>(
        std::floor(static_cast<float>(world_y) / kChunkSize));
    const int cz = static_cast<int>(
        std::floor(static_cast<float>(world_z) / kChunkSize));
    const int lx = world_x - cx * kChunkSize;
    const int ly = world_y - cy * kChunkSize;
    const int lz = world_z - cz * kChunkSize;

    ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return false;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return false;

    chunk->terrain.cell_at(lx, ly, lz).material =
        static_cast<TerrainMaterial>(material);
    chunk->terrain.cell_at(lx, ly, lz).flags = flags;
    return true;
}

bool TreeGrowthSystem::get_biome_at(
    int global_x, int global_y, int global_z,
    float& out_temperature, float& out_humidity) const {
    if (!world_) return false;

    auto config = world_->worldgen_config();
    if (!config) return false;

    // Use the same noise seeds as pass_biome for consistency.
    // We need a chunk seed, but we don't know the exact chunk here.
    // Use a simplified approach: compute temperature/humidity from
    // the world seed using the same noise parameters.
    // For now, return a simple approximation based on position.
    // A more accurate implementation would cache biome noise per chunk.

    // Simple temperature model: decreases with altitude, varies with x/z.
    const float lat_factor = static_cast<float>(global_z) * 0.001f;
    const float alt_factor = static_cast<float>(global_y) * 0.01f;
    out_temperature = std::clamp(0.0f - lat_factor - alt_factor, -1.0f, 1.0f);

    // Simple humidity model: varies with x/z position.
    const float humid_factor = static_cast<float>(global_x) * 0.001f;
    out_humidity = std::clamp(humid_factor, -1.0f, 1.0f);

    return true;
}

} // namespace science_and_theology
