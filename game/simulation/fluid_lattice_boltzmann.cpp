// Deterministic D3Q7 lattice-Boltzmann flow-field solver implementation.

#include "game/simulation/fluid_lattice_boltzmann.h"

#include <algorithm>
#include <array>
#include <limits>

namespace snt::game {
namespace {

constexpr std::array<std::array<int32_t, 3>, 7> kDirections = {{
    {{0, 0, 0}},
    {{1, 0, 0}}, {{-1, 0, 0}},
    {{0, 1, 0}}, {{0, -1, 0}},
    {{0, 0, 1}}, {{0, 0, -1}},
}};

constexpr std::array<size_t, 7> kOppositeDirections = {{0, 2, 1, 4, 3, 6, 5}};

int32_t clamp_velocity(int64_t value, int16_t maximum_velocity) noexcept {
    const int32_t limit = std::max<int32_t>(1, maximum_velocity);
    return static_cast<int32_t>(std::clamp<int64_t>(value, -limit, limit));
}

int32_t distribution_mass(const std::array<int32_t, 7>& distribution) noexcept {
    int32_t result = 0;
    for (const int32_t value : distribution) result += value;
    return result;
}

void initialize_distribution(int16_t mass, std::array<int32_t, 7>& out) noexcept {
    out.fill(0);
    if (mass <= 0) return;

    const int32_t density = mass;
    out[0] = density / 4;
    for (size_t direction = 1; direction < out.size(); ++direction) {
        out[direction] = density / 8;
    }
    int32_t assigned = distribution_mass(out);
    out[0] += density - assigned;
}

void synchronize_distribution_density(int16_t mass,
                                      std::array<int32_t, 7>& distribution) noexcept {
    if (mass <= 0) {
        distribution.fill(0);
        return;
    }

    const int32_t density = distribution_mass(distribution);
    if (density <= 0) {
        initialize_distribution(mass, distribution);
        return;
    }

    std::array<int32_t, 7> scaled{};
    int32_t assigned = 0;
    for (size_t direction = 1; direction < distribution.size(); ++direction) {
        scaled[direction] = static_cast<int32_t>(
            static_cast<int64_t>(distribution[direction]) * mass / density);
        assigned += scaled[direction];
    }
    scaled[0] = std::max<int32_t>(0, mass - assigned);
    distribution = scaled;
}

std::array<int32_t, 7> collide(const std::array<int32_t, 7>& current,
                                const FluidLbmVelocity& gravity,
                                FluidLbmStepConfig config) noexcept {
    const int32_t density = distribution_mass(current);
    if (density <= 0) return {};

    const uint16_t denominator = std::max<uint16_t>(1, config.relaxation_denominator);
    const uint16_t numerator = std::min(config.relaxation_numerator, denominator);
    const int32_t raw_velocity_x = static_cast<int32_t>(
        static_cast<int64_t>(current[1] - current[2]) * kFluidLbmVelocityScale /
        density) + gravity.x;
    const int32_t raw_velocity_y = static_cast<int32_t>(
        static_cast<int64_t>(current[3] - current[4]) * kFluidLbmVelocityScale /
        density) + gravity.y;
    const int32_t raw_velocity_z = static_cast<int32_t>(
        static_cast<int64_t>(current[5] - current[6]) * kFluidLbmVelocityScale /
        density) + gravity.z;
    const FluidLbmVelocity velocity{
        .x = static_cast<int16_t>(clamp_velocity(raw_velocity_x, config.maximum_velocity)),
        .y = static_cast<int16_t>(clamp_velocity(raw_velocity_y, config.maximum_velocity)),
        .z = static_cast<int16_t>(clamp_velocity(raw_velocity_z, config.maximum_velocity)),
    };

    std::array<int32_t, 7> equilibrium{};
    int32_t directional_total = 0;
    for (size_t direction = 1; direction < equilibrium.size(); ++direction) {
        const int32_t dot = kDirections[direction][0] * velocity.x +
                            kDirections[direction][1] * velocity.y +
                            kDirections[direction][2] * velocity.z;
        const int32_t base = density / 8;
        const int32_t bias = static_cast<int32_t>(
            static_cast<int64_t>(density) * 3 * dot /
            (8 * kFluidLbmVelocityScale));
        equilibrium[direction] = std::max<int32_t>(0, base + bias);
        directional_total += equilibrium[direction];
    }
    equilibrium[0] = std::max<int32_t>(0, density - directional_total);

    std::array<int32_t, 7> post_collision{};
    int32_t assigned = 0;
    for (size_t direction = 1; direction < post_collision.size(); ++direction) {
        const int64_t delta = static_cast<int64_t>(equilibrium[direction]) -
                              current[direction];
        post_collision[direction] = std::max<int32_t>(0, current[direction] +
            static_cast<int32_t>(delta * numerator / denominator));
        assigned += post_collision[direction];
    }
    post_collision[0] = std::max<int32_t>(0, density - assigned);
    return post_collision;
}

}  // namespace

void FluidLbmChunkState::reset() noexcept {
    size_x = 0;
    size_y = 0;
    size_z = 0;
    distributions.clear();
}

bool FluidLatticeBoltzmannSolver::step(
    const FluidLbmStepInput& input, FluidLbmChunkState& state,
    std::vector<FluidLbmVelocity>& out_velocity) const {
    out_velocity.clear();
    if (input.size_x < 3 || input.size_y < 3 || input.size_z < 3) return false;

    const int64_t expected_count = static_cast<int64_t>(input.size_x) * input.size_y *
                                   input.size_z;
    if (expected_count <= 0 ||
        expected_count != static_cast<int64_t>(input.cells.size())) {
        return false;
    }
    const size_t cell_count = input.cells.size();
    const bool dimensions_changed = state.size_x != input.size_x ||
                                    state.size_y != input.size_y ||
                                    state.size_z != input.size_z ||
                                    state.distributions.size() != cell_count;
    if (dimensions_changed) {
        state.size_x = input.size_x;
        state.size_y = input.size_y;
        state.size_z = input.size_z;
        state.distributions.assign(cell_count, {});
        for (size_t index = 0; index < cell_count; ++index) {
            if (!input.cells[index].is_solid) {
                initialize_distribution(input.cells[index].mass,
                                        state.distributions[index]);
            }
        }
    } else {
        for (size_t index = 0; index < cell_count; ++index) {
            if (input.cells[index].is_solid) {
                state.distributions[index].fill(0);
            } else {
                synchronize_distribution_density(input.cells[index].mass,
                                                 state.distributions[index]);
            }
        }
    }

    const auto index_of = [size_y = input.size_y, size_z = input.size_z,
                           size_x = input.size_x](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>((y * size_z + z) * size_x + x);
    };
    const auto is_inside = [size_x = input.size_x, size_y = input.size_y,
                            size_z = input.size_z](int32_t x, int32_t y, int32_t z) {
        return x >= 0 && x < size_x && y >= 0 && y < size_y && z >= 0 && z < size_z;
    };

    std::vector<std::array<int32_t, 7>> streamed(cell_count);
    for (int32_t y = 0; y < input.size_y; ++y) {
        for (int32_t z = 0; z < input.size_z; ++z) {
            for (int32_t x = 0; x < input.size_x; ++x) {
                const size_t source_index = index_of(x, y, z);
                if (input.cells[source_index].is_solid) continue;
                const std::array<int32_t, 7> post_collision = collide(
                    state.distributions[source_index], input.gravity, input.config);
                streamed[source_index][0] += post_collision[0];
                for (size_t direction = 1; direction < post_collision.size(); ++direction) {
                    const int32_t target_x = x + kDirections[direction][0];
                    const int32_t target_y = y + kDirections[direction][1];
                    const int32_t target_z = z + kDirections[direction][2];
                    if (!is_inside(target_x, target_y, target_z)) {
                        streamed[source_index][kOppositeDirections[direction]] +=
                            post_collision[direction];
                        continue;
                    }
                    const size_t target_index = index_of(target_x, target_y, target_z);
                    if (input.cells[target_index].is_solid) {
                        streamed[source_index][kOppositeDirections[direction]] +=
                            post_collision[direction];
                        continue;
                    }
                    streamed[target_index][direction] += post_collision[direction];
                }
            }
        }
    }

    state.distributions = std::move(streamed);
    out_velocity.resize(cell_count);
    for (size_t index = 0; index < cell_count; ++index) {
        if (input.cells[index].is_solid) continue;
        const std::array<int32_t, 7>& distribution = state.distributions[index];
        const int32_t density = distribution_mass(distribution);
        if (density <= 0) continue;
        out_velocity[index] = {
            .x = static_cast<int16_t>(clamp_velocity(
                static_cast<int64_t>(distribution[1] - distribution[2]) *
                    kFluidLbmVelocityScale / density,
                input.config.maximum_velocity)),
            .y = static_cast<int16_t>(clamp_velocity(
                static_cast<int64_t>(distribution[3] - distribution[4]) *
                    kFluidLbmVelocityScale / density,
                input.config.maximum_velocity)),
            .z = static_cast<int16_t>(clamp_velocity(
                static_cast<int64_t>(distribution[5] - distribution[6]) *
                    kFluidLbmVelocityScale / density,
                input.config.maximum_velocity)),
        };
    }
    return true;
}

}  // namespace snt::game
