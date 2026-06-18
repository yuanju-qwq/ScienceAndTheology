#include "block_physics_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../world/world_data.hpp"
#include "../world_gen/world_gen_config.hpp"

namespace science_and_theology {

// --- SimulationSystem interface ---

void BlockPhysicsSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void BlockPhysicsSystem::tick_active(const ChunkKey& chunk, float delta) {
    (void)chunk;
    (void)delta;
    if (!world_) return;

    // Read the current tick from WorldData (set by TickSystem each frame).
    const int64_t tick = world_->current_tick();

    // Consume block physics events from the WorldData queue.
    // These are enqueued by the command server when blocks are mined.
    BlockPhysicsEvent event;
    while (world_->pop_physics_event(event)) {
        schedule_gravity_fall_after_mine(
            event.dimension_id,
            event.block_x, event.block_y, event.block_z,
            tick);
        schedule_collapse_after_mine(
            event.dimension_id,
            event.block_x, event.block_y, event.block_z,
            tick);
    }

    process_pending(tick);
}

void BlockPhysicsSystem::tick_sleeping(const ChunkKey& chunk, float delta) {
    (void)chunk;
    (void)delta;
    // Sleeping chunks do not process block physics.
}

void BlockPhysicsSystem::shutdown() {
    // Drain remaining pending checks.
    while (!pending_.empty()) {
        pending_.pop();
    }
}

// --- Scheduled block checks ---

void BlockPhysicsSystem::schedule_check(const PendingCheck& check) {
    pending_.push(check);
}

void BlockPhysicsSystem::schedule_gravity_fall_after_mine(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int64_t current_tick) {
    if (!world_) return;
    const auto& gc = world_->gameplay_config();
    if (!gc.is_gravity_fall_enabled(dimension_id)) return;

    // Schedule checks for the 6 neighbors, with slight delays
    // so that chain reactions spread over multiple ticks.
    constexpr int kDeltas[][3] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1},
    };
    for (const auto& d : kDeltas) {
        PendingCheck check;
        check.dimension_id = dimension_id;
        check.block_x = block_x + d[0];
        check.block_y = block_y + d[1];
        check.block_z = block_z + d[2];
        check.target_tick = current_tick + 1;
        check.check_type = 0;  // gravity fall
        schedule_check(check);
    }
}

void BlockPhysicsSystem::schedule_collapse_after_mine(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int64_t current_tick) {
    if (!world_) return;
    const auto& gc = world_->gameplay_config();
    if (!gc.is_collapse_enabled(dimension_id)) return;

    // Schedule collapse checks for neighbors, with slightly longer
    // delays to create a cascading cave-in effect.
    constexpr int kDeltas[][3] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1},
    };
    for (int i = 0; i < 6; ++i) {
        PendingCheck check;
        check.dimension_id = dimension_id;
        check.block_x = block_x + kDeltas[i][0];
        check.block_y = block_y + kDeltas[i][1];
        check.block_z = block_z + kDeltas[i][2];
        check.target_tick = current_tick + 2 + i;
        check.check_type = 1;  // collapse
        schedule_check(check);
    }
}

// --- Processing ---

void BlockPhysicsSystem::process_pending(int64_t current_tick) {
    if (!world_) return;

    int processed = 0;
    while (!pending_.empty() && processed < kMaxChecksPerTick) {
        PendingCheck check = pending_.front();
        pending_.pop();

        // Skip checks that are scheduled for the future.
        if (check.target_tick > current_tick) {
            pending_.push(check);
            break;
        }

        bool acted = false;
        if (check.check_type == 0) {
            acted = process_gravity_fall(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z);
        } else if (check.check_type == 1) {
            acted = process_collapse(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z);
        }

        // If a block fell or collapsed, schedule more checks for
        // its new neighbors (chain reaction).
        if (acted) {
            schedule_gravity_fall_after_mine(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z,
                current_tick);
            schedule_collapse_after_mine(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z,
                current_tick);
        }

        ++processed;
    }
}

// --- Gravity direction ---

BlockPhysicsSystem::GravityStep BlockPhysicsSystem::compute_gravity_step(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) const {
    GravityStep step = {0, -1, 0};  // Default: flat world, gravity down.

    if (!world_) return step;

    auto config = world_->worldgen_config();
    if (!config) return step;

    const PlanetConfig* planet = config->find_planet_config(dimension_id);
    if (!planet || !planet->is_planet()) return step;

    // For spherical planet: gravity step is toward planet center,
    // snapped to the nearest axis.
    const float dx = static_cast<float>(block_x) - planet->center_x;
    const float dy = static_cast<float>(block_y) - planet->center_y;
    const float dz = static_cast<float>(block_z) - planet->center_z;

    // Find the axis with the largest absolute component.
    const float adx = std::abs(dx);
    const float ady = std::abs(dy);
    const float adz = std::abs(dz);

    step = {0, 0, 0};
    if (adx >= ady && adx >= adz) {
        step.dx = (dx > 0.0f) ? -1 : 1;
    } else if (ady >= adx && ady >= adz) {
        step.dy = (dy > 0.0f) ? -1 : 1;
    } else {
        step.dz = (dz > 0.0f) ? -1 : 1;
    }

    return step;
}

// --- Per-block physics ---

bool BlockPhysicsSystem::process_gravity_fall(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) {
    if (!world_) return false;

    const auto& gc = world_->gameplay_config();
    if (!gc.is_gravity_fall_enabled(dimension_id)) return false;

    // Convert world block position to chunk + local coordinates.
    const int cx = static_cast<int>(
        std::floor(static_cast<float>(block_x) / ChunkData::kChunkSize));
    const int cy = static_cast<int>(
        std::floor(static_cast<float>(block_y) / ChunkData::kChunkSize));
    const int cz = static_cast<int>(
        std::floor(static_cast<float>(block_z) / ChunkData::kChunkSize));
    const int lx = block_x - cx * ChunkData::kChunkSize;
    const int ly = block_y - cy * ChunkData::kChunkSize;
    const int lz = block_z - cz * ChunkData::kChunkSize;

    ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return false;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return false;

    TerrainCell& cell = chunk->terrain.cell_at(lx, ly, lz);
    if (!cell.is_gravity_fall()) return false;

    // Check if the block below (in gravity direction) is empty.
    const GravityStep gs = compute_gravity_step(
        dimension_id, block_x, block_y, block_z);
    const int below_x = block_x + gs.dx;
    const int below_y = block_y + gs.dy;
    const int below_z = block_z + gs.dz;

    const int bcx = static_cast<int>(
        std::floor(static_cast<float>(below_x) / ChunkData::kChunkSize));
    const int bcy = static_cast<int>(
        std::floor(static_cast<float>(below_y) / ChunkData::kChunkSize));
    const int bcz = static_cast<int>(
        std::floor(static_cast<float>(below_z) / ChunkData::kChunkSize));
    const int blx = below_x - bcx * ChunkData::kChunkSize;
    const int bly = below_y - bcy * ChunkData::kChunkSize;
    const int blz = below_z - bcz * ChunkData::kChunkSize;

    ChunkData* below_chunk = world_->get_chunk(dimension_id, bcx, bcy, bcz);
    if (!below_chunk) return false;
    if (!below_chunk->terrain.is_valid_cell(blx, bly, blz)) return false;

    const TerrainCell& below_cell = below_chunk->terrain.cell_at(blx, bly, blz);
    // Can fall into air or liquid.
    if (below_cell.is_solid()) return false;

    // Move the block down.
    const TerrainMaterialId moved_material =
        static_cast<TerrainMaterialId>(cell.material);
    const uint32_t moved_flags = cell.flags;

    // Clear the source.
    auto config = world_->worldgen_config();
    const TerrainMaterialId air = config ? config->roles.air : 0;
    chunk->terrain.set_cell(lx, ly, lz, air, 0);

    // Place at the destination.
    below_chunk->terrain.set_cell(blx, bly, blz, moved_material, moved_flags);

    return true;
}

bool BlockPhysicsSystem::process_collapse(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) {
    if (!world_) return false;

    const auto& gc = world_->gameplay_config();
    if (!gc.is_collapse_enabled(dimension_id)) return false;

    // Convert world block position to chunk + local coordinates.
    const int cx = static_cast<int>(
        std::floor(static_cast<float>(block_x) / ChunkData::kChunkSize));
    const int cy = static_cast<int>(
        std::floor(static_cast<float>(block_y) / ChunkData::kChunkSize));
    const int cz = static_cast<int>(
        std::floor(static_cast<float>(block_z) / ChunkData::kChunkSize));
    const int lx = block_x - cx * ChunkData::kChunkSize;
    const int ly = block_y - cy * ChunkData::kChunkSize;
    const int lz = block_z - cz * ChunkData::kChunkSize;

    ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return false;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return false;

    TerrainCell& cell = chunk->terrain.cell_at(lx, ly, lz);
    if (!cell.is_collapse_risk()) return false;

    // Check if a support beam is nearby.
    const int support_radius = gc.get_support_beam_radius(dimension_id);
    if (has_support_beam_nearby(dimension_id, block_x, block_y, block_z,
                                support_radius)) {
        return false;
    }

    // Check if the block has support below (in gravity direction).
    const GravityStep gs = compute_gravity_step(
        dimension_id, block_x, block_y, block_z);
    const int below_x = block_x + gs.dx;
    const int below_y = block_y + gs.dy;
    const int below_z = block_z + gs.dz;

    const int bcx = static_cast<int>(
        std::floor(static_cast<float>(below_x) / ChunkData::kChunkSize));
    const int bcy = static_cast<int>(
        std::floor(static_cast<float>(below_y) / ChunkData::kChunkSize));
    const int bcz = static_cast<int>(
        std::floor(static_cast<float>(below_z) / ChunkData::kChunkSize));
    const int blx = below_x - bcx * ChunkData::kChunkSize;
    const int bly = below_y - bcy * ChunkData::kChunkSize;
    const int blz = below_z - bcz * ChunkData::kChunkSize;

    ChunkData* below_chunk = world_->get_chunk(dimension_id, bcx, bcy, bcz);
    if (below_chunk && below_chunk->terrain.is_valid_cell(blx, bly, blz)) {
        const TerrainCell& below_cell =
            below_chunk->terrain.cell_at(blx, bly, blz);
        if (below_cell.is_solid()) {
            // Has solid support below: no collapse.
            return false;
        }
    }

    // No support: roll for collapse.
    // Use the material's collapse_chance multiplied by the config multiplier.
    float base_chance = 0.3f;
    auto wg_config = world_->worldgen_config();
    if (wg_config) {
        const TerrainMaterialDef* mat_def =
            wg_config->find_material(static_cast<TerrainMaterialId>(cell.material));
        if (mat_def) {
            base_chance = mat_def->collapse_chance;
        }
    }

    const float chance = base_chance * gc.get_collapse_chance_multiplier(dimension_id);
    const float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

    if (roll >= chance) return false;

    // Collapse: convert the block to air (it falls as debris).
    auto config = world_->worldgen_config();
    const TerrainMaterialId air = config ? config->roles.air : 0;
    chunk->terrain.set_cell(lx, ly, lz, air, 0);

    return true;
}

bool BlockPhysicsSystem::has_support_beam_nearby(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int radius) const {
    if (!world_) return false;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                // Manhattan distance check for efficiency.
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > radius) continue;

                const int nx = block_x + dx;
                const int ny = block_y + dy;
                const int nz = block_z + dz;

                const int ncx = static_cast<int>(
                    std::floor(static_cast<float>(nx) / ChunkData::kChunkSize));
                const int ncy = static_cast<int>(
                    std::floor(static_cast<float>(ny) / ChunkData::kChunkSize));
                const int ncz = static_cast<int>(
                    std::floor(static_cast<float>(nz) / ChunkData::kChunkSize));
                const int nlx = nx - ncx * ChunkData::kChunkSize;
                const int nly = ny - ncy * ChunkData::kChunkSize;
                const int nlz = nz - ncz * ChunkData::kChunkSize;

                const ChunkData* nchunk =
                    world_->get_chunk(dimension_id, ncx, ncy, ncz);
                if (!nchunk) continue;
                if (!nchunk->terrain.is_valid_cell(nlx, nly, nlz)) continue;

                if (nchunk->terrain.cell_at(nlx, nly, nlz).is_support_beam()) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace science_and_theology
