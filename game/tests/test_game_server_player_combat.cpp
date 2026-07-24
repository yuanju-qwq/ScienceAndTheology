// Dedicated-server combat death producer regression coverage.

#include "game/server/game_server_player_combat.h"
#include "game/server/game_server_player_death.h"
#include "game/server/game_server_player_state.h"
#include "game/tests/test_player_resource_snapshot.h"

#include "ecs/world.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

using snt::game::GameChunkSidecarRegistry;
using snt::game::GamePlayerItemStack;
using snt::game::GamePlayerWorldPosition;
using snt::game::test_support::player_resource_snapshot;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameServerPlayerCombatService;
using snt::game::replication::GameServerPlayerDamageRequest;
using snt::game::replication::GameServerPlayerDamageSource;
using snt::game::replication::GameServerPlayerDeathService;
using snt::game::replication::GameServerPlayerGraveStore;
using snt::game::replication::GameServerPlayerRespawnResolver;
using snt::game::replication::GameServerPlayerState;

void add_ground_chunk(snt::voxel::ChunkRegistry& chunks) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    for (int z = 0; z < snt::voxel::VoxelChunk::kChunkSize; ++z) {
        for (int x = 0; x < snt::voxel::VoxelChunk::kChunkSize; ++x) {
            chunk.terrain.set_cell(x, 0, z, 1, snt::voxel::TF_SOLID);
        }
    }
    chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));
}

GamePlayerWorldPosition position(int x, int y, int z) {
    return {.dimension_id = "overworld", .position = {.x = x, .y = y, .z = z}};
}

GameAuthenticatedPeer make_peer(snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

}  // namespace

TEST(GameServerPlayerCombatTest, AppliesWildDamageAndCommitsDeathBeforeResettingHealth) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);

    auto player_state = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = player_resource_snapshot(),
            .spawn = position(2, 1, 2),
            .inventory_slots = 3,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
            .combat_max_health = 10.0f,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const GameAuthenticatedPeer peer = make_peer(731, "Combat Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, {.additions = {GamePlayerItemStack::item("iron_ingot", 3)}}));

    auto graves = GameServerPlayerGraveStore::create(chunks, sidecars);
    ASSERT_TRUE(graves) << graves.error().format();
    auto beds = snt::game::replication::GameServerPlayerBedService::create(
        *(*player_state), chunks, sidecars);
    ASSERT_TRUE(beds) << beds.error().format();
    auto respawn = GameServerPlayerRespawnResolver::create(
        chunks, *(*beds), {.world_spawn = position(2, 1, 2)});
    ASSERT_TRUE(respawn) << respawn.error().format();
    auto death = GameServerPlayerDeathService::create(
        *(*player_state), *(*graves), *(*respawn));
    ASSERT_TRUE(death) << death.error().format();
    auto combat = GameServerPlayerCombatService::create(*(*player_state), *(*death));
    ASSERT_TRUE(combat) << combat.error().format();

    const auto targets = (*combat)->active_wild_creature_player_targets();
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_EQ(targets.front().account_id, peer.identity.account_id);
    EXPECT_EQ(targets.front().feet_x, 2.0f);

    ASSERT_TRUE((*combat)->apply_wild_creature_player_damage({
        .wild_entity_id = 9001,
        .species_id = 129,
        .target_account_id = peer.identity.account_id,
        .damage = 3.0f,
        .source_tick = 11,
    }));
    auto wounded = (*player_state)->combat_state_for_peer(peer);
    ASSERT_TRUE(wounded) << wounded.error().format();
    EXPECT_FLOAT_EQ(wounded->health_current, 7.0f);
    EXPECT_FLOAT_EQ(wounded->health_max, 10.0f);

    auto killed = (*combat)->apply_damage(
        peer,
        {
            .damage = 9.0f,
            .source = GameServerPlayerDamageSource::kEnvironment,
        },
        12);
    ASSERT_TRUE(killed) << killed.error().format();
    EXPECT_TRUE(killed->death_resolved);
    EXPECT_FLOAT_EQ(killed->damage_applied, 7.0f);
    EXPECT_FLOAT_EQ(killed->combat.health_current, 10.0f);
    EXPECT_EQ((*graves)->active_grave_count(), 1u);

    // Wildlife uses one immutable player-position snapshot per source tick.
    // A second nearby predator must not turn that old location into another
    // death after the first attack has already respawned this actor.
    auto duplicate = (*combat)->apply_damage(
        peer,
        {
            .damage = 10.0f,
            .source = GameServerPlayerDamageSource::kWildCreature,
            .source_entity_id = 9002,
            .source_species_id = 130,
        },
        12);
    ASSERT_TRUE(duplicate) << duplicate.error().format();
    EXPECT_TRUE(duplicate->ignored_after_death_this_tick);
    EXPECT_FALSE(duplicate->death_resolved);
    EXPECT_FLOAT_EQ(duplicate->damage_applied, 0.0f);
    EXPECT_FLOAT_EQ(duplicate->combat.health_current, 10.0f);
    EXPECT_EQ((*graves)->active_grave_count(), 1u);

    auto next_tick_wound = (*combat)->apply_damage(
        peer, {.damage = 3.0f, .source = GameServerPlayerDamageSource::kWildCreature}, 13);
    ASSERT_TRUE(next_tick_wound) << next_tick_wound.error().format();
    EXPECT_FALSE(next_tick_wound->ignored_after_death_this_tick);
    EXPECT_FLOAT_EQ(next_tick_wound->combat.health_current, 7.0f);

    auto after = (*player_state)->capture_persistent_state(peer);
    ASSERT_TRUE(after) << after.error().format();
    EXPECT_TRUE(after->inventory.slots.front().is_empty());
    EXPECT_EQ(after->position.dimension_id, "overworld");
    EXPECT_EQ(after->position.position.x, 2);
    EXPECT_EQ(after->position.position.y, 1);
    EXPECT_EQ(after->position.position.z, 2);

    const auto rejected = (*combat)->apply_damage(
        peer, {.damage = 0.0f, .source = GameServerPlayerDamageSource::kSystem}, 13);
    EXPECT_FALSE(rejected);
    (*player_state)->shutdown();
}
