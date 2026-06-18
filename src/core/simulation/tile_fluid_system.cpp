#include "tile_fluid_system.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

#include "../world/world_data.hpp"

namespace science_and_theology {

// ============================================================
// SimulationSystem interface
// ============================================================

void TileFluidSystem::initialize(WorldData* world, EventBus* bus) {
    world_ = world;
    event_bus_ = bus;

    // Cache water fluid ID.
    cached_water_id_ = static_cast<CellFluidId>(
        gt::FluidRegistry::get_fluid_id("water"));

    // Cache set of fluid types that have phase transitions.
    phase_transition_fluids_.clear();
    size_t count = gt::FluidRegistry::get_fluid_count();
    for (size_t i = 1; i <= count; ++i) {
        const gt::FluidDefinition* def =
            gt::FluidRegistry::get_fluid(static_cast<gt::FluidId>(i));
        if (def == nullptr) continue;
        if (def->evaporation_temp > 0 ||
            def->condensation_temp > 0) {
            phase_transition_fluids_.insert(static_cast<CellFluidId>(i));
        }
    }
}

void TileFluidSystem::tick_active(const ChunkKey& chunk, float delta,
                                  const TickContext* ctx) {
    if (!world_) return;

    // Fluid simulation runs once per game tick, not once per chunk.
    // When multiple active chunks call tick_active in the same frame,
    // only the first invocation runs the simulation.
    int64_t current_tick = world_->current_tick();
    if (last_sim_tick_ == current_tick) return;
    last_sim_tick_ = current_tick;

    // Phase 1: Process gravity flow for liquid cells.
    process_gravity_flow();

    // Phase 2: Process pressure equalization for liquid cells.
    process_pressure_equalization();

    // Phase 2b: Process gas diffusion for gas cells.
    process_gas_diffusion();

    // Phase 3: Process thermal conduction.
    process_thermal_conduction();

    // Phase 4: Process phase transitions.
    process_phase_transitions();

    // Phase 5: Process water cycle (evaporation + rainfall).
    process_evaporation();

    // Rainfall runs at reduced frequency for performance.
    if (current_tick % kRainfallInterval == 0) {
        process_rainfall(chunk);
    }

    // Phase 6: Detect sleeping cells.
    detect_sleeping_cells();

    // Phase 7: Update fluid region aggregate data.
    update_fluid_regions(chunk);
}

void TileFluidSystem::tick_sleeping(const ChunkKey& chunk, float delta,
                                    const TickContext* ctx) {
    if (!world_) return;

    // Sleeping chunks: only update region aggregate data.
    // No per-cell simulation for sleeping chunks.
    update_fluid_regions(chunk);
}

void TileFluidSystem::shutdown() {
    active_cells_.clear();
    idle_counters_.clear();
    fluid_regions_.clear();
    cell_to_region_.clear();
    ports_.clear();
    atmosphere_configs_.clear();
    humidity_levels_.clear();
    phase_transition_fluids_.clear();
    cached_water_id_ = kInvalidCellFluidId;
    next_region_id_ = 1;
    last_sim_tick_ = -1;
    world_ = nullptr;
    event_bus_ = nullptr;
}

// ============================================================
// Active cell management
// ============================================================

void TileFluidSystem::wake_cell(const std::string& dimension_id,
                                 int32_t x, int32_t y, int32_t z) {
    FluidCellKey key = make_key(dimension_id, x, y, z);

    // If this cell belongs to an equilibrium region, wake the
    // entire region back to per-cell simulation first.
    auto rit = cell_to_region_.find(key);
    if (rit != cell_to_region_.end()) {
        uint64_t region_id = rit->second;
        wake_equilibrium_region(region_id);
        return;
    }

    if (active_cells_.insert(key).second) {
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
    bool is_gas = cell->fluid_is_gas;

    cell->clear_fluid();

    GravityStep gs = compute_gravity_step(dimension_id, x, y, z);

    struct Offset { int32_t dx, dy, dz; };
    Offset offsets[6] = {
        {gs.dx, gs.dy, gs.dz},
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1},
        {-gs.dx, -gs.dy, -gs.dz},
    };

    for (int i = 0; i < 6 && mass_to_displace > 0; ++i) {
        int32_t nx = x + offsets[i].dx;
        int32_t ny = y + offsets[i].dy;
        int32_t nz = z + offsets[i].dz;

        TerrainCell* neighbor = get_terrain_cell(dimension_id, nx, ny, nz);
        if (neighbor == nullptr) continue;
        if (neighbor->is_solid()) continue;

        int16_t inserted = neighbor->insert_fluid(fluid, mass_to_displace, is_gas);
        if (inserted > 0) {
            mass_to_displace -= inserted;
            wake_cell(dimension_id, nx, ny, nz);
        }
    }
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
// Gravity flow
// ============================================================
//
// For each active cell, move fluid in the gravity direction.
// The cell below receives as much as it can (up to capacity).
// This simulates water falling down.
//
// Processing order: lowest Y first (bottom-up) so that water
// fills from the bottom before upper cells try to flow down.

void TileFluidSystem::process_gravity_flow() {
    if (active_cells_.empty()) return;

    // Snapshot active cells into a sorted vector.
    // Sort by Y ascending (bottom first) for correct fill order.
    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());
    std::sort(cells.begin(), cells.end(),
        [](const FluidCellKey& a, const FluidCellKey& b) {
            return a.y < b.y;
        });

    int budget = kMaxActiveCellsPerTick;

    for (const auto& key : cells) {
        if (budget <= 0) break;

        TerrainCell* cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        // Skip gas cells — gas does not fall by gravity.
        if (cell->fluid_is_gas) continue;

        GravityStep gs = compute_gravity_step(
            key.dimension_id, key.x, key.y, key.z);

        int32_t below_x = key.x + gs.dx;
        int32_t below_y = key.y + gs.dy;
        int32_t below_z = key.z + gs.dz;

        TerrainCell* below = get_terrain_cell(
            key.dimension_id, below_x, below_y, below_z);

        if (below == nullptr || below->is_solid()) continue;

        // If the cell below has a different fluid type, skip.
        if (below->has_fluid() && below->fluid_type != cell->fluid_type) {
            continue;
        }

        // If the cell below is already full, skip.
        if (below->fluid_is_full()) continue;

        // Transfer fluid: move as much as the cell below can accept.
        int16_t space = below->fluid_remaining_space();
        int16_t to_move = (cell->fluid_mass < space)
            ? cell->fluid_mass : space;

        if (to_move <= 0) continue;

        CellFluidId fluid = cell->fluid_type;
        int16_t moved = below->insert_fluid(fluid, to_move, false);
        cell->extract_fluid(moved);

        if (moved > 0) {
            wake_cell(key.dimension_id, below_x, below_y, below_z);
            --budget;
        }
    }
}

// ============================================================
// Pressure equalization
// ============================================================
//
// After gravity flow, fluid spreads horizontally to equalize
// pressure. For each active cell, if it has more fluid than
// a horizontal neighbor, transfer some fluid to equalize.
//
// This creates the "water spreads out on flat ground" behavior
// and the "water surface is level" effect over time.
//
// Transfer rule: move half the difference (rounded down) from
// the higher cell to the lower cell. This converges quickly
// without oscillating.

void TileFluidSystem::process_pressure_equalization() {
    if (active_cells_.empty()) return;

    // Snapshot active cells to avoid iterator invalidation.
    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());

    int budget = kMaxActiveCellsPerTick;

    // Horizontal neighbor offsets (4-connected).
    static constexpr int32_t kDx[] = {1, -1, 0, 0};
    static constexpr int32_t kDz[] = {0, 0, 1, -1};

    for (const auto& key : cells) {
        if (budget <= 0) break;

        TerrainCell* cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        // Skip gas cells — gas uses process_gas_diffusion instead.
        if (cell->fluid_is_gas) continue;

        // Skip cells with too little mass to spread horizontally.
        if (cell->fluid_mass < kMinHorizontalFlowMass) continue;

        for (int d = 0; d < 4; ++d) {
            int32_t nx = key.x + kDx[d];
            int32_t nz = key.z + kDz[d];

            TerrainCell* neighbor = get_terrain_cell(
                key.dimension_id, nx, key.y, nz);
            if (neighbor == nullptr) continue;
            if (neighbor->is_solid()) continue;

            // Skip if different fluid type.
            if (neighbor->has_fluid() &&
                neighbor->fluid_type != cell->fluid_type) {
                continue;
            }

            // If neighbor has no fluid, seed it with the same type
            // so insert_fluid works (mass=0 with valid type is okay
            // for the transfer, but we use insert_fluid which checks
            // fluid_type consistency).
            if (!neighbor->has_fluid()) {
                neighbor->fluid_type = cell->fluid_type;
                neighbor->fluid_is_gas = false;
            }

            int16_t diff = cell->fluid_mass - neighbor->fluid_mass;
            if (diff <= kPressureEpsilon) continue;

            // Surface tension: skip if the difference is too small
            // relative to the cell's mass. This prevents micro-
            // transfers between nearly-equal cells.
            int16_t min_diff = cell->fluid_mass / kSurfaceTensionDivisor;
            if (min_diff < kPressureEpsilon) min_diff = kPressureEpsilon;
            if (diff < min_diff) continue;

            // Transfer half the difference (rounded down).
            int16_t to_move = diff / 2;
            if (to_move <= 0) continue;

            // Clamp to remaining space in neighbor.
            int16_t space = neighbor->fluid_remaining_space();
            if (to_move > space) to_move = space;
            if (to_move <= 0) continue;

            int16_t moved = neighbor->insert_fluid(cell->fluid_type, to_move, false);
            cell->extract_fluid(moved);

            if (moved > 0) {
                wake_cell(key.dimension_id, nx, key.y, nz);
                --budget;
            }
        }
    }
}

// ============================================================
// Gas diffusion
// ============================================================
//
// Gas diffuses equally in all 6 directions (no gravity bias).
// For each active gas cell, equalize mass with all 6-connected
// neighbors. Transfer rule: move half the difference (rounded
// down) from the higher cell to the lower cell, same as liquid
// pressure equalization but in 3D.

void TileFluidSystem::process_gas_diffusion() {
    if (active_cells_.empty()) return;

    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());

    int budget = kMaxActiveCellsPerTick;

    // 6-connected neighbor offsets.
    static constexpr int32_t kDx[] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t kDy[] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t kDz[] = {0, 0, 0, 0, 1, -1};

    for (const auto& key : cells) {
        if (budget <= 0) break;

        TerrainCell* cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        // Only process gas cells here.
        if (!cell->fluid_is_gas) continue;

        for (int d = 0; d < 6; ++d) {
            int32_t nx = key.x + kDx[d];
            int32_t ny = key.y + kDy[d];
            int32_t nz = key.z + kDz[d];

            TerrainCell* neighbor = get_terrain_cell(
                key.dimension_id, nx, ny, nz);
            if (neighbor == nullptr) continue;
            if (neighbor->is_solid()) continue;

            // Skip if different fluid type.
            if (neighbor->has_fluid() &&
                neighbor->fluid_type != cell->fluid_type) {
                continue;
            }

            // Seed empty neighbor with the same fluid type.
            if (!neighbor->has_fluid()) {
                neighbor->fluid_type = cell->fluid_type;
                neighbor->fluid_is_gas = true;
            }

            int16_t diff = cell->fluid_mass - neighbor->fluid_mass;
            if (diff <= kPressureEpsilon) continue;

            // Transfer half the difference.
            int16_t to_move = diff / 2;
            if (to_move <= 0) continue;

            int16_t space = neighbor->fluid_remaining_space();
            if (to_move > space) to_move = space;
            if (to_move <= 0) continue;

            int16_t moved = neighbor->insert_fluid(
                cell->fluid_type, to_move, true);
            cell->extract_fluid(moved);

            if (moved > 0) {
                wake_cell(key.dimension_id, nx, ny, nz);
                --budget;
            }
        }
    }
}

// ============================================================
// Sleep detection
// ============================================================
//
// A cell goes to sleep when:
//   1. The cell below is solid, full, or has >= fluid_mass.
//   2. All horizontal neighbors have fluid_mass within epsilon
//      of this cell's mass (or are solid).
//   3. The idle counter reaches kSleepThreshold.
//
// Sleeping cells are removed from the active set and will not
// be simulated until woken by a disturbance.

void TileFluidSystem::detect_sleeping_cells() {
    if (active_cells_.empty()) return;

    std::vector<FluidCellKey> to_sleep;

    for (const auto& key : active_cells_) {
        const TerrainCell* cell = get_terrain_cell_const(
            key.dimension_id, key.x, key.y, key.z);

        // Cell no longer has fluid — it can sleep.
        if (cell == nullptr || !cell->has_fluid()) {
            to_sleep.push_back(key);
            continue;
        }

        bool is_stable = true;

        if (cell->fluid_is_gas) {
            // Gas stability: all 6 neighbors must be within epsilon
            // or solid. No gravity check for gas.
            static constexpr int32_t kDx[] = {1, -1, 0, 0, 0, 0};
            static constexpr int32_t kDy[] = {0, 0, 1, -1, 0, 0};
            static constexpr int32_t kDz[] = {0, 0, 0, 0, 1, -1};

            for (int d = 0; d < 6 && is_stable; ++d) {
                const TerrainCell* nb = get_terrain_cell_const(
                    key.dimension_id,
                    key.x + kDx[d], key.y + kDy[d], key.z + kDz[d]);

                if (nb == nullptr || nb->is_solid()) continue;

                if (nb->has_fluid() && nb->fluid_type == cell->fluid_type) {
                    int16_t diff = cell->fluid_mass - nb->fluid_mass;
                    if (diff < 0) diff = -diff;
                    if (diff > kPressureEpsilon) {
                        is_stable = false;
                    }
                } else if (!nb->has_fluid()) {
                    if (cell->fluid_mass > kPressureEpsilon) {
                        is_stable = false;
                    }
                }
            }
        } else {
            // Liquid stability: check gravity + horizontal.

            // Check 1: the cell below must be solid, full, or have more fluid.
            GravityStep gs = compute_gravity_step(
                key.dimension_id, key.x, key.y, key.z);
            const TerrainCell* below = get_terrain_cell_const(
                key.dimension_id,
                key.x + gs.dx, key.y + gs.dy, key.z + gs.dz);

            if (below != nullptr && !below->is_solid()) {
                if (below->has_fluid() && below->fluid_type == cell->fluid_type) {
                    if (!below->fluid_is_full() && below->fluid_mass < cell->fluid_mass) {
                        is_stable = false;
                    }
                } else if (!below->has_fluid()) {
                    is_stable = false;
                }
            }

            // Check 2: horizontal neighbors must be within epsilon.
            if (is_stable) {
                static constexpr int32_t kDx[] = {1, -1, 0, 0};
                static constexpr int32_t kDz[] = {0, 0, 1, -1};

                for (int d = 0; d < 4 && is_stable; ++d) {
                    const TerrainCell* nb = get_terrain_cell_const(
                        key.dimension_id,
                        key.x + kDx[d], key.y, key.z + kDz[d]);

                    if (nb == nullptr || nb->is_solid()) continue;

                    if (nb->has_fluid() && nb->fluid_type == cell->fluid_type) {
                        int16_t diff = cell->fluid_mass - nb->fluid_mass;
                        if (diff < 0) diff = -diff;
                        if (diff > kPressureEpsilon) {
                            is_stable = false;
                        }
                    } else if (!nb->has_fluid()) {
                        if (cell->fluid_mass > kPressureEpsilon) {
                            is_stable = false;
                        }
                    }
                }
            }
        }

        // Update idle counter.
        auto it = idle_counters_.find(key);
        if (it != idle_counters_.end()) {
            if (is_stable) {
                ++it->second;
                if (it->second >= kSleepThreshold) {
                    to_sleep.push_back(key);
                }
            } else {
                it->second = 0;
            }
        }
    }

    // Put cells to sleep.
    for (const auto& key : to_sleep) {
        active_cells_.erase(key);
        idle_counters_.erase(key);
    }
}

// ============================================================
// Fluid region update — equilibrium detection
// ============================================================
//
// After cells go to sleep, check if any sleeping cell can be
// promoted to an equilibrium region. An equilibrium region is
// a connected component of sleeping cells with the same fluid
// type where the water surface is flat.
//
// When promoted, the region is tracked with FluidRegionData
// and all its cells are mapped in cell_to_region_. The region
// costs O(1) per tick (no per-cell simulation).

void TileFluidSystem::update_fluid_regions(const ChunkKey& chunk) {
    if (!world_) return;

    // Collect sleeping cells in this chunk that are not yet in
    // an equilibrium region. These are candidates for promotion.
    int32_t cx = chunk.chunk_x;
    int32_t cy = chunk.chunk_y;
    int32_t cz = chunk.chunk_z;

    for (int ly = 0; ly < ChunkData::kChunkSize; ++ly) {
        for (int lz = 0; lz < ChunkData::kChunkSize; ++lz) {
            for (int lx = 0; lx < ChunkData::kChunkSize; ++lx) {
                int32_t wx = cx * ChunkData::kChunkSize + lx;
                int32_t wy = cy * ChunkData::kChunkSize + ly;
                int32_t wz = cz * ChunkData::kChunkSize + lz;

                const TerrainCell* cell = get_terrain_cell_const(
                    chunk.dimension_id, wx, wy, wz);
                if (cell == nullptr || !cell->has_fluid()) continue;

                FluidCellKey key = make_key(
                    chunk.dimension_id, wx, wy, wz);

                // Skip if already active or already in an equilibrium region.
                if (active_cells_.count(key) > 0) continue;
                if (cell_to_region_.count(key) > 0) continue;

                // Try to promote this sleeping cell to an equilibrium region.
                try_promote_equilibrium(key);
            }
        }
    }
}

// ============================================================
// BFS fluid component
// ============================================================
//
// BFS from a seed cell to discover all connected fluid cells
// of the same type. Only traverses non-solid, non-active cells
// that have the same fluid_type as the seed.

std::unordered_set<FluidCellKey> TileFluidSystem::bfs_fluid_component(
        const FluidCellKey& seed) const {
    std::unordered_set<FluidCellKey> visited;
    const TerrainCell* seed_cell = get_terrain_cell_const(
        seed.dimension_id, seed.x, seed.y, seed.z);
    if (seed_cell == nullptr || !seed_cell->has_fluid()) return visited;

    CellFluidId fluid_type = seed_cell->fluid_type;

    std::queue<FluidCellKey> q;
    q.push(seed);
    visited.insert(seed);

    // 6-connected neighbor offsets.
    static constexpr int32_t kDx[] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t kDy[] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t kDz[] = {0, 0, 0, 0, 1, -1};

    while (!q.empty()) {
        FluidCellKey current = q.front();
        q.pop();

        for (int d = 0; d < 6; ++d) {
            FluidCellKey nb;
            nb.dimension_id = current.dimension_id;
            nb.x = current.x + kDx[d];
            nb.y = current.y + kDy[d];
            nb.z = current.z + kDz[d];

            if (visited.count(nb) > 0) continue;

            // Skip if active (still simulating).
            if (active_cells_.count(nb) > 0) continue;

            const TerrainCell* nb_cell = get_terrain_cell_const(
                nb.dimension_id, nb.x, nb.y, nb.z);
            if (nb_cell == nullptr) continue;
            if (nb_cell->is_solid()) continue;
            if (!nb_cell->has_fluid()) continue;
            if (nb_cell->fluid_type != fluid_type) continue;

            visited.insert(nb);
            q.push(nb);
        }
    }

    return visited;
}

// ============================================================
// Try promote equilibrium
// ============================================================
//
// Check if a connected component of sleeping fluid cells
// qualifies as an equilibrium region. Qualification:
//   - All cells are sleeping (not active).
//   - All cells have the same fluid type.
//   - The component has at least kMinCellsForEquilibrium cells.
//
// If qualified, create a FluidRegionData and register all cells.

void TileFluidSystem::try_promote_equilibrium(const FluidCellKey& seed) {
    // Minimum number of cells to form an equilibrium region.
    // Small bodies (1-3 cells) are cheap to simulate per-cell.
    static constexpr size_t kMinCellsForEquilibrium = 4;

    auto component = bfs_fluid_component(seed);
    if (component.size() < kMinCellsForEquilibrium) return;

    // Create a new equilibrium region.
    uint64_t region_id = next_region_id_++;
    FluidRegionData& data = fluid_regions_[region_id];
    data.is_equilibrium = true;

    compute_region_stats(component, data);

    // Register all cells in the mapping.
    for (const auto& key : component) {
        cell_to_region_[key] = region_id;
    }
}

// ============================================================
// Wake equilibrium region
// ============================================================
//
// Transition an equilibrium region back to per-cell simulation.
// All cells in the region are woken and the region data is
// removed.

void TileFluidSystem::wake_equilibrium_region(uint64_t region_id) {
    auto it = fluid_regions_.find(region_id);
    if (it == fluid_regions_.end()) return;

    // Collect all cells belonging to this region.
    std::vector<FluidCellKey> region_cells;
    for (const auto& [key, rid] : cell_to_region_) {
        if (rid == region_id) {
            region_cells.push_back(key);
        }
    }

    // Wake each cell and remove from the region mapping.
    for (const auto& key : region_cells) {
        cell_to_region_.erase(key);
        // Add to active set (not via wake_cell to avoid recursion).
        active_cells_.insert(key);
        idle_counters_[key] = 0;
    }

    // Remove the region data.
    fluid_regions_.erase(it);
}

// ============================================================
// Compute region stats
// ============================================================
//
// Compute water_level_y, total_mass, fluid_cell_count, and
// avg_temperature for a set of fluid cells.

void TileFluidSystem::compute_region_stats(
        const std::unordered_set<FluidCellKey>& cells,
        FluidRegionData& out) const {
    if (cells.empty()) return;

    int64_t total_mass = 0;
    int32_t min_y = INT32_MAX;
    int32_t max_y = INT32_MIN;
    int64_t temp_sum = 0;
    CellFluidId fluid_type = kInvalidCellFluidId;

    for (const auto& key : cells) {
        const TerrainCell* cell = get_terrain_cell_const(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        if (fluid_type == kInvalidCellFluidId) {
            fluid_type = cell->fluid_type;
        }

        total_mass += cell->fluid_mass;
        if (key.y < min_y) min_y = key.y;
        if (key.y > max_y) max_y = key.y;
        temp_sum += cell->fluid_temperature;
    }

    out.fluid_type = fluid_type;
    out.total_mass = total_mass;
    out.fluid_cell_count = cells.size();

    // Compute water_level_y: the highest Y that contains fluid.
    // For a fully settled body, this is max_y.
    // The partial-fill row is at max_y (the surface).
    out.water_level_y = max_y;

    if (!cells.empty()) {
        out.avg_temperature = static_cast<int16_t>(
            temp_sum / static_cast<int64_t>(cells.size()));
    }
}

// ============================================================
// Equilibrium region query
// ============================================================

uint64_t TileFluidSystem::get_cell_equilibrium_region(
        const FluidCellKey& key) const {
    auto it = cell_to_region_.find(key);
    return it != cell_to_region_.end() ? it->second : 0;
}

// ============================================================
// Fluid port management (pipe ↔ tile interface)
// ============================================================

void TileFluidSystem::register_port(const std::string& dimension_id,
                                     int32_t x, int32_t y, int32_t z,
                                     int32_t max_transfer_rate,
                                     bool is_inject,
                                     bool is_extract) {
    FluidCellKey key = make_key(dimension_id, x, y, z);
    FluidPort port;
    port.position = key;
    port.max_transfer_rate = max_transfer_rate;
    port.is_inject = is_inject;
    port.is_extract = is_extract;
    ports_[key] = port;
}

void TileFluidSystem::unregister_port(const std::string& dimension_id,
                                       int32_t x, int32_t y, int32_t z) {
    FluidCellKey key = make_key(dimension_id, x, y, z);
    ports_.erase(key);
}

int16_t TileFluidSystem::inject_fluid(const std::string& dimension_id,
                                       int32_t x, int32_t y, int32_t z,
                                       CellFluidId fluid, int16_t amount,
                                       bool is_gas) {
    if (amount <= 0) return 0;

    TerrainCell* cell = get_terrain_cell(dimension_id, x, y, z);
    if (cell == nullptr) return 0;
    if (cell->is_solid()) return 0;

    // If the cell has a different fluid type, reject.
    if (cell->has_fluid() && cell->fluid_type != fluid) return 0;

    int16_t inserted = cell->insert_fluid(fluid, amount, is_gas);
    if (inserted > 0) {
        wake_cell(dimension_id, x, y, z);
    }
    return inserted;
}

int16_t TileFluidSystem::extract_fluid_at(const std::string& dimension_id,
                                           int32_t x, int32_t y, int32_t z,
                                           int16_t amount) {
    if (amount <= 0) return 0;

    TerrainCell* cell = get_terrain_cell(dimension_id, x, y, z);
    if (cell == nullptr) return 0;
    if (!cell->has_fluid()) return 0;

    int16_t extracted = cell->extract_fluid(amount);
    if (extracted > 0) {
        // Wake the cell and its neighbors so the fluid system
        // can respond to the changed mass.
        wake_cell(dimension_id, x, y, z);
    }
    return extracted;
}

// ============================================================
// Process ports — per-tick pipe ↔ tile transfer
// ============================================================
//
// For each registered port:
//   1. Injection: read delivered fluid from the pipe network
//      (via pipe_delivered_fn) and inject it into the tile cell.
//   2. Extraction: read fluid from the tile cell and offer it
//      to the pipe network (via pipe_accept_fn).
//
// Transfer is capped by the port's max_transfer_rate.

void TileFluidSystem::process_ports(
        const PipeDeliveredFn& pipe_delivered_fn,
        const PipeAcceptFn& pipe_accept_fn) {
    for (auto& [key, port] : ports_) {
        int32_t budget = port.max_transfer_rate;

        // Phase 1: Inject pipe fluid into tile.
        if (port.is_inject && budget > 0) {
            PipeDelivery delivery = pipe_delivered_fn(key);
            if (delivery.amount > 0 && delivery.fluid_id != kInvalidCellFluidId) {
                int32_t to_inject = (delivery.amount < budget)
                    ? delivery.amount : budget;
                int16_t injected = inject_fluid(
                    key.dimension_id, key.x, key.y, key.z,
                    delivery.fluid_id, static_cast<int16_t>(to_inject),
                    delivery.is_gas);
                budget -= static_cast<int32_t>(injected);
            }
        }

        // Phase 2: Extract tile fluid into pipe.
        if (port.is_extract && budget > 0) {
            TerrainCell* cell = get_terrain_cell(
                key.dimension_id, key.x, key.y, key.z);
            if (cell != nullptr && cell->has_fluid()) {
                int16_t available = cell->fluid_mass;
                int32_t to_extract = (available < budget)
                    ? available : budget;
                // Ask the pipe network how much it will accept.
                int16_t accepted = pipe_accept_fn(
                    key, cell->fluid_type,
                    static_cast<int16_t>(to_extract));
                if (accepted > 0) {
                    extract_fluid_at(
                        key.dimension_id, key.x, key.y, key.z, accepted);
                }
            }
        }
    }
}

// ============================================================
// Thermal conduction
// ============================================================
//
// Each active fluid cell's temperature moves toward the average
// of its 6-connected neighbors. Solid blocks act as insulators
// (no conduction). The conduction rate controls how fast
// temperature equalizes.

void TileFluidSystem::process_thermal_conduction() {
    if (active_cells_.empty()) return;

    static constexpr float kConductionRate = 0.1f;

    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());

    static constexpr int32_t kDx[] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t kDy[] = {0, 0, 1, -1, 0, 0};
    static constexpr int32_t kDz[] = {0, 0, 0, 0, 1, -1};

    // Pre-cache atmosphere temperature per dimension to avoid
    // repeated map lookups inside the inner loop.
    std::unordered_map<std::string, int16_t> atm_temp_cache;

    int budget = kMaxThermalPerTick;

    for (const auto& key : cells) {
        if (budget <= 0) break;

        const TerrainCell* cell = get_terrain_cell_const(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        int64_t temp_sum = cell->fluid_temperature;
        int neighbor_count = 1;

        for (int d = 0; d < 6; ++d) {
            const TerrainCell* nb = get_terrain_cell_const(
                key.dimension_id,
                key.x + kDx[d], key.y + kDy[d], key.z + kDz[d]);
            if (nb == nullptr || nb->is_solid()) continue;

            if (nb->has_fluid()) {
                temp_sum += nb->fluid_temperature;
                ++neighbor_count;
            } else {
                // Use cached atmosphere temperature.
                auto it = atm_temp_cache.find(key.dimension_id);
                if (it == atm_temp_cache.end()) {
                    const AtmosphereConfig& atm =
                        get_atmosphere_config(key.dimension_id);
                    int16_t atm_temp = atm.has_atmosphere ? atm.temperature : 0;
                    atm_temp_cache[key.dimension_id] = atm_temp;
                    it = atm_temp_cache.find(key.dimension_id);
                }
                if (it->second > 0) {
                    temp_sum += it->second;
                    ++neighbor_count;
                }
            }
        }

        if (neighbor_count <= 1) continue;

        float avg_temp = static_cast<float>(temp_sum) /
                         static_cast<float>(neighbor_count);
        float current = static_cast<float>(cell->fluid_temperature);
        float diff = avg_temp - current;
        if (diff < 0) diff = -diff;

        // Skip if temperature difference is below epsilon.
        if (diff < static_cast<float>(kThermalEpsilon)) continue;

        float new_temp = current + (avg_temp - current) * kConductionRate;

        // Apply directly (no deferred map — we only read neighbors,
        // and writing our own temperature doesn't affect their reads
        // since we already computed our new_temp from their old values).
        TerrainCell* mut_cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (mut_cell != nullptr) {
            mut_cell->fluid_temperature = static_cast<int16_t>(new_temp);
        }

        --budget;
    }
}

// ============================================================
// Phase transitions
// ============================================================
//
// Check each active fluid cell's temperature against the fluid
// definition's evaporation/condensation thresholds. If the
// threshold is crossed, convert the fluid to the target type.
//
// Example: water at 373+ K → steam, steam below 373 K → water.

void TileFluidSystem::process_phase_transitions() {
    if (active_cells_.empty()) return;
    if (phase_transition_fluids_.empty()) return;

    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());

    int budget = kMaxPhaseTransitionPerTick;

    for (const auto& key : cells) {
        if (budget <= 0) break;

        TerrainCell* cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;

        // Fast reject: skip fluids that have no phase transitions.
        if (phase_transition_fluids_.find(cell->fluid_type) ==
            phase_transition_fluids_.end()) {
            continue;
        }

        const gt::FluidDefinition* def =
            gt::FluidRegistry::get_fluid(
                static_cast<gt::FluidId>(cell->fluid_type));
        if (def == nullptr) continue;

        bool transitioned = false;

        // Check evaporation: liquid → gas.
        if (!cell->fluid_is_gas && def->evaporation_temp > 0 &&
            def->evaporation_target != gt::kInvalidFluidId &&
            cell->fluid_temperature >=
                static_cast<int16_t>(def->evaporation_temp)) {
            CellFluidId target =
                static_cast<CellFluidId>(def->evaporation_target);
            int16_t mass = cell->fluid_mass;
            cell->clear_fluid();
            cell->insert_fluid(target, mass, true);
            cell->fluid_temperature = static_cast<int16_t>(
                def->evaporation_temp);
            transitioned = true;
        }

        // Check condensation: gas → liquid.
        if (cell->fluid_is_gas && !transitioned &&
            def->condensation_temp > 0 &&
            def->condensation_target != gt::kInvalidFluidId &&
            cell->fluid_temperature <
                static_cast<int16_t>(def->condensation_temp)) {
            CellFluidId target =
                static_cast<CellFluidId>(def->condensation_target);
            int16_t mass = cell->fluid_mass;
            cell->clear_fluid();
            cell->insert_fluid(target, mass, false);
            cell->fluid_temperature = static_cast<int16_t>(
                def->condensation_temp);
            transitioned = true;
        }

        if (transitioned) {
            static constexpr int32_t kDx[] = {1, -1, 0, 0, 0, 0};
            static constexpr int32_t kDy[] = {0, 0, 1, -1, 0, 0};
            static constexpr int32_t kDz[] = {0, 0, 0, 0, 1, -1};
            for (int d = 0; d < 6; ++d) {
                wake_cell(key.dimension_id,
                          key.x + kDx[d],
                          key.y + kDy[d],
                          key.z + kDz[d]);
            }
            --budget;
        }
    }
}

// ============================================================
// Water cycle — evaporation
// ============================================================
//
// Surface water cells with high temperature lose mass to the
// atmosphere. This increases the dimension's humidity level.
// Evaporation rate depends on temperature and surface exposure.

void TileFluidSystem::process_evaporation() {
    if (active_cells_.empty()) return;
    if (cached_water_id_ == kInvalidCellFluidId) return;

    static constexpr int16_t kEvapTempThreshold = 320;
    static constexpr int16_t kMaxEvapRate = 10;

    std::vector<FluidCellKey> cells(active_cells_.begin(), active_cells_.end());

    int budget = kMaxEvaporationPerTick;

    for (const auto& key : cells) {
        if (budget <= 0) break;

        TerrainCell* cell = get_terrain_cell(
            key.dimension_id, key.x, key.y, key.z);
        if (cell == nullptr || !cell->has_fluid()) continue;
        if (cell->fluid_is_gas) continue;

        // Fast reject: only water evaporates in the water cycle.
        if (cell->fluid_type != cached_water_id_) continue;

        if (cell->fluid_temperature < kEvapTempThreshold) continue;

        // Check if this is a surface cell (has an empty or gas
        // neighbor above).
        GravityStep gs = compute_gravity_step(
            key.dimension_id, key.x, key.y, key.z);
        const TerrainCell* above = get_terrain_cell_const(
            key.dimension_id,
            key.x - gs.dx, key.y - gs.dy, key.z - gs.dz);
        if (above != nullptr && above->is_solid()) continue;
        if (above != nullptr && above->has_fluid() && !above->fluid_is_gas) {
            continue;
        }

        int16_t excess = cell->fluid_temperature - kEvapTempThreshold;
        int16_t evap_rate = excess / 10;
        if (evap_rate > kMaxEvapRate) evap_rate = kMaxEvapRate;
        if (evap_rate <= 0) continue;

        int16_t evaporated = cell->extract_fluid(evap_rate);
        if (evaporated > 0) {
            float& humidity = humidity_levels_[key.dimension_id];
            humidity += static_cast<float>(evaporated) * 0.0001f;
            if (humidity > 1.0f) humidity = 1.0f;
            --budget;
        }
    }
}

// ============================================================
// Water cycle — rainfall
// ============================================================
//
// If humidity is high enough, deposit water at high-elevation
// cells in the chunk. This is a macro operation that does not
// use per-cell simulation.

void TileFluidSystem::process_rainfall(const ChunkKey& chunk) {
    if (cached_water_id_ == kInvalidCellFluidId) return;

    const std::string& dim = chunk.dimension_id;
    float humidity = get_humidity(dim);
    if (humidity < 0.3f) return;

    const AtmosphereConfig& atm = get_atmosphere_config(dim);
    if (!atm.has_atmosphere) return;

    float rain_rate = (humidity - 0.3f) * 20.0f;
    if (rain_rate < 1.0f) return;

    int16_t rain_amount = static_cast<int16_t>(rain_rate);

    int32_t cx = chunk.chunk_x;
    int32_t cy = chunk.chunk_y;
    int32_t cz = chunk.chunk_z;

    int32_t top_y = (cy + 1) * ChunkData::kChunkSize - 1;
    int32_t bottom_y = cy * ChunkData::kChunkSize;

    // Pre-compute world X/Z offsets to avoid repeated multiply.
    int32_t world_x_base = cx * ChunkData::kChunkSize;
    int32_t world_z_base = cz * ChunkData::kChunkSize;

    for (int lz = 0; lz < ChunkData::kChunkSize; lz += 4) {
        for (int lx = 0; lx < ChunkData::kChunkSize; lx += 4) {
            for (int32_t wy = top_y; wy >= bottom_y; --wy) {
                TerrainCell* cell = get_terrain_cell(
                    dim, world_x_base + lx, wy, world_z_base + lz);
                if (cell == nullptr) continue;
                if (cell->is_solid()) continue;

                // Deposit water only if cell is empty or already has water.
                if (!cell->has_fluid() ||
                    cell->fluid_type == cached_water_id_) {
                    int16_t deposited = cell->insert_fluid(
                        cached_water_id_, rain_amount, false);
                    cell->fluid_temperature = atm.temperature;
                    if (deposited > 0) {
                        wake_cell(dim,
                                  world_x_base + lx, wy,
                                  world_z_base + lz);
                    }
                }
                break;
            }
        }
    }

    float& humidity_ref = humidity_levels_[dim];
    humidity_ref -= rain_rate * 0.001f;
    if (humidity_ref < 0.0f) humidity_ref = 0.0f;
}

// ============================================================
// Humidity accessors
// ============================================================

float TileFluidSystem::get_humidity(
        const std::string& dimension_id) const {
    auto it = humidity_levels_.find(dimension_id);
    return (it != humidity_levels_.end()) ? it->second : 0.0f;
}

void TileFluidSystem::set_humidity(
        const std::string& dimension_id, float humidity) {
    humidity_levels_[dimension_id] = std::clamp(humidity, 0.0f, 1.0f);
}

// ============================================================
// Atmosphere system
// ============================================================
//
// The atmosphere system provides a "default gas" for open spaces.
// When a sealed space is opened (e.g. by mining a block), the
// exposed empty cells are filled with atmosphere gas. When a
// sealed space is created (e.g. by placing a block), the gas
// is released to the infinite atmosphere.
//
// This avoids the need to simulate the entire atmosphere as
// per-cell fluid — the atmosphere is treated as an infinite
// reservoir with fixed properties.

TileFluidSystem::AtmosphereConfig TileFluidSystem::kDefaultAtmosphere;

void TileFluidSystem::set_atmosphere_config(
        const std::string& dimension_id,
        const AtmosphereConfig& config) {
    atmosphere_configs_[dimension_id] = config;
}

const TileFluidSystem::AtmosphereConfig&
TileFluidSystem::get_atmosphere_config(
        const std::string& dimension_id) const {
    auto it = atmosphere_configs_.find(dimension_id);
    return (it != atmosphere_configs_.end()) ? it->second : kDefaultAtmosphere;
}

int16_t TileFluidSystem::fill_with_atmosphere(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) {
    const AtmosphereConfig& atm = get_atmosphere_config(dimension_id);
    if (!atm.has_atmosphere) return 0;
    if (atm.gas_type == kInvalidCellFluidId) return 0;

    TerrainCell* cell = get_terrain_cell(dimension_id, x, y, z);
    if (cell == nullptr) return 0;
    if (cell->is_solid()) return 0;
    if (cell->has_fluid()) return 0;

    // Fill the cell with atmosphere gas.
    int16_t filled = cell->insert_fluid(atm.gas_type, atm.gas_mass, true);
    cell->fluid_temperature = atm.temperature;

    // Atmosphere-filled cells start as sleeping (equilibrium).
    // They will be woken if something disturbs them.
    return filled;
}

bool TileFluidSystem::should_fill_atmosphere(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) const {
    const AtmosphereConfig& atm = get_atmosphere_config(dimension_id);
    if (!atm.has_atmosphere) return false;
    if (atm.gas_type == kInvalidCellFluidId) return false;

    const TerrainCell* cell = get_terrain_cell_const(
        dimension_id, x, y, z);
    if (cell == nullptr) return false;
    if (cell->is_solid()) return false;
    if (cell->has_fluid()) return false;

    // A cell should be filled with atmosphere if it is exposed
    // to open space. Simple heuristic: check if any 6-connected
    // neighbor is also non-solid and either has no fluid or has
    // the atmosphere gas type. A more accurate check would do
    // a full BFS to the surface, but that is expensive.
    //
    // For now, use a simple rule: if the cell above is non-solid
    // and either empty or has atmosphere gas, this cell is exposed.
    // This handles the common case of mining downward into terrain.
    static constexpr int32_t kDx[] = {0, 0, 0, 0, 0, 0};
    static constexpr int32_t kDy[] = {1, -1, 0, 0, 0, 0};
    static constexpr int32_t kDz[] = {0, 0, 0, 0, 0, 0};

    for (int d = 0; d < 6; ++d) {
        const TerrainCell* nb = get_terrain_cell_const(
            dimension_id,
            x + kDx[d], y + kDy[d], z + kDz[d]);
        if (nb == nullptr) {
            // Beyond loaded chunks: assume open atmosphere.
            return true;
        }
        if (nb->is_solid()) continue;

        if (!nb->has_fluid()) {
            // Empty neighbor: could be atmosphere.
            // Check recursively? No — just assume true for now.
            return true;
        }
        if (nb->fluid_is_gas && nb->fluid_type == atm.gas_type) {
            // Neighbor has atmosphere gas: this cell is exposed.
            return true;
        }
    }

    return false;
}

void TileFluidSystem::release_to_atmosphere(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) {
    // The atmosphere is infinite — gas released to it is simply
    // destroyed. No mass conservation tracking needed at the
    // atmosphere level.
    TerrainCell* cell = get_terrain_cell(dimension_id, x, y, z);
    if (cell == nullptr) return;
    if (!cell->has_fluid()) return;
    if (!cell->fluid_is_gas) return;

    const AtmosphereConfig& atm = get_atmosphere_config(dimension_id);
    if (!atm.has_atmosphere) return;

    // Only release gas that matches the atmosphere type.
    if (cell->fluid_type == atm.gas_type) {
        cell->clear_fluid();
    }
    // Non-atmosphere gases in an open space would disperse into
    // the atmosphere — for simplicity, also clear them.
    // A more accurate model would track pollution/composition.
    cell->clear_fluid();
}

// ============================================================
// Helpers
// ============================================================

FluidCellKey TileFluidSystem::make_key(
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
