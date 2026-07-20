// Deterministic D3Q7 lattice-Boltzmann flow-field solver.
//
// This module owns no chunks and mutates no terrain. The game fluid
// orchestrator supplies an immutable density snapshot, then applies the
// resulting velocity field through its capacity-constrained CA transaction.
// Keeping those concerns separate makes the same kernel usable by a future
// CPU job implementation or Vulkan compute backend.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace snt::game {

inline constexpr int32_t kFluidLbmVelocityScale = 1024;

struct FluidLbmVelocity {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
};

struct FluidLbmCell {
    bool is_solid = false;
    int16_t mass = 0;
};

struct FluidLbmStepConfig {
    // BGK relaxation is represented as an integer fraction so the CPU
    // reference path remains deterministic across authoritative hosts.
    uint16_t relaxation_numerator = 1;
    uint16_t relaxation_denominator = 2;
    int16_t maximum_velocity = 192;
};

struct FluidLbmStepInput {
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t size_z = 0;
    std::span<const FluidLbmCell> cells;
    FluidLbmVelocity gravity;
    FluidLbmStepConfig config;
};

// Per-chunk opaque LBM distributions. The state is invalidated by terrain or
// composition changes outside the LBM layer; it deliberately does not expose
// a terrain reference or any game-specific material identifiers.
struct FluidLbmChunkState {
    int32_t size_x = 0;
    int32_t size_y = 0;
    int32_t size_z = 0;
    std::vector<std::array<int32_t, 7>> distributions;

    void reset() noexcept;
};

class FluidLatticeBoltzmannSolver final {
public:
    // Produces one velocity per input cell. Returns false when the input does
    // not describe a valid dense grid and leaves the output empty.
    [[nodiscard]] bool step(const FluidLbmStepInput& input,
                            FluidLbmChunkState& state,
                            std::vector<FluidLbmVelocity>& out_velocity) const;
};

}  // namespace snt::game
