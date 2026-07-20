// Hybrid, deterministic voxel-fluid simulation.
//
// Low-density disturbances use a sparse active-cell set. Dense chunks use a
// double-buffered cellular automaton for authoritative mass transfer; eligible
// homogeneous interiors additionally use an LBM velocity field to guide that
// CA step. Settled chunks retain only a compact equilibrium summary until an
// adjacent mutation wakes them. The optional compute backend remains behind a
// value-only contract, so client GPU code never owns mutable simulation data.

#pragma once

#include "game/simulation/fluid_compute_backend.h"
#include "game/simulation/game_fluid_system_events.h"
#include "game/worldgen/world_gen_config.h"
#include "voxel/data/chunk_registry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game {

struct GameFluidSystemConfig {
    uint32_t max_sparse_cells_per_tick = 1024;
    uint32_t dense_activation_cells_per_chunk = 192;
    uint32_t max_dense_chunks_per_tick = 2;
    // LBM is deliberately restricted to high-density, single-phase chunk
    // interiors. CA remains the fallback for gas, mixed fluids, and edges.
    uint32_t lattice_boltzmann_activation_cells_per_chunk = 768;
    int16_t lattice_boltzmann_min_cell_mass = 250;
    int16_t lattice_boltzmann_gravity_impulse = 64;
    bool enable_lattice_boltzmann = true;
    // A GPU backend may run only after it explicitly promises deterministic
    // output. The default keeps all authority in the CPU reference solver.
    bool allow_deterministic_compute_backend = false;
    uint32_t telemetry_interval_ticks = 100;
};

struct FluidEquilibriumSummary {
    snt::voxel::CellFluidId fluid_type = snt::voxel::kInvalidCellFluidId;
    uint32_t fluid_cell_count = 0;
    int64_t total_fluid_mass = 0;
    int32_t surface_block_y = 0;
    int16_t average_temperature = 300;
};

class GameFluidSystem final {
public:
    GameFluidSystem(snt::voxel::ChunkRegistry& chunks,
                    const WorldGenConfigSnapshot& worldgen_config,
                    GameFluidSystemConfig config = {});

    GameFluidSystem(const GameFluidSystem&) = delete;
    GameFluidSystem& operator=(const GameFluidSystem&) = delete;

    void set_mutation_sink(IFluidMutationSink* sink) noexcept { mutation_sink_ = sink; }
    void set_presentation_sink(IFluidPresentationSink* sink) noexcept {
        presentation_sink_ = sink;
    }
    void set_telemetry_sink(IFluidSimulationTelemetrySink* sink) noexcept {
        telemetry_sink_ = sink;
    }
    void set_compute_backend(IFluidComputeBackend* backend) noexcept {
        compute_backend_ = backend;
    }

    // Called once after chunks are loaded or generated. Existing fluid cells
    // are scheduled without requiring a legacy world-wide tick registration.
    void initialize_loaded_chunks();

    void schedule_after_terrain_mutation(std::string_view dimension_id,
                                         int32_t block_x,
                                         int32_t block_y,
                                         int32_t block_z,
                                         uint64_t source_tick);

    // Narrow authority API for future pipes, buckets, and worldgen sources.
    // The calls preserve mass and wake the affected local neighborhood.
    [[nodiscard]] int16_t inject_fluid(std::string_view dimension_id,
                                       int32_t block_x,
                                       int32_t block_y,
                                       int32_t block_z,
                                       snt::voxel::CellFluidId fluid_type,
                                       int16_t amount,
                                       int16_t temperature = 300,
                                       bool is_gas = false);
    [[nodiscard]] int16_t extract_fluid(std::string_view dimension_id,
                                        int32_t block_x,
                                        int32_t block_y,
                                        int32_t block_z,
                                        int16_t amount);

    // Advances all three solver layers from the shared authoritative clock.
    void tick(uint64_t tick_index);

    [[nodiscard]] size_t active_cell_count() const noexcept { return active_cells_.size(); }
    [[nodiscard]] size_t dense_chunk_count() const noexcept { return dense_chunks_.size(); }
    [[nodiscard]] size_t equilibrium_chunk_count() const noexcept {
        return equilibrium_chunks_.size();
    }
    [[nodiscard]] const FluidSimulationTelemetry& last_telemetry() const noexcept {
        return last_telemetry_;
    }
    [[nodiscard]] const FluidEquilibriumSummary* find_equilibrium_summary(
        const snt::voxel::ChunkKey& key) const noexcept;

private:
    struct ChunkKeyLess {
        [[nodiscard]] bool operator()(const snt::voxel::ChunkKey& left,
                                      const snt::voxel::ChunkKey& right) const noexcept;
    };

    struct FluidCellKey {
        std::string dimension_id;
        int32_t block_x = 0;
        int32_t block_y = 0;
        int32_t block_z = 0;
    };

    struct FluidCellKeyLess {
        [[nodiscard]] bool operator()(const FluidCellKey& left,
                                      const FluidCellKey& right) const noexcept;
    };

    struct ResolvedCell {
        snt::voxel::VoxelChunk* chunk = nullptr;
        snt::voxel::TerrainCell* cell = nullptr;
        snt::voxel::ChunkKey chunk_key;
        int32_t local_x = 0;
        int32_t local_y = 0;
        int32_t local_z = 0;
    };

    struct GravityStep {
        int32_t dx = 0;
        int32_t dy = -1;
        int32_t dz = 0;
    };

    struct DenseCellState {
        snt::voxel::CellFluidId fluid_type = snt::voxel::kInvalidCellFluidId;
        int16_t mass = 0;
        int16_t temperature = 300;
        bool is_gas = false;
    };

    [[nodiscard]] static int32_t floor_divide(int32_t value, int32_t divisor) noexcept;
    [[nodiscard]] static bool offset_coordinate(int32_t value, int32_t offset,
                                                int32_t& out) noexcept;
    [[nodiscard]] static bool same_terrain_cell(const snt::voxel::TerrainCell& left,
                                                 const snt::voxel::TerrainCell& right) noexcept;
    [[nodiscard]] static bool same_fluid_state(const DenseCellState& left,
                                                const DenseCellState& right) noexcept;

    [[nodiscard]] snt::voxel::ChunkKey chunk_key_for(std::string_view dimension_id,
                                                       int32_t block_x,
                                                       int32_t block_y,
                                                       int32_t block_z) const;
    [[nodiscard]] ResolvedCell resolve_cell(std::string_view dimension_id,
                                            int32_t block_x,
                                            int32_t block_y,
                                            int32_t block_z);
    [[nodiscard]] GravityStep gravity_step(std::string_view dimension_id,
                                           int32_t block_x,
                                           int32_t block_y,
                                           int32_t block_z) const noexcept;
    [[nodiscard]] bool chunk_is_dense(const snt::voxel::ChunkKey& key) const noexcept;
    [[nodiscard]] bool chunk_has_active_cells(const snt::voxel::ChunkKey& key) const;
    [[nodiscard]] bool can_use_lattice_boltzmann(
        const snt::voxel::ChunkKey& key) const;
    [[nodiscard]] FluidSimulationLayer dense_layer_for(
        const snt::voxel::ChunkKey& key) const noexcept;

    void wake_cell(std::string_view dimension_id, int32_t block_x, int32_t block_y,
                   int32_t block_z);
    void wake_fluid_neighbors(std::string_view dimension_id, int32_t block_x,
                              int32_t block_y, int32_t block_z);
    void mark_chunk_touched(const snt::voxel::ChunkKey& key,
                            FluidSimulationLayer layer);
    void invalidate_lattice_boltzmann_state(const snt::voxel::ChunkKey& key);
    void emit_terrain_change(const FluidCellKey& key,
                             const snt::voxel::TerrainCell& previous,
                             const snt::voxel::TerrainCell& current,
                             FluidSimulationLayer layer);

    void promote_dense_chunks();
    void process_dense_chunks(uint64_t tick_index);
    [[nodiscard]] bool process_dense_chunk(const snt::voxel::ChunkKey& key);
    [[nodiscard]] bool process_lattice_boltzmann_chunk(
        const snt::voxel::ChunkKey& key, uint64_t tick_index);
    [[nodiscard]] bool dense_chunk_has_cross_boundary_flow(
        const snt::voxel::ChunkKey& key);
    void settle_equilibrium_chunk(const snt::voxel::ChunkKey& key);
    void process_sparse_cells();
    [[nodiscard]] bool process_sparse_cell(const FluidCellKey& key);
    [[nodiscard]] bool try_transfer(const FluidCellKey& source_key,
                                    const FluidCellKey& destination_key,
                                    int16_t requested_amount,
                                    FluidSimulationLayer layer);
    [[nodiscard]] bool can_receive(const snt::voxel::TerrainCell& source,
                                   const snt::voxel::TerrainCell& destination) const noexcept;
    [[nodiscard]] bool can_flow_into(const snt::voxel::TerrainCell& source,
                                     const snt::voxel::TerrainCell& destination,
                                     bool require_lower_mass) const noexcept;
    void update_equilibrium_candidates();
    void flush_presentation_updates(uint64_t tick_index);
    void emit_low_frequency_telemetry(uint64_t tick_index);

    snt::voxel::ChunkRegistry* chunks_ = nullptr;
    const WorldGenConfigSnapshot* worldgen_config_ = nullptr;
    GameFluidSystemConfig config_;
    IFluidMutationSink* mutation_sink_ = nullptr;
    IFluidPresentationSink* presentation_sink_ = nullptr;
    IFluidSimulationTelemetrySink* telemetry_sink_ = nullptr;
    IFluidComputeBackend* compute_backend_ = nullptr;
    std::set<FluidCellKey, FluidCellKeyLess> active_cells_;
    std::set<snt::voxel::ChunkKey, ChunkKeyLess> dense_chunks_;
    std::map<snt::voxel::ChunkKey, FluidSimulationLayer, ChunkKeyLess>
        dense_chunk_layers_;
    std::deque<snt::voxel::ChunkKey> dense_queue_;
    FluidLatticeBoltzmannSolver lattice_boltzmann_solver_;
    std::map<snt::voxel::ChunkKey, FluidLbmChunkState, ChunkKeyLess>
        lattice_boltzmann_states_;
    std::map<snt::voxel::ChunkKey, FluidEquilibriumSummary, ChunkKeyLess> equilibrium_chunks_;
    std::set<snt::voxel::ChunkKey, ChunkKeyLess> equilibrium_candidates_;
    std::map<snt::voxel::ChunkKey, FluidSimulationLayer, ChunkKeyLess>
        presentation_dirty_chunks_;
    FluidSimulationTelemetry last_telemetry_;
    uint64_t last_telemetry_tick_ = 0;
};

}  // namespace snt::game
