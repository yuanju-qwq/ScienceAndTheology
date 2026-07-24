// Near-field wild creature projection and interaction regression coverage.

#include "game/simulation/wild_creature_system.h"

#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

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

struct RecordingPresentationSink final : snt::game::IGameCreaturePresentationSink {
    std::vector<snt::game::GameCreaturePresentationEvent> events;

    void on_creature_presentation_event(
        const snt::game::GameCreaturePresentationEvent& event) override {
        events.push_back(event);
    }
};

snt::game::GameEcosystemWildProxyRebalanceRequest make_request(
    const snt::voxel::ChunkKey& chunk, uint64_t tick, uint64_t entity_id) {
    return {
        .chunk = chunk,
        .source_tick = tick,
        .proxies = {{
            .stable_id = entity_id,
            .species_id = 1,
            .role = snt::game::CreatureRole::HERBIVORE,
            .slot = 0,
        }},
    };
}

snt::game::CaptiveCreature make_tamed_captive(float x, float z) {
    return {
        .species_id = 1,
        .role = snt::game::CreatureRole::HERBIVORE,
        .age_stage = snt::game::CreatureAgeStage::ADULT,
        .pos_x = x,
        .pos_y = 1.0f,
        .pos_z = z,
        .wander_target_x = x,
        .wander_target_y = 1.0f,
        .wander_target_z = z,
        .bounds_min_x = 0,
        .bounds_min_y = 1,
        .bounds_min_z = 0,
        .bounds_max_x = 1,
        .bounds_max_y = 1,
        .bounds_max_z = 1,
        .health = 1.0f,
        .tame_progress = 1.0f,
        .is_tamed = true,
    };
}

}  // namespace

TEST(GameWildCreatureSystemTest, ReconcilesStableProxiesAndRecordsHuntOnKill) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    ASSERT_TRUE(ecosystem.ensure_population_cell(chunk, 1));
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species());
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    wildlife.request_wild_proxy_rebalance(make_request(chunk, 2, 77));
    ASSERT_EQ(wildlife.wild_creature_count(), 1u);
    ASSERT_EQ(presentation.events.size(), 1u);
    EXPECT_EQ(presentation.events.front().kind,
              snt::game::GameCreaturePresentationEventKind::kSpawned);

    const auto attack = wildlife.apply_damage(77, 10.0f, 3);
    EXPECT_TRUE(attack.hit);
    EXPECT_TRUE(attack.killed);
    EXPECT_EQ(wildlife.wild_creature_count(), 0u);
    const auto* population = ecosystem.find_population_cell(chunk);
    ASSERT_NE(population, nullptr);
    EXPECT_FLOAT_EQ(population->hunting_pressure_herb,
                    ecosystem.config().hunting_kill_contribution);

    wildlife.request_wild_proxy_rebalance(make_request(chunk, 4, 77));
    EXPECT_EQ(wildlife.wild_creature_count(), 0u);
    wildlife.request_wild_proxy_rebalance(make_request(chunk, 123, 77));
    EXPECT_EQ(wildlife.wild_creature_count(), 1u);
}

TEST(GameWildCreatureSystemTest, PromotesFarVisualToInteractiveWildCreature) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    ASSERT_TRUE(ecosystem.ensure_population_cell(chunk, 1));
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species());
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    wildlife.request_far_visual_rebalance(make_request(chunk, 2, 91));
    ASSERT_EQ(wildlife.far_visual_creature_count(), 1u);
    EXPECT_EQ(wildlife.wild_creature_count(), 0u);
    EXPECT_FALSE(wildlife.apply_damage(91, 1.0f, 3).hit);
    ASSERT_EQ(presentation.events.size(), 1u);
    EXPECT_FALSE(presentation.events.front().creature.is_interactive);

    wildlife.request_wild_proxy_rebalance(make_request(chunk, 4, 91));
    EXPECT_EQ(wildlife.far_visual_creature_count(), 0u);
    ASSERT_EQ(wildlife.wild_creature_count(), 1u);
    const auto promoted = wildlife.find_wild_creature(91);
    ASSERT_TRUE(promoted.has_value());
    EXPECT_TRUE(promoted->is_interactive);
    ASSERT_EQ(presentation.events.size(), 2u);
    EXPECT_EQ(presentation.events.back().kind,
              snt::game::GameCreaturePresentationEventKind::kSpawned);
    EXPECT_TRUE(presentation.events.back().creature.is_interactive);
}

TEST(GameWildCreatureSystemTest, CapturesWildCreatureAndCompletesTamingThroughFeeding) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    ASSERT_TRUE(ecosystem.ensure_population_cell(chunk, 1));
    const float original_density = ecosystem.find_population_cell(chunk)->herbivore_density;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species());
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);
    wildlife.request_wild_proxy_rebalance(make_request(chunk, 2, 88));
    const auto wild = wildlife.find_wild_creature(88);
    ASSERT_TRUE(wild.has_value());

    const auto captured = wildlife.capture_wild_creature({
        .wild_entity_id = 88,
        .captive_chunk = chunk,
        .pen_bounds = {
            .min_x = -16, .min_y = -16, .min_z = -16,
            .max_x = 48, .max_y = 48, .max_z = 48,
        },
    }, 3);
    ASSERT_TRUE(captured) << captured.error().format();
    ASSERT_TRUE(captured->captured);
    EXPECT_EQ(wildlife.wild_creature_count(), 0u);
    const auto* population = ecosystem.find_population_cell(chunk);
    ASSERT_NE(population, nullptr);
    EXPECT_FLOAT_EQ(population->herbivore_density,
                    original_density - ecosystem.config().wild_capture_density_contribution);
    const auto* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_TRUE(sidecar->has_captive_creatures);
    ASSERT_EQ(sidecar->captive_creatures.size(), 1u);
    EXPECT_FALSE(sidecar->captive_creatures.front().is_tamed);

    const auto first_feed = wildlife.feed_captive_creature(captured->captive_entity_id, 0.6f, 4);
    ASSERT_TRUE(first_feed) << first_feed.error().format();
    EXPECT_TRUE(first_feed->fed);
    EXPECT_FALSE(first_feed->became_tamed);
    const auto second_feed = wildlife.feed_captive_creature(captured->captive_entity_id, 0.5f, 5);
    ASSERT_TRUE(second_feed) << second_feed.error().format();
    EXPECT_TRUE(second_feed->became_tamed);
    EXPECT_TRUE(sidecar->captive_creatures.front().is_tamed);
}

TEST(GameWildCreatureSystemTest, AdvancesCaptiveBreedingBirthAndGrowthLifecycle) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    snt::game::GameWildCreatureConfig config;
    config.captive_wander_interval_ticks = 0;
    config.captive_gestation_ticks = 2;
    config.captive_growth_ticks = 3;
    config.captive_breed_cooldown_ticks = 20;
    config.captive_presentation_interval_ticks = 1;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species(), config);
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    sidecars.set(chunk, {});
    auto* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    sidecar->has_captive_creatures = true;
    sidecar->captive_creatures = {
        make_tamed_captive(0.5f, 0.5f),
        make_tamed_captive(1.5f, 1.5f),
    };

    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 1});
    ASSERT_EQ(sidecar->captive_creatures.size(), 2u);
    const uint64_t mother_id = sidecar->captive_creatures[0].runtime_id;
    ASSERT_NE(mother_id, 0u);
    const auto breeding = wildlife.feed_captive_creature(mother_id, 0.1f, 2);
    ASSERT_TRUE(breeding) << breeding.error().format();
    EXPECT_TRUE(breeding->fed);
    EXPECT_TRUE(breeding->breeding_started);
    EXPECT_NE(breeding->partner_entity_id, 0u);
    EXPECT_EQ(breeding->gestation_end_tick, 4u);
    EXPECT_TRUE(sidecar->captive_creatures[0].is_pregnant);

    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 3});
    EXPECT_EQ(sidecar->captive_creatures.size(), 2u);
    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 4});
    ASSERT_EQ(sidecar->captive_creatures.size(), 3u);
    const auto& baby = sidecar->captive_creatures.back();
    EXPECT_EQ(baby.age_stage, snt::game::CreatureAgeStage::BABY);
    EXPECT_TRUE(baby.is_tamed);
    EXPECT_EQ(baby.grow_up_tick, 7);
    const auto baby_state = wildlife.find_captive_creature(baby.runtime_id);
    ASSERT_TRUE(baby_state.has_value());
    EXPECT_EQ(baby_state->age_stage, snt::game::CreatureAgeStage::BABY);

    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 7});
    EXPECT_EQ(sidecar->captive_creatures.back().age_stage,
              snt::game::CreatureAgeStage::ADULT);
    const auto matured_state = wildlife.find_captive_creature(baby.runtime_id);
    ASSERT_TRUE(matured_state.has_value());
    EXPECT_EQ(matured_state->age_stage, snt::game::CreatureAgeStage::ADULT);

    bool saw_breeding = false;
    bool saw_birth = false;
    bool saw_maturity = false;
    for (const auto& event : presentation.events) {
        saw_breeding = saw_breeding || event.kind ==
            snt::game::GameCreaturePresentationEventKind::kBreedingStarted;
        saw_birth = saw_birth || event.kind ==
            snt::game::GameCreaturePresentationEventKind::kBorn;
        saw_maturity = saw_maturity || event.kind ==
            snt::game::GameCreaturePresentationEventKind::kMatured;
    }
    EXPECT_TRUE(saw_breeding);
    EXPECT_TRUE(saw_birth);
    EXPECT_TRUE(saw_maturity);
}

TEST(GameWildCreatureSystemTest, KeepsCaptiveWanderingInsideWalkablePenCells) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    snt::game::GameWildCreatureConfig config;
    config.captive_wander_interval_ticks = 1;
    config.captive_wander_target_attempts = 16;
    config.captive_presentation_interval_ticks = 1;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species(), config);
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    sidecars.set(chunk, {});
    auto* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    sidecar->has_captive_creatures = true;
    sidecar->captive_creatures = {make_tamed_captive(0.5f, 0.5f)};

    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 1});
    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 2});
    const auto& captive = sidecar->captive_creatures.front();
    EXPECT_GE(captive.pos_x, 0.0f);
    EXPECT_LT(captive.pos_x, 2.0f);
    EXPECT_GE(captive.pos_z, 0.0f);
    EXPECT_LT(captive.pos_z, 2.0f);
    EXPECT_GE(captive.wander_target_x, 0.0f);
    EXPECT_LT(captive.wander_target_x, 2.0f);
    EXPECT_GE(captive.wander_target_z, 0.0f);
    EXPECT_LT(captive.wander_target_z, 2.0f);

    bool saw_update = false;
    for (const auto& event : presentation.events) {
        saw_update = saw_update || event.kind ==
            snt::game::GameCreaturePresentationEventKind::kUpdated;
    }
    EXPECT_TRUE(saw_update);
}

TEST(GameWildCreatureSystemTest, ReservesBirthCapacityBeforeStartingBreeding) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    snt::game::GameWildCreatureConfig config;
    config.max_captive_creatures_per_chunk = 2;
    config.captive_wander_interval_ticks = 0;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species(), config);

    sidecars.set(chunk, {});
    auto* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    sidecar->has_captive_creatures = true;
    sidecar->captive_creatures = {
        make_tamed_captive(0.5f, 0.5f),
        make_tamed_captive(1.5f, 1.5f),
    };
    wildlife.tick_captive_creatures({.chunk = chunk, .source_tick = 1});

    const auto breeding = wildlife.feed_captive_creature(
        sidecar->captive_creatures.front().runtime_id, 0.1f, 2);
    ASSERT_TRUE(breeding) << breeding.error().format();
    EXPECT_TRUE(breeding->fed);
    EXPECT_FALSE(breeding->breeding_started);
    EXPECT_FALSE(sidecar->captive_creatures.front().is_pregnant);
}

TEST(GameWildCreatureSystemTest, MovesInteractivePredatorsTowardNearbyHerbivores) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    snt::game::GameWildCreatureConfig config;
    config.wild_wander_interval_ticks = 0;
    config.wild_presentation_interval_ticks = 1;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species(), config);
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    wildlife.request_wild_proxy_rebalance({
        .chunk = chunk,
        .source_tick = 1,
        .proxies = {{
            {.stable_id = 1, .species_id = 1,
             .role = snt::game::CreatureRole::HERBIVORE, .slot = 0},
            {.stable_id = 2, .species_id = 129,
             .role = snt::game::CreatureRole::PREDATOR, .slot = 0},
        }},
    });
    const auto herbivore_before = wildlife.find_wild_creature(1);
    ASSERT_TRUE(herbivore_before.has_value());
    const auto predator_before = wildlife.find_wild_creature(2);
    ASSERT_TRUE(predator_before.has_value());
    ASSERT_LT(predator_before->position_x, herbivore_before->position_x);

    const float separation_before = herbivore_before->position_x -
        predator_before->position_x;

    wildlife.tick_interactive_wild_creatures({.chunk = chunk, .source_tick = 2});
    const auto herbivore_after = wildlife.find_wild_creature(1);
    ASSERT_TRUE(herbivore_after.has_value());
    const auto predator_after = wildlife.find_wild_creature(2);
    ASSERT_TRUE(predator_after.has_value());
    EXPECT_GT(herbivore_after->position_x, herbivore_before->position_x);
    EXPECT_GT(predator_after->position_x, predator_before->position_x);
    EXPECT_LT(herbivore_after->position_x - predator_after->position_x,
              separation_before);

    bool saw_update = false;
    for (const auto& event : presentation.events) {
        saw_update = saw_update || event.kind ==
            snt::game::GameCreaturePresentationEventKind::kUpdated;
    }
    EXPECT_TRUE(saw_update);
}

TEST(GameWildCreatureSystemTest, DoesNotAdvanceFarVisualWildCreatures) {
    const auto worldgen = make_worldgen_config();
    snt::voxel::ChunkRegistry chunks;
    const snt::voxel::ChunkKey chunk{"overworld", 0, 0, 0};
    add_active_chunk(chunks, chunk, worldgen.roles.dirt);
    snt::game::GameChunkSidecarRegistry sidecars;
    snt::game::GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    snt::game::GameWildCreatureConfig config;
    config.wild_wander_interval_ticks = 0;
    config.wild_presentation_interval_ticks = 1;
    snt::game::GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, snt::game::builtin_creature_species(), config);
    RecordingPresentationSink presentation;
    wildlife.set_presentation_sink(&presentation);

    wildlife.request_far_visual_rebalance({
        .chunk = chunk,
        .source_tick = 1,
        .proxies = {{
            {.stable_id = 1, .species_id = 1,
             .role = snt::game::CreatureRole::HERBIVORE, .slot = 0},
            {.stable_id = 2, .species_id = 129,
             .role = snt::game::CreatureRole::PREDATOR, .slot = 0},
        }},
    });
    ASSERT_EQ(wildlife.wild_creature_count(), 0u);
    ASSERT_EQ(wildlife.far_visual_creature_count(), 2u);
    presentation.events.clear();

    wildlife.tick_interactive_wild_creatures({.chunk = chunk, .source_tick = 2});

    EXPECT_EQ(wildlife.wild_creature_count(), 0u);
    EXPECT_EQ(wildlife.far_visual_creature_count(), 2u);
    EXPECT_TRUE(presentation.events.empty());
}
