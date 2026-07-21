// Game-owned ecosystem regression coverage.

#include "game/simulation/ecosystem_system.h"

#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr uint32_t kSolid = static_cast<uint32_t>(snt::voxel::TF_SOLID);

snt::game::WorldGenConfigSnapshot make_worldgen_config() {
    snt::game::WorldGenConfigSnapshot config;
    const auto add_material = [&config](const char* key, uint32_t flags) {
        snt::game::TerrainMaterialDef material;
        material.key = key;
        material.flags = flags;
        config.materials.push_back(std::move(material));
    };
    add_material("snt:air", 0);
    add_material("snt:dirt", kSolid);
    add_material("snt:sand", kSolid);
    add_material("snt:stone", kSolid);
    add_material("snt:water", 0);
    config.role_keys.air = "snt:air";
    config.role_keys.dirt = "snt:dirt";
    config.role_keys.sand = "snt:sand";
    config.role_keys.stone = "snt:stone";
    config.role_keys.water = "snt:water";
    EXPECT_TRUE(snt::game::finalize_world_gen_config(config));
    return config;
}

void add_active_chunk(snt::voxel::ChunkRegistry& chunks,
                      const snt::voxel::ChunkKey& key,
                      snt::game::TerrainMaterialId surface_material) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(2, 2, 2);
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            chunk.terrain.set_cell(x, 0, z, surface_material, kSolid);
        }
    }
    chunks.set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z,
                     std::move(chunk));
}

snt::game::GameEcosystemConfig quiescent_config() {
    snt::game::GameEcosystemConfig config;
    config.vegetation_growth_rate = 0.0f;
    config.vegetation_decay_rate = 0.0f;
    config.grazing_rate = 0.0f;
    config.herbivore_reproduction_rate = 0.0f;
    config.herbivore_natural_death_rate = 0.0f;
    config.predation_rate = 0.0f;
    config.predator_reproduction_rate = 0.0f;
    config.predator_natural_death_rate = 0.0f;
    config.decomposition_rate = 0.0f;
    config.hunting_pressure_decay = 1.0f;
    config.diffusion_rate = 0.0f;
    config.diffusion_interval_ticks = 0;
    return config;
}

struct RecordingMutationSink final : snt::game::IGameEcosystemMutationSink {
    std::vector<snt::game::GameEcosystemPopulationMutation> mutations;

    void on_ecosystem_population_mutated(
        const snt::game::GameEcosystemPopulationMutation& mutation) override {
        mutations.push_back(mutation);
    }
};

struct RecordingWildProxySink final : snt::game::IGameEcosystemWildProxySink {
    std::vector<snt::game::GameEcosystemWildProxyRebalanceRequest> requests;

    void request_wild_proxy_rebalance(
        const snt::game::GameEcosystemWildProxyRebalanceRequest& request) override {
        requests.push_back(request);
    }
};

struct RecordingFarVisualSink final : snt::game::IGameEcosystemFarVisualSink {
    std::vector<snt::game::GameEcosystemFarVisualRebalanceRequest> requests;

    void request_far_visual_rebalance(
        const snt::game::GameEcosystemFarVisualRebalanceRequest& request) override {
        requests.push_back(request);
    }
};

struct GameplayPolicyProvider final : snt::game::IGameEcosystemEnvironmentProvider {
    bool sample_ecosystem_environment(
        const snt::voxel::ChunkKey& chunk,
        snt::game::GameEcosystemEnvironmentSample& out_sample) const override {
        out_sample.enabled = chunk.dimension_id != "disabled";
        out_sample.rate_multiplier = 0.0f;
        out_sample.is_daytime = false;
        out_sample.water_availability = 0.2f;
        return true;
    }
};

struct FixedInterestProvider final : snt::game::IGameEcosystemInterestProvider {
    std::vector<snt::game::GameEcosystemInterestCenter> centers;

    void collect_ecosystem_interest_centers(
        uint64_t /*current_tick*/,
        std::vector<snt::game::GameEcosystemInterestCenter>& out_centers) const override {
        out_centers.insert(out_centers.end(), centers.begin(), centers.end());
    }
};

bool same_chunk(const snt::voxel::ChunkKey& left, const snt::voxel::ChunkKey& right) {
    return left.dimension_id == right.dimension_id &&
           left.chunk_x == right.chunk_x &&
           left.chunk_y == right.chunk_y &&
           left.chunk_z == right.chunk_z;
}

const snt::game::GameEcosystemWildProxyRebalanceRequest* latest_request_for(
    const std::vector<snt::game::GameEcosystemWildProxyRebalanceRequest>& requests,
    const snt::voxel::ChunkKey& chunk) {
    for (auto it = requests.rbegin(); it != requests.rend(); ++it) {
        if (same_chunk(it->chunk, chunk)) return &*it;
    }
    return nullptr;
}

void expect_same_proxy_plan(
    const std::vector<snt::game::GameEcosystemWildProxyPlan>& expected,
    const std::vector<snt::game::GameEcosystemWildProxyPlan>& actual) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(actual[index].stable_id, expected[index].stable_id);
        EXPECT_EQ(actual[index].species_id, expected[index].species_id);
        EXPECT_EQ(actual[index].role, expected[index].role);
        EXPECT_EQ(actual[index].slot, expected[index].slot);
    }
}

}  // namespace

TEST(GameEcosystemSystemTest, InitializesSurfaceBiomesAndRebalancesWildProxiesAtCadence) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey plains{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey desert{"overworld", 1, 0, 0};
    add_active_chunk(chunks, plains, worldgen.roles.dirt);
    add_active_chunk(chunks, desert, worldgen.roles.sand);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config;
    config.diffusion_interval_ticks = 0;
    config.wild_proxy_rebalance_interval_ticks = 10;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    RecordingMutationSink mutations;
    RecordingWildProxySink wild_proxies;
    FixedInterestProvider interests;
    interests.centers = {{plains}, {desert}};
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_mutation_sink(&mutations);
    ecosystem.set_wild_proxy_sink(&wild_proxies);

    ecosystem.tick(7, snt::game::Season::SPRING);

    ASSERT_EQ(ecosystem.population_cell_count(), 2u);
    const auto* plains_cell = ecosystem.find_population_cell(plains);
    const auto* desert_cell = ecosystem.find_population_cell(desert);
    ASSERT_NE(plains_cell, nullptr);
    ASSERT_NE(desert_cell, nullptr);
    EXPECT_EQ(plains_cell->biome_type, snt::game::ecosystem_biome::kPlains);
    EXPECT_EQ(desert_cell->biome_type, snt::game::ecosystem_biome::kDesert);
    EXPECT_EQ(wild_proxies.requests.size(), 2u);
    EXPECT_TRUE(std::any_of(
        mutations.mutations.begin(), mutations.mutations.end(), [](const auto& mutation) {
            return mutation.kind == snt::game::GameEcosystemPopulationMutationKind::kInitialized &&
                   mutation.source_tick == 7;
        }));

    ecosystem.tick(8, snt::game::Season::SPRING);
    EXPECT_EQ(wild_proxies.requests.size(), 2u);
    ecosystem.tick(10, snt::game::Season::SPRING);
    EXPECT_EQ(wild_proxies.requests.size(), 4u);
}

TEST(GameEcosystemSystemTest, RespectsProviderEnablementAndRatePolicy) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey enabled{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey disabled{"disabled", 0, 0, 0};
    add_active_chunk(chunks, enabled, worldgen.roles.dirt);
    add_active_chunk(chunks, disabled, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    GameplayPolicyProvider policy;
    FixedInterestProvider interests;
    interests.centers = {{enabled}, {disabled}};
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_environment_provider(&policy);

    ecosystem.tick(11, snt::game::Season::SPRING);

    EXPECT_EQ(ecosystem.population_cell_count(), 1u);
    const auto* cell = ecosystem.find_population_cell(enabled);
    ASSERT_NE(cell, nullptr);
    EXPECT_FLOAT_EQ(cell->vegetation_density, 0.5f);
    EXPECT_FLOAT_EQ(cell->herbivore_density, 0.3f);
    EXPECT_FLOAT_EQ(cell->predator_density, 0.1f);
    EXPECT_FLOAT_EQ(cell->water_availability, 0.2f);
    EXPECT_EQ(ecosystem.find_population_cell(disabled), nullptr);
}

TEST(GameEcosystemSystemTest, BuildsStableBiomeLimitedWildProxyPlans) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey plains{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey desert{"overworld", 1, 0, 0};
    const snt::voxel::ChunkKey ocean{"overworld", 2, 0, 0};
    add_active_chunk(chunks, plains, worldgen.roles.dirt);
    add_active_chunk(chunks, desert, worldgen.roles.sand);
    add_active_chunk(chunks, ocean, worldgen.roles.water);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config = quiescent_config();
    config.wild_proxy_rebalance_interval_ticks = 1;
    config.wild_proxy_herbivore_min_density = 0.0f;
    config.wild_proxy_predator_min_density = 0.0f;
    config.max_wild_herbivore_proxies_per_chunk = 2;
    config.max_wild_predator_proxies_per_chunk = 2;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    RecordingWildProxySink wild_proxies;
    FixedInterestProvider interests;
    interests.centers = {{plains}, {desert}, {ocean}};
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_wild_proxy_sink(&wild_proxies);

    ecosystem.tick(1, snt::game::Season::SPRING);

    const auto* plains_request = latest_request_for(wild_proxies.requests, plains);
    const auto* desert_request = latest_request_for(wild_proxies.requests, desert);
    const auto* ocean_request = latest_request_for(wild_proxies.requests, ocean);
    ASSERT_NE(plains_request, nullptr);
    ASSERT_NE(desert_request, nullptr);
    ASSERT_NE(ocean_request, nullptr);
    ASSERT_EQ(plains_request->proxies.size(), 2u);
    for (const auto& proxy : plains_request->proxies) {
        EXPECT_NE(proxy.stable_id, 0u);
        const auto* definition = snt::game::builtin_creature_species().get_species(proxy.species_id);
        ASSERT_NE(definition, nullptr);
        EXPECT_EQ(definition->role, proxy.role);
        EXPECT_NE(std::find(definition->biomes.begin(), definition->biomes.end(),
                            snt::game::ecosystem_biome::kPlains),
                  definition->biomes.end());
    }

    ASSERT_EQ(desert_request->proxies.size(), 2u);
    EXPECT_EQ(desert_request->proxies[0].role, snt::game::CreatureRole::HERBIVORE);
    EXPECT_EQ(desert_request->proxies[0].species_id, 2u);
    EXPECT_EQ(desert_request->proxies[1].role, snt::game::CreatureRole::PREDATOR);
    EXPECT_EQ(desert_request->proxies[1].species_id, 129u);

    ASSERT_EQ(ocean_request->proxies.size(), 1u);
    EXPECT_EQ(ocean_request->proxies.front().role, snt::game::CreatureRole::PREDATOR);
    EXPECT_EQ(ocean_request->proxies.front().species_id, 130u);

    auto* plains_cell = ecosystem.find_population_cell(plains);
    ASSERT_NE(plains_cell, nullptr);
    plains_cell->herbivore_density = 1.0f;
    plains_cell->predator_density = 1.0f;
    ecosystem.tick(2, snt::game::Season::SPRING);
    const auto* saturated_request = latest_request_for(wild_proxies.requests, plains);
    ASSERT_NE(saturated_request, nullptr);
    ASSERT_EQ(saturated_request->proxies.size(), 4u);
    const auto stable_plan = saturated_request->proxies;

    ecosystem.tick(3, snt::game::Season::SPRING);
    const auto* repeated_request = latest_request_for(wild_proxies.requests, plains);
    ASSERT_NE(repeated_request, nullptr);
    expect_same_proxy_plan(stable_plan, repeated_request->proxies);
}

TEST(GameEcosystemSystemTest, DiffusesDeterministicallyAndRecordsHuntPressure) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey source{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey target{"overworld", 1, 0, 0};
    add_active_chunk(chunks, source, worldgen.roles.dirt);
    add_active_chunk(chunks, target, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config = quiescent_config();
    config.diffusion_rate = 0.1f;
    config.diffusion_interval_ticks = 1;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    RecordingMutationSink mutations;
    FixedInterestProvider interests;
    interests.centers = {{source}, {target}};
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_mutation_sink(&mutations);

    ASSERT_TRUE(ecosystem.ensure_population_cell(source, 3));
    ASSERT_TRUE(ecosystem.ensure_population_cell(target, 3));
    auto* source_cell = ecosystem.find_population_cell(source);
    auto* target_cell = ecosystem.find_population_cell(target);
    ASSERT_NE(source_cell, nullptr);
    ASSERT_NE(target_cell, nullptr);
    source_cell->vegetation_density = 0.8f;
    target_cell->vegetation_density = 0.2f;

    ASSERT_TRUE(ecosystem.record_hunt(source, snt::game::CreatureRole::HERBIVORE,
                                      0.25f, 4));
    EXPECT_FLOAT_EQ(source_cell->hunting_pressure_herb, 0.25f);
    EXPECT_TRUE(std::any_of(
        mutations.mutations.begin(), mutations.mutations.end(), [](const auto& mutation) {
            return mutation.kind == snt::game::GameEcosystemPopulationMutationKind::kHuntRecorded &&
                   mutation.source_tick == 4;
        }));

    ecosystem.tick(5, snt::game::Season::SPRING);

    EXPECT_NEAR(source_cell->vegetation_density, 0.74f, 0.000001f);
    EXPECT_NEAR(target_cell->vegetation_density, 0.26f, 0.000001f);
    EXPECT_TRUE(std::any_of(
        mutations.mutations.begin(), mutations.mutations.end(), [](const auto& mutation) {
            return mutation.kind == snt::game::GameEcosystemPopulationMutationKind::kDiffused &&
                   mutation.source_tick == 5;
        }));
}

TEST(GameEcosystemSystemTest, LimitsWildProxiesToInteractionCircleAndDespawnsWhenLeaving) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey center{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey macro_only{"overworld", 1, 0, 0};
    add_active_chunk(chunks, center, worldgen.roles.dirt);
    add_active_chunk(chunks, macro_only, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config = quiescent_config();
    config.macro_horizontal_radius_chunks = 1;
    config.macro_vertical_radius_chunks = 0;
    config.interactive_horizontal_radius_chunks = 0;
    config.interactive_vertical_radius_chunks = 0;
    config.wild_proxy_rebalance_interval_ticks = 100;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    FixedInterestProvider interests;
    interests.centers = {{center}};
    RecordingWildProxySink wild_proxies;
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_wild_proxy_sink(&wild_proxies);

    ecosystem.tick(1, snt::game::Season::SPRING);

    EXPECT_EQ(ecosystem.population_cell_count(), 2u);
    ASSERT_EQ(wild_proxies.requests.size(), 1u);
    EXPECT_TRUE(same_chunk(wild_proxies.requests.front().chunk, center));
    EXPECT_FALSE(wild_proxies.requests.front().proxies.empty());

    interests.centers.clear();
    ecosystem.tick(2, snt::game::Season::SPRING);

    ASSERT_EQ(wild_proxies.requests.size(), 2u);
    EXPECT_TRUE(same_chunk(wild_proxies.requests.back().chunk, center));
    EXPECT_TRUE(wild_proxies.requests.back().proxies.empty());
}

TEST(GameEcosystemSystemTest, ProjectsMacroOnlyChunksAsFarVisualsWithoutWildAgents) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey center{"overworld", 0, 0, 0};
    const snt::voxel::ChunkKey macro_only{"overworld", 2, 0, 0};
    add_active_chunk(chunks, center, worldgen.roles.dirt);
    add_active_chunk(chunks, macro_only, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config = quiescent_config();
    config.macro_horizontal_radius_chunks = 2;
    config.macro_vertical_radius_chunks = 0;
    config.visual_horizontal_radius_chunks = 2;
    config.visual_vertical_radius_chunks = 0;
    config.interactive_horizontal_radius_chunks = 0;
    config.interactive_vertical_radius_chunks = 0;
    config.wild_proxy_rebalance_interval_ticks = 100;
    config.far_visual_rebalance_interval_ticks = 100;
    config.far_visual_herbivore_min_density = 0.0f;
    config.max_far_visual_herbivore_proxies_per_chunk = 1;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    FixedInterestProvider interests;
    interests.centers = {{center}};
    RecordingWildProxySink wild_proxies;
    RecordingFarVisualSink far_visuals;
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_wild_proxy_sink(&wild_proxies);
    ecosystem.set_far_visual_sink(&far_visuals);

    ecosystem.tick(1, snt::game::Season::SPRING);

    ASSERT_EQ(wild_proxies.requests.size(), 1u);
    EXPECT_TRUE(same_chunk(wild_proxies.requests.front().chunk, center));
    const auto* far_request = latest_request_for(far_visuals.requests, macro_only);
    ASSERT_NE(far_request, nullptr);
    EXPECT_FALSE(far_request->proxies.empty());

    interests.centers.clear();
    ecosystem.tick(2, snt::game::Season::SPRING);

    const auto* stopped = latest_request_for(far_visuals.requests, macro_only);
    ASSERT_NE(stopped, nullptr);
    EXPECT_TRUE(stopped->proxies.empty());
}

TEST(GameEcosystemSystemTest, CatchesUpOnlyWhenAPlayerReactivatesMacroCircle) {
    const snt::game::WorldGenConfigSnapshot worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemConfig config = quiescent_config();
    config.vegetation_growth_rate = 0.01f;
    config.macro_horizontal_radius_chunks = 0;
    config.macro_vertical_radius_chunks = 0;
    config.interactive_horizontal_radius_chunks = 0;
    config.interactive_vertical_radius_chunks = 0;
    config.max_macro_catchup_ticks = 100;
    config.max_macro_catchup_substeps = 100;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen, config);
    FixedInterestProvider interests;
    interests.centers = {{chunk}};
    RecordingMutationSink mutations;
    ecosystem.set_interest_provider(&interests);
    ecosystem.set_mutation_sink(&mutations);

    ecosystem.tick(1, snt::game::Season::SPRING);
    const auto* initial = ecosystem.find_population_cell(chunk);
    ASSERT_NE(initial, nullptr);
    const float initial_vegetation = initial->vegetation_density;

    interests.centers.clear();
    ecosystem.tick(10, snt::game::Season::SPRING);
    ASSERT_NE(ecosystem.find_population_cell(chunk), nullptr);
    EXPECT_FLOAT_EQ(ecosystem.find_population_cell(chunk)->vegetation_density,
                    initial_vegetation);

    interests.centers = {{chunk}};
    ecosystem.tick(11, snt::game::Season::SPRING);

    const auto* reactivated = ecosystem.find_population_cell(chunk);
    ASSERT_NE(reactivated, nullptr);
    EXPECT_GT(reactivated->vegetation_density, initial_vegetation);
    EXPECT_EQ(reactivated->last_macro_simulation_tick, 11u);
    EXPECT_TRUE(std::any_of(
        mutations.mutations.begin(), mutations.mutations.end(), [](const auto& mutation) {
            return mutation.kind ==
                       snt::game::GameEcosystemPopulationMutationKind::kMacroCatchUp &&
                   mutation.source_tick == 11;
        }));
}
