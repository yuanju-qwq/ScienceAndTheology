// Native remote-player avatar presentation regression coverage.

#include "game/client/remote_player_presentation_world.h"

#include "ecs/world.h"
#include "game/player/player_identity.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

snt::game::PlayerIdentity make_identity(std::string display_name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(display_name));
    return identity ? std::move(*identity) : snt::game::PlayerIdentity{};
}

snt::game::replication::GameRemotePlayerState make_remote_player(
    uint64_t guid, float feet_x, int16_t yaw_centidegrees, uint64_t source_tick) {
    snt::game::replication::GameRemotePlayerState result{
        .entity_guid = {guid},
        .player = {
            .identity = make_identity("VisiblePlayer"),
            .position = {
                .dimension_id = "overworld",
                .position = {.x = static_cast<int32_t>(std::floor(feet_x)), .y = 1, .z = 0},
            },
            .motion = {
                .feet_x = feet_x,
                .feet_y = 1.0f,
                .feet_z = 2.0f,
                .velocity_x = 0.0f,
                .velocity_y = 0.0f,
                .velocity_z = 0.0f,
                .yaw_centidegrees = yaw_centidegrees,
                .pitch_centidegrees = 1200,
                .last_processed_input_sequence = 4,
                .source_tick = source_tick,
                .grounded = true,
            },
        },
    };
    result.player.equipment_item_ids[0] = "copper_pickaxe";
    return result;
}

snt::game::GameRemotePlayerPresentationVisual make_visual() {
    return {
        .mesh = {.handle = {.id = 31}},
        .lod = {
            .simplified_handle = {.id = 32},
            .simplified_detail_distance = 18.0f,
            .cull_distance = 96.0f,
        },
        .model_scale = 1.1f,
    };
}

}  // namespace

TEST(GameRemotePlayerPresentationWorldTest, SamplesInterpolatedMotionAndRemovesExitedPlayers) {
    snt::ecs::World world;
    snt::game::GameRemotePlayerPresentationWorld presentation(world, make_visual());
    snt::game::GameRemotePlayerInterpolator interpolator({.interpolation_delay_ticks = 2});

    auto player = make_remote_player(41, 0.0f, 9000, 20);
    std::vector<snt::game::replication::GameRemotePlayerState> players{player};
    interpolator.reconcile(players, 10);
    presentation.reconcile(players, interpolator, 10);

    ASSERT_EQ(presentation.player_count(), 1u);
    const auto entity = presentation.entity_for(player.entity_guid);
    ASSERT_TRUE(entity.has_value());
    ASSERT_TRUE((world.registry().all_of<snt::render::Transform, snt::render::MeshRef,
                                        snt::render::MeshLod,
                                        snt::game::GameRemotePlayerPresentationComponent>(*entity)));
    EXPECT_FLOAT_EQ(world.registry().get<snt::render::Transform>(*entity).position[0], 0.0f);
    EXPECT_FLOAT_EQ(world.registry().get<snt::render::Transform>(*entity).rotation[1], 90.0f);

    players.front() = make_remote_player(41, 4.0f, 4500, 21);
    interpolator.reconcile(players, 14);
    presentation.reconcile(players, interpolator, 14);

    ASSERT_EQ(presentation.entity_for(player.entity_guid), entity);
    const auto& transform = world.registry().get<snt::render::Transform>(*entity);
    EXPECT_FLOAT_EQ(transform.position[0], 2.0f);
    EXPECT_FLOAT_EQ(transform.position[1], 1.0f);
    EXPECT_FLOAT_EQ(transform.position[2], 2.0f);
    EXPECT_FLOAT_EQ(transform.rotation[1], 45.0f);
    EXPECT_FLOAT_EQ(transform.scale[0], 1.1f);
    const auto& metadata =
        world.registry().get<snt::game::GameRemotePlayerPresentationComponent>(*entity);
    EXPECT_EQ(metadata.player_guid.value, 41u);
    EXPECT_EQ(metadata.display_name, "VisiblePlayer");
    EXPECT_EQ(metadata.equipment_item_ids[0], "copper_pickaxe");
    EXPECT_EQ(metadata.source_tick, 21u);
    EXPECT_TRUE(metadata.grounded);

    const std::array<snt::game::replication::GameRemotePlayerState, 0> empty_players{};
    presentation.reconcile(empty_players, interpolator, 15);
    EXPECT_EQ(presentation.player_count(), 0u);
    EXPECT_FALSE(world.registry().valid(*entity));
}
