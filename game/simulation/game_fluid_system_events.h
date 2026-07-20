// Game-owned fluid simulation integration contracts.
//
// The authoritative solver, replication, and optional GPU presentation stay
// separate. All cross-module data is copied by value so neither networking nor
// rendering can retain mutable chunk ownership.

#pragma once

#include "voxel/data/voxel_chunk.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace snt::game {

enum class FluidSimulationLayer : uint8_t {
    kSparse = 0,
    kDenseCellularAutomaton = 1,
    kLatticeBoltzmann = 2,
    kEquilibrium = 3,
};

struct FluidTerrainChange {
    std::string dimension_id;
    int32_t block_x = 0;
    int32_t block_y = 0;
    int32_t block_z = 0;
    uint32_t previous_material = 0;
    uint32_t previous_flags = 0;
    snt::voxel::CellFluidId previous_fluid_type = snt::voxel::kInvalidCellFluidId;
    int16_t previous_fluid_mass = 0;
    int16_t previous_fluid_temperature = 300;
    bool previous_fluid_is_gas = false;
    uint32_t current_material = 0;
    uint32_t current_flags = 0;
    snt::voxel::CellFluidId current_fluid_type = snt::voxel::kInvalidCellFluidId;
    int16_t current_fluid_mass = 0;
    int16_t current_fluid_temperature = 300;
    bool current_fluid_is_gas = false;
    FluidSimulationLayer layer = FluidSimulationLayer::kSparse;
};

// A future GPU presentation adapter receives only chunk-level invalidations.
// It reads immutable replicated terrain snapshots rather than sharing solver
// buffers with the deterministic simulation runtime.
struct FluidPresentationChunk {
    snt::voxel::ChunkKey chunk;
    FluidSimulationLayer layer = FluidSimulationLayer::kSparse;
    uint64_t tick_index = 0;
    uint32_t fluid_cell_count = 0;
    int64_t total_fluid_mass = 0;
};

// Low-frequency diagnostics make solver-mode and backlog decisions visible
// without placing per-cell logging on the fixed-tick hot path.
struct FluidSimulationTelemetry {
    uint64_t tick_index = 0;
    uint32_t active_cells_before = 0;
    uint32_t active_cells_after = 0;
    uint32_t sparse_cells_processed = 0;
    uint32_t dense_chunks_processed = 0;
    uint32_t dense_cells_processed = 0;
    uint32_t lattice_boltzmann_chunks_processed = 0;
    uint32_t lattice_boltzmann_cells_processed = 0;
    uint32_t accelerator_dispatches = 0;
    uint32_t equilibrium_chunks = 0;
    uint32_t terrain_mutations = 0;
};

class IFluidMutationSink {
public:
    virtual ~IFluidMutationSink() = default;
    virtual void on_fluid_terrain_changed(const FluidTerrainChange& change) = 0;
};

class IFluidPresentationSink {
public:
    virtual ~IFluidPresentationSink() = default;
    virtual void on_fluid_presentation_chunk_dirty(
        const FluidPresentationChunk& chunk) = 0;
};

class IFluidSimulationTelemetrySink {
public:
    virtual ~IFluidSimulationTelemetrySink() = default;
    virtual void on_fluid_simulation_telemetry(
        const FluidSimulationTelemetry& telemetry) = 0;
};

// Terrain writers call this after their own transaction commits. The solver
// then wakes only the affected local neighborhood instead of scanning loaded
// terrain globally.
class IFluidTrigger {
public:
    virtual ~IFluidTrigger() = default;
    virtual void schedule_fluid_after_terrain_mutation(
        std::string_view dimension_id,
        int32_t block_x,
        int32_t block_y,
        int32_t block_z,
        uint64_t source_tick) = 0;
};

}  // namespace snt::game
