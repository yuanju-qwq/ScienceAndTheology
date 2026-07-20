// Regression coverage for the hybrid game-owned fluid solver.

#include "game/simulation/game_fluid_system.h"
#include "game/simulation/fluid_lattice_boltzmann.h"

#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kSolid = static_cast<uint32_t>(snt::voxel::TF_SOLID);
constexpr snt::voxel::CellFluidId kWater = 7;

void add_empty_chunk(snt::voxel::ChunkRegistry& chunks) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));
}

snt::game::WorldGenConfigSnapshot make_worldgen_config() {
    snt::game::WorldGenConfigSnapshot config;
    config.materials.push_back({.key = "snt:air"});
    config.role_keys.air = "snt:air";
    EXPECT_TRUE(snt::game::finalize_world_gen_config(config));
    return config;
}

struct RecordingMutationSink final : snt::game::IFluidMutationSink {
    std::vector<snt::game::FluidTerrainChange> changes;

    void on_fluid_terrain_changed(
        const snt::game::FluidTerrainChange& change) override {
        changes.push_back(change);
    }
};

struct RecordingPresentationSink final : snt::game::IFluidPresentationSink {
    std::vector<snt::game::FluidPresentationChunk> chunks;

    void on_fluid_presentation_chunk_dirty(
        const snt::game::FluidPresentationChunk& chunk) override {
        chunks.push_back(chunk);
    }
};

struct RecordingComputeBackend final : snt::game::IFluidComputeBackend {
    uint32_t dispatch_count = 0;

    bool supports(snt::game::FluidComputeKernel kernel) const noexcept override {
        return kernel == snt::game::FluidComputeKernel::kLatticeBoltzmann;
    }

    bool produces_deterministic_results() const noexcept override {
        return true;
    }

    bool try_dispatch(const snt::game::FluidComputeChunkRequest& request,
                      snt::game::FluidComputeChunkResult& result) override {
        ++dispatch_count;
        result.velocity_field.assign(request.cells.size(), {});
        return true;
    }
};

int64_t fluid_mass_total(const snt::voxel::VoxelChunk& chunk) {
    int64_t result = 0;
    for (const snt::voxel::TerrainCell& cell : chunk.terrain.cells) {
        result += cell.fluid_mass;
    }
    return result;
}

}  // namespace

TEST(FluidLatticeBoltzmannSolverTest, ProducesGravityBiasedVelocityField) {
    constexpr int32_t kSize = 3;
    std::vector<snt::game::FluidLbmCell> cells(kSize * kSize * kSize);
    const auto index_of = [](int32_t x, int32_t y, int32_t z) {
        return static_cast<size_t>((y * kSize + z) * kSize + x);
    };
    cells[index_of(1, 1, 1)].mass = 1000;

    snt::game::FluidLatticeBoltzmannSolver solver;
    snt::game::FluidLbmChunkState state;
    std::vector<snt::game::FluidLbmVelocity> velocity;
    ASSERT_TRUE(solver.step({.size_x = kSize,
                             .size_y = kSize,
                             .size_z = kSize,
                             .cells = cells,
                             .gravity = {.x = 96, .y = 0, .z = 0},
                             .config = {}},
                            state, velocity));
    ASSERT_EQ(velocity.size(), cells.size());
    EXPECT_GT(velocity[index_of(2, 1, 1)].x, 0);
}

TEST(GameFluidSystemTest, SparseLayerMovesFluidDownwardAndEmitsTerrainChanges) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 64,
                           .max_dense_chunks_per_tick = 1,
                           .telemetry_interval_ticks = 0});
    RecordingMutationSink mutations;
    RecordingPresentationSink presentation;
    system.set_mutation_sink(&mutations);
    system.set_presentation_sink(&presentation);

    ASSERT_EQ(system.inject_fluid("overworld", 4, 4, 4, kWater, 1000), 1000);
    system.tick(1);

    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->terrain.cell_at(4, 4, 4).fluid_mass, 0);
    EXPECT_EQ(chunk->terrain.cell_at(4, 3, 4).fluid_type, kWater);
    EXPECT_EQ(chunk->terrain.cell_at(4, 3, 4).fluid_mass, 1000);
    EXPECT_GE(mutations.changes.size(), 3u);
    ASSERT_EQ(presentation.chunks.size(), 1u);
    EXPECT_EQ(presentation.chunks.front().layer,
              snt::game::FluidSimulationLayer::kSparse);
    EXPECT_EQ(presentation.chunks.front().total_fluid_mass, 1000);
    EXPECT_EQ(system.last_telemetry().sparse_cells_processed, 1u);
}

TEST(GameFluidSystemTest, DenseChunkUsesCellularAutomatonAfterThreshold) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 4,
                           .max_dense_chunks_per_tick = 1,
                           .telemetry_interval_ticks = 0});

    for (int32_t x = 5; x < 9; ++x) {
        ASSERT_EQ(system.inject_fluid("overworld", x, 5, 5, kWater, 1000), 1000);
    }
    system.tick(1);

    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    for (int32_t x = 5; x < 9; ++x) {
        EXPECT_EQ(chunk->terrain.cell_at(x, 5, 5).fluid_mass, 0);
        EXPECT_EQ(chunk->terrain.cell_at(x, 4, 5).fluid_mass, 1000);
    }
    EXPECT_EQ(system.last_telemetry().dense_chunks_processed, 1u);
    EXPECT_EQ(system.dense_chunk_count(), 1u);
}

TEST(GameFluidSystemTest, DenseSinglePhaseChunkUsesLatticeBoltzmannGuidedCa) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 4,
                           .max_dense_chunks_per_tick = 1,
                           .lattice_boltzmann_activation_cells_per_chunk = 4,
                           .lattice_boltzmann_min_cell_mass = 1,
                           .telemetry_interval_ticks = 0});
    RecordingPresentationSink presentation;
    system.set_presentation_sink(&presentation);

    for (int32_t x = 5; x < 9; ++x) {
        ASSERT_EQ(system.inject_fluid("overworld", x, 5, 5, kWater, 1000), 1000);
    }
    system.tick(1);

    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(fluid_mass_total(*chunk), 4000);
    EXPECT_EQ(system.last_telemetry().lattice_boltzmann_chunks_processed, 1u);
    EXPECT_EQ(system.last_telemetry().lattice_boltzmann_cells_processed,
              static_cast<uint32_t>(chunk->terrain.cells.size()));
    ASSERT_EQ(presentation.chunks.size(), 1u);
    EXPECT_EQ(presentation.chunks.front().layer,
              snt::game::FluidSimulationLayer::kLatticeBoltzmann);
}

TEST(GameFluidSystemTest, DeterministicComputeBackendCanSupplyLbmVelocityField) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 4,
                           .max_dense_chunks_per_tick = 1,
                           .lattice_boltzmann_activation_cells_per_chunk = 4,
                           .lattice_boltzmann_min_cell_mass = 1,
                           .allow_deterministic_compute_backend = true,
                           .telemetry_interval_ticks = 0});
    RecordingComputeBackend backend;
    system.set_compute_backend(&backend);
    for (int32_t x = 5; x < 9; ++x) {
        ASSERT_EQ(system.inject_fluid("overworld", x, 5, 5, kWater, 1000), 1000);
    }

    system.tick(1);

    EXPECT_EQ(backend.dispatch_count, 1u);
    EXPECT_EQ(system.last_telemetry().accelerator_dispatches, 1u);
    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(fluid_mass_total(*chunk), 4000);
}

TEST(GameFluidSystemTest, DenseGasFallsBackToCellularAutomaton) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 4,
                           .max_dense_chunks_per_tick = 1,
                           .lattice_boltzmann_activation_cells_per_chunk = 4,
                           .lattice_boltzmann_min_cell_mass = 1,
                           .telemetry_interval_ticks = 0});
    for (int32_t x = 5; x < 9; ++x) {
        ASSERT_EQ(system.inject_fluid("overworld", x, 5, 5, kWater, 1000,
                                      300, true), 1000);
    }

    system.tick(1);

    EXPECT_EQ(system.last_telemetry().lattice_boltzmann_chunks_processed, 0u);
    EXPECT_EQ(system.last_telemetry().dense_chunks_processed, 1u);
    const auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    EXPECT_EQ(fluid_mass_total(*chunk), 4000);
}

TEST(GameFluidSystemTest, StableSealedFluidPromotesToEquilibriumAggregate) {
    snt::voxel::ChunkRegistry chunks;
    add_empty_chunk(chunks);
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::game::GameFluidSystem system(
        chunks, worldgen, {.max_sparse_cells_per_tick = 32,
                           .dense_activation_cells_per_chunk = 64,
                           .max_dense_chunks_per_tick = 1,
                           .telemetry_interval_ticks = 0});
    auto* chunk = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(chunk, nullptr);
    for (const auto& offset : std::array<std::array<int32_t, 3>, 6>{{
             {{1, 0, 0}}, {{-1, 0, 0}}, {{0, 1, 0}},
             {{0, -1, 0}}, {{0, 0, 1}}, {{0, 0, -1}},
         }}) {
        chunk->terrain.set_cell(12 + offset[0], 12 + offset[1], 12 + offset[2],
                                1, kSolid);
    }

    ASSERT_EQ(system.inject_fluid("overworld", 12, 12, 12, kWater, 800), 800);
    system.tick(1);

    const snt::voxel::ChunkKey key{"overworld", 0, 0, 0};
    ASSERT_EQ(system.active_cell_count(), 0u);
    ASSERT_EQ(system.equilibrium_chunk_count(), 1u);
    const snt::game::FluidEquilibriumSummary* summary =
        system.find_equilibrium_summary(key);
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(summary->fluid_type, kWater);
    EXPECT_EQ(summary->fluid_cell_count, 1u);
    EXPECT_EQ(summary->total_fluid_mass, 800);
}
