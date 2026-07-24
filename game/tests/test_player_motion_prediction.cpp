// Client prediction, correction, and remote interpolation coverage.

#include "game/client/player_motion_presentation.h"
#include "game/player/player_identity.h"

#include "player/voxel_collision.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class FlatFloorCollisionWorld final : public snt::player::IVoxelCollisionWorld {
public:
    bool is_solid_block(int32_t, int32_t y, int32_t) const override { return y == 0; }
};

snt::game::PlayerIdentity make_identity(std::string display_name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(display_name));
    return identity ? std::move(*identity) : snt::game::PlayerIdentity{};
}

snt::game::replication::GameReplicatedPlayerState make_player_state(
    float feet_x, uint64_t acknowledged_sequence, uint64_t source_tick) {
    return {
        .identity = make_identity("PredictionPlayer"),
        .position = {
            .dimension_id = "overworld",
            .position = {.x = static_cast<int32_t>(std::floor(feet_x)), .y = 1, .z = 0},
        },
        .motion = {
            .feet_x = feet_x,
            .feet_y = 1.0f,
            .feet_z = 0.0f,
            .velocity_x = 0.0f,
            .velocity_y = 0.0f,
            .velocity_z = 0.0f,
            .yaw_centidegrees = 0,
            .pitch_centidegrees = 0,
            .last_processed_input_sequence = acknowledged_sequence,
            .source_tick = source_tick,
            .grounded = true,
        },
    };
}

}  // namespace

TEST(GameClientPlayerPredictionTest, ReplaysUnacknowledgedInputAndSmoothsConfirmation) {
    auto prediction = snt::game::GameClientPlayerPrediction::create({});
    ASSERT_TRUE(prediction) << prediction.error().format();
    FlatFloorCollisionWorld collision_world;

    auto initial = make_player_state(0.0f, 0, 10);
    auto initialized = (*prediction)->apply_authoritative(initial, collision_world);
    ASSERT_TRUE(initialized) << initialized.error().format();
    EXPECT_TRUE(initialized->initialized);

    ASSERT_TRUE((*prediction)->predict(
        {.client_sequence = 1, .forward_axis = 1, .yaw_centidegrees = 0},
        collision_world, 0.05f));
    ASSERT_TRUE((*prediction)->predict(
        {.client_sequence = 2, .forward_axis = 1, .yaw_centidegrees = 0},
        collision_world, 0.05f));
    ASSERT_EQ((*prediction)->pending_input_count(), 2u);

    auto confirmed = make_player_state(0.215f, 1, 11);
    auto reconciliation = (*prediction)->apply_authoritative(confirmed, collision_world);
    ASSERT_TRUE(reconciliation) << reconciliation.error().format();
    EXPECT_EQ(reconciliation->acknowledged_movement_sequence, 1u);
    EXPECT_EQ(reconciliation->pending_input_count, 1u);
    EXPECT_TRUE(reconciliation->replayed_pending_inputs);
    EXPECT_FALSE(reconciliation->hard_snapped);

    const auto presentation = (*prediction)->presentation();
    ASSERT_TRUE(presentation.has_value());
    EXPECT_GT(presentation->feet_x, confirmed.motion.feet_x);
}

TEST(GameClientPlayerPredictionTest, DecaysSmallCorrectionAndHardSnapsLargeCorrection) {
    auto prediction = snt::game::GameClientPlayerPrediction::create({});
    ASSERT_TRUE(prediction) << prediction.error().format();
    FlatFloorCollisionWorld collision_world;

    ASSERT_TRUE((*prediction)->apply_authoritative(
        make_player_state(0.0f, 0, 10), collision_world));
    ASSERT_TRUE((*prediction)->predict(
        {.client_sequence = 1, .forward_axis = 1, .yaw_centidegrees = 0},
        collision_world, 0.05f));

    auto rejected = make_player_state(0.0f, 1, 11);
    auto correction = (*prediction)->apply_authoritative(rejected, collision_world);
    ASSERT_TRUE(correction) << correction.error().format();
    EXPECT_FALSE(correction->hard_snapped);
    const auto before_decay = (*prediction)->presentation();
    ASSERT_TRUE(before_decay.has_value());
    EXPECT_GT(before_decay->feet_x, 0.0f);

    (*prediction)->advance_presentation(0.12f);
    const auto after_decay = (*prediction)->presentation();
    ASSERT_TRUE(after_decay.has_value());
    EXPECT_GT(after_decay->feet_x, 0.0f);
    EXPECT_LT(after_decay->feet_x, before_decay->feet_x);

    auto distant = make_player_state(-10.0f, 1, 12);
    auto hard_correction = (*prediction)->apply_authoritative(distant, collision_world);
    ASSERT_TRUE(hard_correction) << hard_correction.error().format();
    EXPECT_TRUE(hard_correction->hard_snapped);
    const auto snapped = (*prediction)->presentation();
    ASSERT_TRUE(snapped.has_value());
    EXPECT_FLOAT_EQ(snapped->feet_x, -10.0f);
}

TEST(GameRemotePlayerInterpolatorTest, SamplesBetweenOrderedRemoteSnapshots) {
    snt::game::GameRemotePlayerInterpolator interpolator({.interpolation_delay_ticks = 2});
    snt::game::replication::GameRemotePlayerState remote{
        .entity_guid = {77},
        .player = make_player_state(0.0f, 0, 20),
    };
    interpolator.reconcile({remote}, 10);

    remote.player = make_player_state(4.0f, 0, 21);
    interpolator.reconcile({remote}, 14);

    const auto interpolated = interpolator.sample(remote.entity_guid, 14);
    ASSERT_TRUE(interpolated.has_value());
    EXPECT_FLOAT_EQ(interpolated->feet_x, 2.0f);
    EXPECT_EQ(interpolated->source_tick, 21u);
}
