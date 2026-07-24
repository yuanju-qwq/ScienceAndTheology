// Native ground-loot presentation adapter regression coverage.

#include "game/client/ground_loot_presentation_world.h"

#include "ecs/world.h"

#include <array>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

snt::game::GameGroundLootPresentationVisualCatalog make_visuals() {
    snt::game::GameGroundLootPresentationVisualCatalog visuals;
    visuals.set_default_visual({
        .mesh = {.handle = {.id = 13}},
        .model_scale = 0.3f,
        .vertical_offset_blocks = 0.15f,
    });
    visuals.set_item_visual("special", {
        .mesh = {.handle = {.id = 21}},
        .model_scale = 0.5f,
        .vertical_offset_blocks = 0.2f,
    });
    return visuals;
}

snt::game::replication::GameGroundLootPresentationState make_loot(
    uint64_t id, std::string item_id = "iron") {
    return {
        .loot_id = id,
        .chunk = {"overworld", 0, 0, 0},
        .resource = snt::game::ResourceContentStack::item(std::move(item_id), 3),
        .position_x = 3.0f,
        .position_y = 4.0f,
        .position_z = 5.0f,
        .spawned_tick = 9,
    };
}

}  // namespace

TEST(GameGroundLootPresentationWorldTest, ReconcilesOnlyTransientLootEntities) {
    snt::ecs::World world;
    snt::game::GameGroundLootPresentationWorld presentation(world, make_visuals());
    auto standard = make_loot(41);
    auto special = make_loot(42, "special");
    const std::array initial = {standard, special};

    presentation.reconcile(initial, 1);
    ASSERT_EQ(presentation.loot_count(), 2u);
    const auto standard_entity = presentation.entity_for(41);
    const auto special_entity = presentation.entity_for(42);
    ASSERT_TRUE(standard_entity.has_value());
    ASSERT_TRUE(special_entity.has_value());
    ASSERT_TRUE((world.registry().all_of<snt::render::Transform, snt::render::MeshRef,
                                        snt::game::GameGroundLootPresentationComponent>(
        *standard_entity)));
    const auto& standard_mesh = world.registry().get<snt::render::MeshRef>(*standard_entity);
    EXPECT_EQ(standard_mesh.handle.id, 13u);
    const auto& special_mesh = world.registry().get<snt::render::MeshRef>(*special_entity);
    EXPECT_EQ(special_mesh.handle.id, 21u);
    const auto& transform = world.registry().get<snt::render::Transform>(*special_entity);
    EXPECT_FLOAT_EQ(transform.position[1], 4.2f);
    EXPECT_FLOAT_EQ(transform.scale[0], 0.5f);
    const auto& metadata =
        world.registry().get<snt::game::GameGroundLootPresentationComponent>(*special_entity);
    EXPECT_EQ(metadata.loot_id, 42u);
    EXPECT_EQ(metadata.resource.key.id, "special");

    standard.position_x = 8.0f;
    const std::array current = {standard};
    presentation.reconcile(current, 2);

    EXPECT_EQ(presentation.loot_count(), 1u);
    EXPECT_EQ(presentation.entity_for(41), standard_entity);
    EXPECT_FLOAT_EQ(world.registry().get<snt::render::Transform>(*standard_entity).position[0], 8.0f);
    EXPECT_FALSE(presentation.entity_for(42).has_value());
    EXPECT_FALSE(world.registry().valid(*special_entity));
}
