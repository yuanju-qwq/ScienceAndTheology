#include "tile_fluid_system.hpp"

#include <algorithm>
#include <cmath>

#include "../world/world_data.hpp"

namespace science_and_theology {

// ============================================================
// SimulationSystem interface
// ============================================================

void TileFluidSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;
}

void TileFluidSystem::tick_active(const ChunkKey& chunk, float delta) {
    if (!world_) return;

    // Phase 1: Process gravity flow for active cells in this chunk.
    process_gravity_flow();

    // Phase 2: Process pressure equalization.
    process_pressure_equalization();

    // Phase 3: Detect sleeping cells.
    detect_sleeping_cells();

    // Phase 4: Update fluid region aggregate data.
    update_fluid_regions(chunk);
}

void TileFluidSystem::tick_sleeping(const ChunkKey& chunk, float delta) {
    if (!world_) return;

    // Sleeping chunks: only update region aggregate data.
    // No per-cell simulation for sleeping chunks.
    update_fluid_regions(chunk);
}

void TileFluidSystem::shutdown() {
    active_cells_.clear();
    idle_counters_.clear();
    fluid_regions_.clear();
    world_ = nullptr;
    event_bus_ = nullptr;
}

// ============================================================
// Active cell management
// ============================================================

void TileFluidSystem::wake_cell(const std::string& dimension_id,
                                 int32_t x, int32_t y, int32_t z) {
    FluidCellKey key = make_key(dimension_id, x, y, z);
    if (active_cells_.insert(key).second) {
        // Newly woken cell: reset idle counter.
        idle_counters_[key] = 0;
    }
}

void TileFluidSystem::sleep_cell(const std::string& dimension_id,
                                  int32_t x, int32_t y, int32_t z) {
    FluidCellKey key = make_key(dimension_id, x, y, z);
    active_cells_.erase(key);
    idle_counters_.erase(key);
}

// ============================================================
// Fluid displacement
// ============================================================

void TileFluidSystem::displace_fluid(const std::string& dimension_id,
                                      int32_t x, int32_t y, int32_t z) {
    TerrainCell* cell = get_terrain_cell(dimension_id, x, y, z);
    if (cell == nullptr || !cell->has_fluid()) return;

    CellFluidId fluid = cell->fluid_type;
    int16_t mass_to_displace = cell->fluid_mass;

    // Clear the fluid from this cell.
    cell->clear_fluid();

    // Try to move fluid to adjacent cells.
    // Priority: gravity direction first, then horizontal, then up.
    GravityStep gs = compute_gravity_step(dimension_id, x, y, z);

    // 6-connected neighbor offsets: gravity dir, 4 horizontal, anti-gravity.
    struct Offset { int32_t dx, dy, dz; };
    Offset offsets[6] = {
        {gs.dx, gs.dy, gs.dz},                            // gravity dir
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},   // horizontal
        {-gs.dx, -gs.dy, -gs.dz},                          // anti-gravity
    };

    for (int i = 0; i < 6 && mass_to_displace > 0; ++i) {
        int32_t nx = x + offsets[i].dx;
        int32_t ny = y + offsets[i].dy;
        int32_t nz = z + offsets[i].dz;

        TerrainCell* neighbor = get_terrain_cell(dimension_id, nx, ny, nz);
        if (neighbor == nullptr) continue;
        if (neighbor->is_solid()) continue;

        int16_t inserted = neighbor->insert_fluid(fluid, mass_to_displace);
        if (inserted > 0) {
            mass_to_displace -= inserted;
            // Wake the neighbor cell since it received fluid.
            wake_cell(dimension_id, nx, ny, nz);
        }
    }

    // If any fluid could not be displaced, it is destroyed.
    // This should be rare and indicates a fully enclosed space.
    // In the future, we could emit an event for logging.
}

// ============================================================
// Fluid region data access
// ============================================================

const FluidRegionData* TileFluidSystem::get_fluid_region_data(
        uint64_t region_id) const {
    auto it = fluid_regions_.find(region_id);
    return it != fluid_regions_.end() ? &it->second : nullptr;
}

FluidRegionData* TileFluidSystem::get_fluid_region_data_mut(
        uint64_t region_id) {
    auto it = fluid_regions_.find(region_id);
    return it != fluid_regions_.end() ? &it->second : nullptr;
}

// ============================================================
// Per-tick simulation steps (stubs for Step 3-5 of the plan)
// ============================================================

void TileFluidSystem::process_gravity_flow() {
    // TODO: Step 3 — implement gravity flow.
    // For each active cell, move fluid in the gravity direction
    // if the cell below is not full and not solid.
}

void TileFluidSystem::process_pressure_equalization() {
    // TODO: Step 4 — implement pressure equalization.
    // For each active cell, equalize fluid mass with horizontal
    // neighbors (water surface tends to be flat).
}

void TileFluidSystem::detect_sleeping_cells() {
    // TODO: Step 5 — implement sleep detection.
    // A cell goes to sleep when:
    //   - Its fluid mass equals all horizontal neighbors (within epsilon).
    //   - The cell below is either solid, full, or has more fluid.
    //   - The idle counter reaches kSleepThreshold.
}

void TileFluidSystem::update_fluid_regions(const ChunkKey& chunk) {
    // TODO: Step 1 (partial) — update fluid region aggregate data.
    // Count total fluid mass and cell count in each region,
    // update FluidRegionData, and detect equilibrium.
}

// ============================================================
// Helpers
// ============================================================

TileFluidSystem::FluidCellKey TileFluidSystem::make_key(
        const std::string& dim, int32_t x, int32_t y, int32_t z) const {
    FluidCellKey key;
    key.dimension_id = dim;
    key.x = x;
    key.y = y;
    key.z = z;
    return key;
}

TerrainCell* TileFluidSystem::get_terrain_cell(
        const std::string& dimension_id, int32_t x, int32_t y, int32_t z) {
    if (!world_) return nullptr;

    int cx = static_cast<int>(
        std::floor(static_cast<float>(x) / ChunkData::kChunkSize));
    int cy = static_cast<int>(
        std::floor(static_cast<float>(y) / ChunkData::kChunkSize));
    int cz = static_cast<int>(
        std::floor(static_cast<float>(z) / ChunkData::kChunkSize));

    ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return nullptr;

    int lx = x - cx * ChunkData::kChunkSize;
    int ly = y - cy * ChunkData::kChunkSize;
    int lz = z - cz * ChunkData::kChunkSize;

    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return nullptr;
    return &chunk->terrain.cell_at(lx, ly, lz);
}

const TerrainCell* TileFluidSystem::get_terrain_cell_const(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) const {
    if (!world_) return nullptr;

    int cx = static_cast<int>(
        std::floor(static_cast<float>(x) / ChunkData::kChunkSize));
    int cy = static_cast<int>(
        std::floor(static_cast<float>(y) / ChunkData::kChunkSize));
    int cz = static_cast<int>(
        std::floor(static_cast<float>(z) / ChunkData::kChunkSize));

    const ChunkData* chunk = world_->get_chunk(dimension_id, cx, cy, cz);
    if (!chunk) return nullptr;

    int lx = x - cx * ChunkData::kChunkSize;
    int ly = y - cy * ChunkData::kChunkSize;
    int lz = z - cz * ChunkData::kChunkSize;

    if (!chunk->terrain.is_valid_cell(lx, ly, lz)) return nullptr;
    return &chunk->terrain.cell_at(lx, ly, lz);
}

TileFluidSystem::GravityStep TileFluidSystem::compute_gravity_step(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) const {
    (void)dimension_id;
    (void)x;
    (void)y;
    (void)z;

    // Default: flat world gravity points down (Y-).
    // Planet gravity should compute the direction toward the planet center.
    // TODO: integrate with WorldGenConfig planet data.
    return {0, -1, 0};
}

} // namespace science_and_theology
