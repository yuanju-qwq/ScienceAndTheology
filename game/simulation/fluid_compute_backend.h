// Optional accelerator contract for dense fluid kernels.
//
// A Vulkan/Metal/D3D compute implementation can live outside the shared game
// simulation target. It receives immutable value snapshots and returns a
// velocity field; the GameFluidSystem still validates and commits every mass
// transfer on its deterministic CA authority path.

#pragma once

#include "game/simulation/fluid_lattice_boltzmann.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/terrain_data.h"

#include <cstdint>
#include <span>
#include <vector>

namespace snt::game {

enum class FluidComputeKernel : uint8_t {
    kDenseCellularAutomaton = 0,
    kLatticeBoltzmann = 1,
};

// Full per-cell input for a compute kernel. The solver keeps material and
// terrain ownership on the CPU; a backend only observes collision-relevant
// fluid values and returns a candidate result for validation.
struct FluidComputeCell {
    bool is_solid = false;
    snt::voxel::CellFluidId fluid_type = snt::voxel::kInvalidCellFluidId;
    int16_t mass = 0;
    int16_t temperature = 300;
    bool is_gas = false;
};

struct FluidComputeChunkRequest {
    snt::voxel::ChunkKey chunk;
    uint64_t tick_index = 0;
    FluidComputeKernel kernel = FluidComputeKernel::kLatticeBoltzmann;
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t size_z = 0;
    std::span<const FluidComputeCell> cells;
    FluidLbmVelocity gravity;
    FluidLbmStepConfig lattice_boltzmann_config;
};

struct FluidComputeChunkResult {
    // For a CA dispatch, this contains exactly size_x * size_y * size_z
    // candidate fluid states. The authoritative adapter must validate mass,
    // composition, and capacity before committing any result.
    std::vector<FluidComputeCell> cellular_automaton_cells;
    // For an LBM dispatch, this contains exactly size_x * size_y * size_z
    // velocities. An incomplete result is rejected and the CPU reference
    // solver runs for the tick instead.
    std::vector<FluidLbmVelocity> velocity_field;
};

class IFluidComputeBackend {
public:
    virtual ~IFluidComputeBackend() = default;

    [[nodiscard]] virtual bool supports(FluidComputeKernel kernel) const noexcept = 0;
    // Authoritative use is opt-in because most floating-point GPU paths are
    // not bitwise deterministic between vendors. Presentation-only adapters
    // should return false and use IFluidPresentationSink instead.
    [[nodiscard]] virtual bool produces_deterministic_results() const noexcept = 0;
    [[nodiscard]] virtual bool try_dispatch(const FluidComputeChunkRequest& request,
                                            FluidComputeChunkResult& result) = 0;
};

}  // namespace snt::game
