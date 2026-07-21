// Native creature presentation adapter regression coverage.

#include "game/client/creature_presentation_world.h"

#include "ecs/world.h"

#include <array>

#include <gtest/gtest.h>

namespace {

snt::game::GameCreaturePresentationVisualCatalog make_visuals() {
    snt::game::GameCreaturePresentationVisualCatalog visuals;
    visuals.set_role_visual(snt::game::CreatureRole::HERBIVORE, {
        .mesh = {.handle = {.id = 4}},
        .lod = {
            .simplified_handle = {.id = 8},
            .simplified_detail_distance = 12.0f,
            .cull_distance = 64.0f,
        },
        .model_scale = 1.25f,
    });
    return visuals;
}

snt::game::GameCreaturePresentationState make_creature(uint64_t id) {
    return {
        .entity_id = id,
        .chunk = {"overworld", 0, 0, 0},
        .species_id = 1,
        .role = snt::game::CreatureRole::HERBIVORE,
        .position_x = 3.0f,
        .position_y = 4.0f,
        .position_z = 5.0f,
        .health = 0.8f,
    };
}

}  // namespace

TEST(GameCreaturePresentationWorldTest, UpsertsAndDespawnsOnlyOwnedEcsEntities) {
    snt::ecs::World world;
    snt::game::GameCreaturePresentationWorld presentation(world, make_visuals());
    auto creature = make_creature(41);

    presentation.on_creature_presentation_event({
        .kind = snt::game::GameCreaturePresentationEventKind::kSpawned,
        .source_tick = 1,
        .creature = creature,
    });

    ASSERT_EQ(presentation.creature_count(), 1u);
    const auto entity = presentation.entity_for(41);
    ASSERT_TRUE(entity.has_value());
    ASSERT_TRUE((world.registry().all_of<snt::render::Transform, snt::render::MeshRef,
                                        snt::render::MeshLod,
                                        snt::game::GameCreaturePresentationComponent>(*entity)));
    const auto& transform = world.registry().get<snt::render::Transform>(*entity);
    EXPECT_FLOAT_EQ(transform.position[0], 3.0f);
    EXPECT_FLOAT_EQ(transform.scale[0], 1.25f);
    const auto& lod = world.registry().get<snt::render::MeshLod>(*entity);
    EXPECT_EQ(lod.simplified_handle.id, 8u);

    creature.is_interactive = true;
    creature.is_captive = true;
    creature.is_tamed = true;
    creature.position_x = 9.0f;
    presentation.on_creature_presentation_event({
        .kind = snt::game::GameCreaturePresentationEventKind::kCaptured,
        .source_tick = 2,
        .creature = creature,
    });
    ASSERT_EQ(presentation.entity_for(41), entity);
    EXPECT_FLOAT_EQ(world.registry().get<snt::render::Transform>(*entity).position[0], 9.0f);
    const auto& metadata =
        world.registry().get<snt::game::GameCreaturePresentationComponent>(*entity);
    EXPECT_TRUE(metadata.is_interactive);
    EXPECT_TRUE(metadata.is_captive);
    EXPECT_TRUE(metadata.is_tamed);

    presentation.on_creature_presentation_event({
        .kind = snt::game::GameCreaturePresentationEventKind::kDespawned,
        .source_tick = 3,
        .creature = creature,
    });
    EXPECT_EQ(presentation.creature_count(), 0u);
    EXPECT_FALSE(world.registry().valid(*entity));
}

TEST(GameCreaturePresentationWorldTest, ReconcileRemovesStatesOmittedByReplicationSnapshot) {
    snt::ecs::World world;
    snt::game::GameCreaturePresentationWorld presentation(world, make_visuals());
    const auto first = make_creature(51);
    const auto second = make_creature(52);
    const std::array initial = {first, second};
    presentation.reconcile(initial, 1);
    ASSERT_EQ(presentation.creature_count(), 2u);
    const auto removed_entity = presentation.entity_for(51);
    ASSERT_TRUE(removed_entity.has_value());

    const std::array current = {second};
    presentation.reconcile(current, 2);

    EXPECT_EQ(presentation.creature_count(), 1u);
    EXPECT_FALSE(presentation.entity_for(51).has_value());
    EXPECT_FALSE(world.registry().valid(*removed_entity));
    EXPECT_TRUE(presentation.entity_for(52).has_value());
}
