#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "simulation_system.hpp"
#include "region_graph.hpp"
#include "../world/terrain_data.hpp"

namespace science_and_theology {

// ============================================================
// FluidCellKey — identifies a single active fluid cell
// ============================================================
//
// Used as the key in the active cell set. Only cells that are
// currently being simulated (not in equilibrium) are tracked.

struct FluidCellKey {
    std::string dimension_id;
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    bool operator==(const FluidCellKey& o) const {
        return x == o.x && y == o.y && z == o.z
            && dimension_id == o.dimension_id;
    }

    bool operator!=(const FluidCellKey& o) const { return !(*this == o); }
};

} // namespace science_and_theology

// std::hash specialization for FluidCellKey.
template <>
struct std::hash<science_and_theology::FluidCellKey> {
    size_t operator()(const science_and_theology::FluidCellKey& k) const {
        size_t h = std::hash<std::string>()(k.dimension_id);
        h ^= std::hash<int32_t>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace science_and_theology {

// ============================================================
// TileFluidSystem — per-tile capacity-based fluid simulation
// ============================================================
//
// Implements Oxygen Not Included-style fluid dynamics:
//   - Each cell has a fixed capacity (kCellFluidCapacity mB).
//   - Fluid can spread horizontally and flow down by gravity.
//   - Fluid and solid blocks are mutually exclusive: placing a
//     solid block displaces fluid to adjacent cells.
//   - Large bodies of fluid use the water-level equilibrium
//     model (FluidRegionData) for O(1) per-tick cost.
//
// Priority 3 — after BlockPhysics (1) and before Machine (2)
// so that fluid displacement settles before machine tick.
//
// Thread safety: main thread only. Not thread-safe.

class TileFluidSystem : public SimulationSystem {
public:
    TileFluidSystem() = default;
    ~TileFluidSystem() override = default;

    SIMULATION_SYSTEM_NAME(TileFluidSystem, "TileFluidSystem")

    void initialize(WorldData* world, EventBus* bus) override;
    void tick_active(const ChunkKey& chunk, float delta) override;
    void tick_sleeping(const ChunkKey& chunk, float delta) override;
    void shutdown() override;

    // Runs after BlockPhysics (priority 1), before Machine (priority 2).
    int priority() const override { return 3; }

    // Not thread-safe: fluid cells may cross chunk boundaries.
    bool is_thread_safe() const override { return false; }

    // --- Active cell management ---

    // Wake a cell for simulation. Called when a cell is disturbed
    // (block mined, fluid injected, temperature change, etc.).
    void wake_cell(const std::string& dimension_id,
                   int32_t x, int32_t y, int32_t z);

    // Mark a cell as sleeping (equilibrium reached).
    void sleep_cell(const std::string& dimension_id,
                    int32_t x, int32_t y, int32_t z);

    // Returns the number of currently active fluid cells.
    size_t active_cell_count() const { return active_cells_.size(); }

    // --- Fluid displacement ---

    // Displaces fluid from a cell when a solid block is placed.
    // The fluid is moved to adjacent cells (gravity direction first,
    // then horizontal neighbors). Any fluid that cannot be displaced
    // is destroyed (should not happen in normal gameplay).
    void displace_fluid(const std::string& dimension_id,
                        int32_t x, int32_t y, int32_t z);

    // --- Simulation budget ---

    // Maximum number of active cells to simulate per tick.
    // Prevents frame spikes from large fluid disturbances.
    static constexpr int kMaxActiveCellsPerTick = 1024;

    // Number of consecutive idle ticks before a cell goes to sleep.
    static constexpr int kSleepThreshold = 10;

    // Minimum pressure difference (mB) to consider a cell active.
    static constexpr int16_t kPressureEpsilon = 1;

    // --- Fluid region data ---

    // Returns the FluidRegionData for a given region, or nullptr.
    const FluidRegionData* get_fluid_region_data(uint64_t region_id) const;
    FluidRegionData* get_fluid_region_data_mut(uint64_t region_id);

    // Returns the number of fluid regions.
    size_t fluid_region_count() const { return fluid_regions_.size(); }

private:
    // --- Per-tick simulation steps ---

    // Step 1: Process gravity flow for all active cells.
    // Fluid flows down first (gravity direction), then spreads
    // horizontally when the cell below is full or solid.
    void process_gravity_flow();

    // Step 2: Process pressure equalization for all active cells.
    // Fluid moves from high-pressure cells to low-pressure neighbors.
    void process_pressure_equalization();

    // Step 3: Detect cells that have reached equilibrium and put
    // them to sleep.
    void detect_sleeping_cells();

    // Step 4: Update fluid region connectivity and aggregate data.
    void update_fluid_regions(const ChunkKey& chunk);

    // --- Helpers ---

    // Converts a world position to a FluidCellKey.
    FluidCellKey make_key(const std::string& dim,
                          int32_t x, int32_t y, int32_t z) const;

    // Gets the TerrainCell at a world position, or nullptr if
    // the chunk is not loaded.
    TerrainCell* get_terrain_cell(const std::string& dimension_id,
                                  int32_t x, int32_t y, int32_t z);
    const TerrainCell* get_terrain_cell_const(
        const std::string& dimension_id,
        int32_t x, int32_t y, int32_t z) const;

    // Computes the gravity direction step for a given position.
    // Returns (dx, dy, dz) pointing toward the planet center.
    // Flat worlds return (0, -1, 0).
    struct GravityStep { int32_t dx, dy, dz; };
    GravityStep compute_gravity_step(const std::string& dimension_id,
                                     int32_t x, int32_t y, int32_t z) const;

    // --- Data ---

    // Set of currently active (simulating) fluid cells.
    std::unordered_set<FluidCellKey> active_cells_;

    // Per-cell idle counter. When this reaches kSleepThreshold,
    // the cell goes to sleep.
    std::unordered_map<FluidCellKey, int> idle_counters_;

    // Per-region fluid data, keyed by region ID.
    // Region IDs come from the FLUID RegionGraph in RegionSystem.
    std::unordered_map<uint64_t, FluidRegionData> fluid_regions_;

    // World data reference (set in initialize).
    WorldData* world_ = nullptr;
    EventBus* event_bus_ = nullptr;
};

} // namespace science_and_theology
