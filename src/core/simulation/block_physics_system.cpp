#include "block_physics_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "event_types.hpp"
#include "../world/world_data.hpp"
#include "../world_gen/world_gen_config.hpp"

namespace science_and_theology {

namespace {

constexpr int kNeighborDeltas[][3] = {
    {0, 0, 0},
    {1, 0, 0}, {-1, 0, 0},
    {0, 1, 0}, {0, -1, 0},
    {0, 0, 1}, {0, 0, -1},
};

int floor_div_chunk(int value) {
    return static_cast<int>(
        std::floor(static_cast<float>(value) / ChunkData::kChunkSize));
}

struct ResolvedCell {
    ChunkData* chunk = nullptr;
    TerrainCell* cell = nullptr;
    int chunk_x = 0;
    int chunk_y = 0;
    int chunk_z = 0;
    int local_x = 0;
    int local_y = 0;
    int local_z = 0;
};

ResolvedCell resolve_cell(
    WorldData* world,
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) {
    ResolvedCell out;
    if (!world) return out;

    out.chunk_x = floor_div_chunk(block_x);
    out.chunk_y = floor_div_chunk(block_y);
    out.chunk_z = floor_div_chunk(block_z);
    out.local_x = block_x - out.chunk_x * ChunkData::kChunkSize;
    out.local_y = block_y - out.chunk_y * ChunkData::kChunkSize;
    out.local_z = block_z - out.chunk_z * ChunkData::kChunkSize;

    out.chunk = world->get_chunk(
        dimension_id, out.chunk_x, out.chunk_y, out.chunk_z);
    if (!out.chunk) return out;
    if (!out.chunk->terrain.is_valid_cell(
            out.local_x, out.local_y, out.local_z)) {
        out.chunk = nullptr;
        return out;
    }
    out.cell = &out.chunk->terrain.cell_at(
        out.local_x, out.local_y, out.local_z);
    return out;
}

const TerrainCell* resolve_const_cell(
    const WorldData* world,
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) {
    if (!world) return nullptr;

    const int cx = floor_div_chunk(block_x);
    const int cy = floor_div_chunk(block_y);
    const int cz = floor_div_chunk(block_z);
    const int lx = block_x - cx * ChunkData::kChunkSize;
    const int ly = block_y - cy * ChunkData::kChunkSize;
    const int lz = block_z - cz * ChunkData::kChunkSize;

    const ChunkData* chunk = world->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return nullptr;
    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return nullptr;
    return &chunk->terrain.cell_at(lx, ly, lz);
}

} // namespace

// --- SimulationSystem interface ---

void BlockPhysicsSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
    last_processed_tick_ = -1;
}

void BlockPhysicsSystem::tick_active(const ChunkKey& chunk, float delta,
                                     const TickContext* ctx) {
    (void)chunk;
    (void)delta;
    (void)ctx;
    if (!world_) return;

    // TickSystem calls tick_active once per active chunk. Block physics owns a
    // global pending queue, so process it only once per simulation tick.
    const int64_t tick = world_->current_tick();
    if (last_processed_tick_ == tick) return;
    last_processed_tick_ = tick;

    // Consume block physics events from the WorldData queue. These are
    // enqueued by the authoritative command server when blocks are mined,
    // placed, or otherwise mutated by gameplay commands.
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

void BlockPhysicsSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                       const TickContext* ctx) {
    (void)chunk;
    (void)delta;
    (void)ctx;
    // Sleeping chunks do not process block physics. Physics is intentionally
    // observer-local so distant cave-ins cannot mutate unloaded terrain.
}

void BlockPhysicsSystem::shutdown() {
    // Drain remaining pending checks.
    while (!pending_.empty()) {
        pending_.pop();
    }
    last_processed_tick_ = -1;
}

// --- Scheduled block checks ---

void BlockPhysicsSystem::schedule_check(const PendingCheck& check) {
    pending_.push(check);
}

void BlockPhysicsSystem::schedule_gravity_fall_after_mine(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int64_t current_tick,
    int chain_depth) {
    if (!world_) return;
    const auto& gc = world_->gameplay_config();
    if (!gc.is_gravity_fall_enabled(dimension_id)) return;
    if (chain_depth > gc.get_max_gravity_fall_chain(dimension_id)) return;

    // Schedule checks for the changed block itself plus the 6 neighbors.
    // The origin check is important for placement; neighbor checks are
    // important for mining/removing support. Neighbors are delayed by one
    // extra tick so a block moved into a neighbor position cannot be processed
    // again by an already-queued check in the same simulation tick.
    for (int i = 0; i < 7; ++i) {
        PendingCheck check;
        check.dimension_id = dimension_id;
        check.block_x = block_x + kNeighborDeltas[i][0];
        check.block_y = block_y + kNeighborDeltas[i][1];
        check.block_z = block_z + kNeighborDeltas[i][2];
        check.target_tick = current_tick + (i == 0 ? 1 : 2);
        check.check_type = 0;  // gravity fall
        check.chain_depth = chain_depth;
        schedule_check(check);
    }
}

void BlockPhysicsSystem::schedule_collapse_after_mine(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int64_t current_tick,
    int chain_depth) {
    if (!world_) return;
    const auto& gc = world_->gameplay_config();
    if (!gc.is_collapse_enabled(dimension_id)) return;
    if (chain_depth > gc.get_max_collapse_chain(dimension_id)) return;

    // Schedule collapse checks with staggered delays to create a cascading
    // cave-in effect without producing a one-frame spike.
    for (int i = 0; i < 7; ++i) {
        PendingCheck check;
        check.dimension_id = dimension_id;
        check.block_x = block_x + kNeighborDeltas[i][0];
        check.block_y = block_y + kNeighborDeltas[i][1];
        check.block_z = block_z + kNeighborDeltas[i][2];
        check.target_tick = current_tick + 2 + i;
        check.check_type = 1;  // collapse
        check.chain_depth = chain_depth;
        schedule_check(check);
    }
}

// --- Processing ---

void BlockPhysicsSystem::process_pending(int64_t current_tick) {
    if (!world_) return;

    int processed = 0;
    size_t scanned = pending_.size();

    // Do not let one future-dated item at the front of the queue block ready
    // items behind it. Rotate future checks to the back and process a snapshot
    // of the queue at most once per tick.
    while (!pending_.empty() && scanned > 0 && processed < kMaxChecksPerTick) {
        PendingCheck check = pending_.front();
        pending_.pop();
        --scanned;

        if (check.target_tick > current_tick) {
            pending_.push(check);
            continue;
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

        // If a block fell or collapsed, schedule more checks for its new
        // neighborhood. Chain depth caps prevent pathological cave-ins.
        if (acted) {
            const int next_depth = check.chain_depth + 1;
            schedule_gravity_fall_after_mine(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z,
                current_tick,
                next_depth);
            schedule_collapse_after_mine(
                check.dimension_id,
                check.block_x, check.block_y, check.block_z,
                current_tick,
                next_depth);
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
    // snapped to the nearest axis on the fixed global voxel lattice.
    const float dx = static_cast<float>(block_x) - planet->center_x;
    const float dy = static_cast<float>(block_y) - planet->center_y;
    const float dz = static_cast<float>(block_z) - planet->center_z;

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

    ResolvedCell src = resolve_cell(
        world_, dimension_id, block_x, block_y, block_z);
    if (!src.cell) return false;
    if (!src.cell->is_gravity_fall()) return false;

    auto config = world_->worldgen_config();
    const TerrainMaterialId air = config ? config->roles.air : 0;

    // Check if the block below (in gravity direction) is empty.
    const GravityStep gs = compute_gravity_step(
        dimension_id, block_x, block_y, block_z);
    if (gs.dx == 0 && gs.dy == 0 && gs.dz == 0) return false;

    const int below_x = block_x + gs.dx;
    const int below_y = block_y + gs.dy;
    const int below_z = block_z + gs.dz;

    ResolvedCell dst = resolve_cell(
        world_, dimension_id, below_x, below_y, below_z);
    if (!dst.cell) return false;
    if (!is_empty_cell(*dst.cell, air)) return false;

    const TerrainMaterialId moved_material =
        static_cast<TerrainMaterialId>(src.cell->material);
    const uint32_t moved_flags = src.cell->flags;
    const TerrainMaterialId dst_old_material =
        static_cast<TerrainMaterialId>(dst.cell->material);

    // Clear source and place at destination.
    src.chunk->terrain.set_cell(src.local_x, src.local_y, src.local_z, air, 0);
    dst.chunk->terrain.set_cell(
        dst.local_x, dst.local_y, dst.local_z, moved_material, moved_flags);

    emit_terrain_changed(
        dimension_id,
        src.chunk_x, src.chunk_y, src.chunk_z,
        src.local_x, src.local_y, src.local_z,
        moved_material, air);
    emit_terrain_changed(
        dimension_id,
        dst.chunk_x, dst.chunk_y, dst.chunk_z,
        dst.local_x, dst.local_y, dst.local_z,
        dst_old_material, moved_material);

    return true;
}

bool BlockPhysicsSystem::process_collapse(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z) {
    if (!world_) return false;

    const auto& gc = world_->gameplay_config();
    if (!gc.is_collapse_enabled(dimension_id)) return false;

    ResolvedCell src = resolve_cell(
        world_, dimension_id, block_x, block_y, block_z);
    if (!src.cell) return false;
    if (!src.cell->is_collapse_risk()) return false;

    // Check if a support beam is nearby.
    const int support_radius = gc.get_support_beam_radius(dimension_id);
    if (has_support_beam_nearby(dimension_id, block_x, block_y, block_z,
                                support_radius)) {
        return false;
    }

    // Check if the block has support below (in gravity direction).
    const GravityStep gs = compute_gravity_step(
        dimension_id, block_x, block_y, block_z);
    if (gs.dx == 0 && gs.dy == 0 && gs.dz == 0) return false;

    const int below_x = block_x + gs.dx;
    const int below_y = block_y + gs.dy;
    const int below_z = block_z + gs.dz;

    ResolvedCell dst = resolve_cell(world_, dimension_id, below_x, below_y, below_z);
    if (!dst.cell) return false;
    if (dst.cell->is_solid()) return false;

    auto config = world_->worldgen_config();
    const TerrainMaterialId air = config ? config->roles.air : 0;
    if (!is_empty_cell(*dst.cell, air)) return false;

    // No support: roll for collapse. Use the material's collapse_chance
    // multiplied by the runtime gameplay config multiplier.
    float base_chance = 0.3f;
    if (config) {
        const TerrainMaterialDef* mat_def =
            config->find_material(static_cast<TerrainMaterialId>(src.cell->material));
        if (mat_def) {
            base_chance = mat_def->collapse_chance;
        }
    }

    const float chance = base_chance * gc.get_collapse_chance_multiplier(dimension_id);
    const float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

    if (roll >= chance) return false;

    const TerrainMaterialId moved_material =
        static_cast<TerrainMaterialId>(src.cell->material);
    const uint32_t moved_flags = src.cell->flags;
    const TerrainMaterialId dst_old_material =
        static_cast<TerrainMaterialId>(dst.cell->material);

    // Collapse now behaves like gravity fall: the original block moves one
    // step downward instead of transforming into rubble/debris.
    src.chunk->terrain.set_cell(src.local_x, src.local_y, src.local_z, air, 0);
    dst.chunk->terrain.set_cell(
        dst.local_x, dst.local_y, dst.local_z, moved_material, moved_flags);

    emit_terrain_changed(
        dimension_id,
        src.chunk_x, src.chunk_y, src.chunk_z,
        src.local_x, src.local_y, src.local_z,
        moved_material, air);
    emit_terrain_changed(
        dimension_id,
        dst.chunk_x, dst.chunk_y, dst.chunk_z,
        dst.local_x, dst.local_y, dst.local_z,
        dst_old_material, moved_material);

    return true;
}

bool BlockPhysicsSystem::has_support_beam_nearby(
    const std::string& dimension_id,
    int block_x, int block_y, int block_z,
    int radius) const {
    if (!world_) return false;
    if (radius <= 0) return false;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                // Manhattan distance keeps the support volume cheap and
                // predictable for gameplay: diamond-shaped support envelope.
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > radius) continue;

                const TerrainCell* cell = resolve_const_cell(
                    world_, dimension_id,
                    block_x + dx, block_y + dy, block_z + dz);
                if (cell && cell->is_support_beam()) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool BlockPhysicsSystem::is_empty_cell(
    const TerrainCell& cell, TerrainMaterialId air) const {
    return static_cast<TerrainMaterialId>(cell.material) == air && !cell.has_fluid();
}

void BlockPhysicsSystem::emit_terrain_changed(
    const std::string& dimension_id,
    int chunk_x, int chunk_y, int chunk_z,
    int local_x, int local_y, int local_z,
    int old_material, int new_material) const {
    if (!event_bus_) return;
    event_bus_->enqueue(GameEvent::terrain_changed(
        dimension_id,
        chunk_x, chunk_y, chunk_z,
        local_x, local_y, local_z,
        old_material, new_material));
}

} // namespace science_and_theology
