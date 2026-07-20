#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "simulation_system.hpp"
#include "../world/terrain_data.hpp"
#include "../fluid/fluid_registry.hpp"

namespace science_and_theology {

// Fluid aggregate state belongs to TileFluidSystem. Its IDs are local to this
// subsystem and are not RegionTopology handles used by game-owned power or
// pollution simulation.
struct FluidRegionData {
    CellFluidId fluid_type = kInvalidCellFluidId;
    bool is_equilibrium = true;
    int32_t water_level_y = 0;
    int64_t total_mass = 0;
    size_t fluid_cell_count = 0;
    int16_t avg_temperature = 300;
};

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
    void tick_active(const ChunkKey& chunk, float delta,
                     const TickContext* ctx = nullptr) override;
    void tick_sleeping(const ChunkKey& chunk, float delta,
                       const TickContext* ctx = nullptr) override;
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

    // --- Atmosphere system ---

    // AtmosphereConfig describes the default atmosphere for a dimension.
    // When a sealed space is opened to the atmosphere, gas from this
    // config is injected into the exposed cells. When a sealed space
    // is created (blocks placed), excess gas is released to atmosphere.
    struct AtmosphereConfig {
        // Default gas type in the atmosphere (e.g. oxygen, nitrogen mix).
        CellFluidId gas_type = kInvalidCellFluidId;
        // Default gas mass per cell (mB). Typically kCellFluidCapacity
        // for a full atmosphere.
        int16_t gas_mass = kCellFluidCapacity;
        // Default temperature (Kelvin).
        int16_t temperature = 293;
        // Whether this dimension has an atmosphere at all.
        bool has_atmosphere = true;
    };

    // Set the atmosphere config for a dimension.
    void set_atmosphere_config(const std::string& dimension_id,
                               const AtmosphereConfig& config);

    // Get the atmosphere config for a dimension.
    const AtmosphereConfig& get_atmosphere_config(
        const std::string& dimension_id) const;

    // Fill a cell with atmosphere gas. Called when a sealed space
    // is opened (e.g. mining a block reveals an empty cell that
    // should be filled with atmosphere). Returns the amount filled.
    int16_t fill_with_atmosphere(const std::string& dimension_id,
                                 int32_t x, int32_t y, int32_t z);

    // Check if a cell should be filled with atmosphere.
    // A cell qualifies if: it is not solid, has no fluid, and is
    // exposed to open space (connected to the atmosphere boundary
    // or above the terrain surface).
    bool should_fill_atmosphere(const std::string& dimension_id,
                                int32_t x, int32_t y, int32_t z) const;

    // Release gas to atmosphere. Called when placing a block
    // displaces gas in a cell that was connected to the atmosphere.
    // The gas is simply destroyed (absorbed by the infinite atmosphere).
    void release_to_atmosphere(const std::string& dimension_id,
                               int32_t x, int32_t y, int32_t z);

    // --- Simulation budget ---

    // Maximum number of active cells to simulate per tick.
    // Prevents frame spikes from large fluid disturbances.
    static constexpr int kMaxActiveCellsPerTick = 1024;

    // Number of consecutive idle ticks before a cell goes to sleep.
    static constexpr int kSleepThreshold = 10;

    // Minimum pressure difference (mB) to consider a cell active.
    static constexpr int16_t kPressureEpsilon = 1;

    // Maximum thermal conduction computations per tick.
    static constexpr int kMaxThermalPerTick = 512;

    // Maximum phase transition checks per tick.
    static constexpr int kMaxPhaseTransitionPerTick = 256;

    // Maximum evaporation operations per tick.
    static constexpr int kMaxEvaporationPerTick = 128;

    // Rainfall runs every N ticks (not every tick).
    static constexpr int kRainfallInterval = 20;

    // Minimum temperature change (K) to consider a cell thermally active.
    static constexpr int16_t kThermalEpsilon = 1;

    // Minimum fluid mass (mB) for horizontal pressure equalization.
    // Cells with less mass than this will not spread horizontally.
    // This prevents a single bucket of water from spreading into
    // a thin film across many cells. At 400, a 1000 mB bucket
    // will only spread to ~2-3 neighbors before stopping.
    static constexpr int16_t kMinHorizontalFlowMass = 400;

    // Surface tension: minimum pressure difference as a fraction
    // of the cell's mass for horizontal flow to occur.
    // Expressed as a divisor (e.g. 5 = 20%). If the difference
    // between two cells is less than this fraction of the source
    // cell's mass, no transfer occurs. This prevents micro-
    // transfers between nearly-equal cells, helping them reach
    // sleep state faster.
    static constexpr int16_t kSurfaceTensionDivisor = 5;

    // --- Fluid region data ---

    // Returns the FluidRegionData for a given region, or nullptr.
    const FluidRegionData* get_fluid_region_data(uint64_t region_id) const;
    FluidRegionData* get_fluid_region_data_mut(uint64_t region_id);

    // Returns the number of fluid regions.
    size_t fluid_region_count() const { return fluid_regions_.size(); }

    // Returns the equilibrium region ID for a cell, or 0 if none.
    uint64_t get_cell_equilibrium_region(const FluidCellKey& key) const;

    // Returns the total number of equilibrium regions.
    size_t equilibrium_region_count() const { return fluid_regions_.size(); }

    // --- Fluid port (pipe ↔ tile interface) ---

    // A FluidPort bridges the FluidNetwork (pipe graph) and the
    // TileFluidSystem (per-cell fluid). It sits at a world position
    // that is NOT solid — the port cell is an open space where fluid
    // from pipes can be injected, and fluid from the tile system can
    // be extracted by pipes.
    //
    // Each tick, the port:
    //   1. Receives fluid from the pipe network (inject into tile).
    //   2. Provides fluid to the pipe network (extract from tile).
    //
    // The port itself does not store fluid — it reads/writes the
    // TerrainCell at its position.

    struct FluidPort {
        FluidCellKey position;
        // Maximum transfer rate per tick (mB). Limits throughput.
        int32_t max_transfer_rate = 1000;
        // If true, this port injects pipe fluid into the tile system.
        bool is_inject = true;
        // If true, this port extracts tile fluid into the pipe system.
        bool is_extract = true;
    };

    // Register a fluid port at the given position.
    void register_port(const std::string& dimension_id,
                       int32_t x, int32_t y, int32_t z,
                       int32_t max_transfer_rate = 1000,
                       bool is_inject = true,
                       bool is_extract = true);

    // Remove a fluid port.
    void unregister_port(const std::string& dimension_id,
                         int32_t x, int32_t y, int32_t z);

    // Returns the number of registered fluid ports.
    size_t port_count() const { return ports_.size(); }

    // Inject fluid from an external source (e.g. pipe network) into
    // the tile system at a given position. Returns the amount
    // actually injected.
    int16_t inject_fluid(const std::string& dimension_id,
                         int32_t x, int32_t y, int32_t z,
                         CellFluidId fluid, int16_t amount,
                         bool is_gas = false);

    // Extract fluid from the tile system at a given position for
    // an external consumer (e.g. pipe network). Returns the amount
    // actually extracted.
    int16_t extract_fluid_at(const std::string& dimension_id,
                             int32_t x, int32_t y, int32_t z,
                             int16_t amount);

    // Process all registered ports: transfer fluid between pipe
    // network and tile system. Should be called once per tick
    // after the pipe network has been updated.
    // The callback functions bridge the gt::FluidId ↔ CellFluidId
    // conversion and the actual pipe network read/write.
    //
    // PipeDelivery: returned by pipe_delivered_fn to describe what
    //   the pipe network is delivering to this port.
    struct PipeDelivery {
        CellFluidId fluid_id = kInvalidCellFluidId;
        int16_t amount = 0;
        bool is_gas = false;
    };

    // pipe_delivered_fn(port_key) → what the pipe network delivered.
    // pipe_accept_fn(port_key, fluid_id, amount) → how much
    //   the pipe network accepted from this port.
    using PipeDeliveredFn = std::function<PipeDelivery(
        const FluidCellKey&)>;
    using PipeAcceptFn = std::function<int16_t(
        const FluidCellKey&, CellFluidId, int16_t)>;

    void process_ports(const PipeDeliveredFn& pipe_delivered_fn,
                       const PipeAcceptFn& pipe_accept_fn);

    // --- Temperature / phase transition ---

    // Process phase transitions for all active fluid cells.
    // Checks each cell's temperature against the fluid definition's
    // evaporation/condensation thresholds and converts the fluid
    // if the threshold is crossed.
    void process_phase_transitions();

    // Process thermal conduction for all active fluid cells.
    // Each cell's temperature moves toward the average of its
    // 6-connected neighbors (solid blocks act as insulators).
    void process_thermal_conduction();

    // --- Water cycle ---

    // Process evaporation: water cells at the surface with
    // temperature above the evaporation threshold lose mass
    // to the atmosphere. Called once per tick for active chunks.
    void process_evaporation();

    // Process rainfall: if the atmosphere has sufficient humidity,
    // water is deposited at high-elevation cells. This is a
    // Region-level macro operation, not per-cell.
    void process_rainfall(const ChunkKey& chunk);

    // Humidity level for a dimension [0.0, 1.0].
    // Higher humidity = more rainfall. Driven by evaporation.
    float get_humidity(const std::string& dimension_id) const;
    void set_humidity(const std::string& dimension_id, float humidity);

private:
    // --- Per-tick simulation steps ---

    // Step 1: Process gravity flow for all active cells.
    // Fluid flows down first (gravity direction), then spreads
    // horizontally when the cell below is full or solid.
    void process_gravity_flow();

    // Step 2: Process pressure equalization for all active cells.
    // Fluid moves from high-pressure cells to low-pressure neighbors.
    void process_pressure_equalization();

    // Step 2b: Process gas diffusion for all active gas cells.
    // Gas diffuses equally in all 6 directions (no gravity bias).
    void process_gas_diffusion();

    // Step 3: Detect cells that have reached equilibrium and put
    // them to sleep.
    void detect_sleeping_cells();

    // Step 4: Update fluid region connectivity and aggregate data.
    void update_fluid_regions(const ChunkKey& chunk);

    // --- Equilibrium region management ---

    // BFS from a seed cell to discover all connected fluid cells
    // of the same type. Returns the set of FluidCellKeys found.
    std::unordered_set<FluidCellKey> bfs_fluid_component(
        const FluidCellKey& seed) const;

    // Check if a connected component of sleeping fluid cells
    // qualifies as an equilibrium region. If so, create or update
    // the FluidRegionData and register all cells in the mapping.
    void try_promote_equilibrium(const FluidCellKey& seed);

    // Wake all cells in an equilibrium region, transitioning it
    // back to per-cell simulation. Called when a disturbance
    // (block placement, mining, machine extraction) affects
    // any cell in the region.
    void wake_equilibrium_region(uint64_t region_id);

    // Compute water_level_y and total_mass for a set of fluid cells.
    // Assumes all cells have the same fluid type.
    void compute_region_stats(
        const std::unordered_set<FluidCellKey>& cells,
        FluidRegionData& out) const;

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
    // Region IDs are allocated locally by TileFluidSystem.
    std::unordered_map<uint64_t, FluidRegionData> fluid_regions_;

    // Maps each sleeping fluid cell to its equilibrium region ID.
    // Only populated for cells that belong to an equilibrium region.
    std::unordered_map<FluidCellKey, uint64_t> cell_to_region_;

    // Next equilibrium region ID counter.
    uint64_t next_region_id_ = 1;

    // Registered fluid ports (pipe ↔ tile interface).
    // Keyed by position for O(1) lookup.
    std::unordered_map<FluidCellKey, FluidPort> ports_;

    // Per-dimension atmosphere configuration.
    std::unordered_map<std::string, AtmosphereConfig> atmosphere_configs_;

    // Default atmosphere config (returned when dimension not found).
    static AtmosphereConfig kDefaultAtmosphere;

    // Per-dimension humidity level [0.0, 1.0] for water cycle.
    // Increased by evaporation, decreased by rainfall.
    std::unordered_map<std::string, float> humidity_levels_;

    // Cached water fluid ID (from FluidRegistry). Set in initialize().
    CellFluidId cached_water_id_ = kInvalidCellFluidId;

    // Set of fluid types that have phase transitions.
    // Populated in initialize() by scanning FluidRegistry.
    std::unordered_set<CellFluidId> phase_transition_fluids_;

    // Tick counter to prevent duplicate simulation when multiple
    // active chunks are processed in the same frame.
    int64_t last_sim_tick_ = -1;

    // World data reference (set in initialize).
    WorldData* world_ = nullptr;
    EventBus* event_bus_ = nullptr;
};

} // namespace science_and_theology
