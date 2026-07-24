// Dedicated-server movement coverage.

#include "ecs/world.h"
#include "game/player/player_identity.h"
#include "game/server/game_server_player_movement.h"
#include "game/server/game_server_player_state.h"
#include "game/tests/test_player_resource_snapshot.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace {

snt::game::replication::GameAuthenticatedPeer make_peer(
    snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

snt::voxel::VoxelChunk make_collision_chunk(bool wall_at_x_two) {
    snt::voxel::VoxelChunk chunk;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    for (int z = 0; z < chunk.terrain.size_z; ++z) {
        for (int x = 0; x < chunk.terrain.size_x; ++x) {
            chunk.terrain.set_cell(x, 0, z, 1, snt::voxel::TF_SOLID);
        }
    }
    if (wall_at_x_two) {
        for (int y = 1; y <= 3; ++y) {
            for (int z = 0; z < chunk.terrain.size_z; ++z) {
                chunk.terrain.set_cell(2, y, z, 1, snt::voxel::TF_SOLID);
            }
        }
    }
    return chunk;
}

void tick(snt::game::replication::GameServerPlayerMovement& movement, uint64_t tick_index) {
    const auto result = movement.tick({.tick_index = tick_index, .delta_seconds = 0.05f});
    ASSERT_TRUE(result) << result.error().format();
}

}  // namespace

TEST(GameServerPlayerMovementTest, IntegratesServerOwnedInputAndExpiresStaleIntent) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    chunks.set_chunk("overworld", 0, 0, 0, make_collision_chunk(false));

    auto player_state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .spawn = {.dimension_id = "overworld", .position = {.x = 1, .y = 1, .z = 1}},
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(701, "Movement Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(peer, (*player_state)->default_persistent_state()));

    auto movement = snt::game::replication::GameServerPlayerMovement::create(*(*player_state), chunks);
    ASSERT_TRUE(movement) << movement.error().format();
    ASSERT_TRUE((*movement)->enqueue_player_movement_input(
        peer,
        {.client_sequence = 1, .forward_axis = 1, .yaw_centidegrees = 0},
        {.tick_index = 1, .delta_seconds = 0.05f}));

    for (uint64_t tick_index = 1; tick_index <= 7; ++tick_index) tick(**movement, tick_index);
    auto moved = (*player_state)->snapshot_for_peer(peer);
    ASSERT_TRUE(moved) << moved.error().format();
    EXPECT_GT(moved->position.position.x, 1);
    EXPECT_EQ(moved->position.position.y, 1);

    for (uint64_t tick_index = 8; tick_index <= 20; ++tick_index) tick(**movement, tick_index);
    auto expired = (*player_state)->snapshot_for_peer(peer);
    ASSERT_TRUE(expired) << expired.error().format();
    EXPECT_EQ(expired->position.position.x, moved->position.position.x);
    EXPECT_EQ((*movement)->active_motion_count(), 1u);

    (*player_state)->on_peer_disconnected(peer, "test disconnect");
    tick(**movement, 21);
    EXPECT_EQ((*movement)->active_motion_count(), 0u);
}

TEST(GameServerPlayerMovementTest, StopsAtSolidVoxelInsteadOfTrustingClientPosition) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    chunks.set_chunk("overworld", 0, 0, 0, make_collision_chunk(true));

    auto player_state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .spawn = {.dimension_id = "overworld", .position = {.x = 1, .y = 1, .z = 1}},
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(702, "Collision Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(peer, (*player_state)->default_persistent_state()));

    auto movement = snt::game::replication::GameServerPlayerMovement::create(*(*player_state), chunks);
    ASSERT_TRUE(movement) << movement.error().format();
    ASSERT_TRUE((*movement)->enqueue_player_movement_input(
        peer,
        {.client_sequence = 1, .forward_axis = 1, .yaw_centidegrees = 0},
        {.tick_index = 1, .delta_seconds = 0.05f}));

    for (uint64_t tick_index = 1; tick_index <= 20; ++tick_index) tick(**movement, tick_index);
    auto stopped = (*player_state)->snapshot_for_peer(peer);
    ASSERT_TRUE(stopped) << stopped.error().format();
    EXPECT_EQ(stopped->position.position.x, 1);
    EXPECT_EQ(stopped->position.position.y, 1);
}

TEST(GameServerPlayerMovementTest, ResetsAcknowledgementWhenAccountPeerIsReplaced) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    chunks.set_chunk("overworld", 0, 0, 0, make_collision_chunk(false));

    auto player_state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = snt::game::test_support::player_resource_snapshot(),
            .spawn = {.dimension_id = "overworld", .position = {.x = 1, .y = 1, .z = 1}},
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto first_peer = make_peer(703, "Takeover Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        first_peer, (*player_state)->default_persistent_state()));

    auto movement = snt::game::replication::GameServerPlayerMovement::create(*(*player_state), chunks);
    ASSERT_TRUE(movement) << movement.error().format();
    ASSERT_TRUE((*movement)->enqueue_player_movement_input(
        first_peer,
        {.client_sequence = 41, .forward_axis = 1, .yaw_centidegrees = 0},
        {.tick_index = 1, .delta_seconds = 0.05f}));
    tick(**movement, 1);
    const auto first_snapshot = (*player_state)->snapshot_for_peer(first_peer);
    ASSERT_TRUE(first_snapshot) << first_snapshot.error().format();
    const auto before_takeover = (*movement)->motion_snapshot_for_player(first_snapshot->entity_guid);
    ASSERT_TRUE(before_takeover.has_value());
    EXPECT_EQ(before_takeover->last_processed_input_sequence, 41u);

    snt::game::replication::GameAuthenticatedPeer replacement_peer{
        .peer = 704,
        .identity = first_peer.identity,
    };
    (*movement)->on_peer_disconnected(first_peer, "test account takeover");
    ASSERT_TRUE((*player_state)->on_peer_replaced(first_peer, replacement_peer));
    ASSERT_TRUE((*movement)->enqueue_player_movement_input(
        replacement_peer,
        {.client_sequence = 1, .strafe_axis = 1, .yaw_centidegrees = 0},
        {.tick_index = 2, .delta_seconds = 0.05f}));
    tick(**movement, 2);

    const auto replacement_snapshot = (*player_state)->snapshot_for_peer(replacement_peer);
    ASSERT_TRUE(replacement_snapshot) << replacement_snapshot.error().format();
    const auto after_takeover =
        (*movement)->motion_snapshot_for_player(replacement_snapshot->entity_guid);
    ASSERT_TRUE(after_takeover.has_value());
    EXPECT_EQ(after_takeover->last_processed_input_sequence, 1u);
    EXPECT_EQ(replacement_snapshot->entity_guid, first_snapshot->entity_guid);
}
