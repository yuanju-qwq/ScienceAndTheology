// Dedicated-server death, grave, bed, and respawn coverage.

#include "game/server/game_server_player_death.h"

#include "ecs/world.h"
#include "game/player/player_identity.h"
#include "game/server/game_server_player_state.h"
#include "game/tests/test_player_resource_snapshot.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace {

using snt::game::GameChunkSidecarRegistry;
using snt::game::GamePlayerEquipmentSlot;
using snt::game::GamePlayerItemStack;
using snt::game::GamePlayerWorldPosition;
using snt::game::test_support::player_resource_snapshot;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameServerPlayerBedService;
using snt::game::replication::GameServerPlayerDeathService;
using snt::game::replication::GameServerPlayerGraveStore;
using snt::game::replication::GameServerPlayerRespawnResolver;
using snt::game::replication::GameServerPlayerState;

GameAuthenticatedPeer make_peer(snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

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

void expect_position(const GamePlayerWorldPosition& actual,
                     const GamePlayerWorldPosition& expected) {
    EXPECT_EQ(actual.dimension_id, expected.dimension_id);
    EXPECT_EQ(actual.position.x, expected.position.x);
    EXPECT_EQ(actual.position.y, expected.position.y);
    EXPECT_EQ(actual.position.z, expected.position.z);
}

struct CheckpointSink final : snt::game::replication::IGameServerPlayerStateCheckpointSink {
    int marks = 0;

    snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer&) override {
        ++marks;
        return {};
    }
};

struct MotionReset final : snt::game::replication::IGameServerPlayerMotionReset {
    int resets = 0;

    snt::core::Expected<void> reset_player_motion(const GameAuthenticatedPeer&) override {
        ++resets;
        return {};
    }
};

}  // namespace

TEST(GameServerPlayerDeathTest, CreatesPersistentGraveKeepsEquipmentAndRespawnsAtBed) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);
    auto* terrain = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(terrain, nullptr);
    terrain->terrain.set_cell(6, 1, 6, 2, snt::voxel::TF_SOLID);

    auto player_state = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = player_resource_snapshot(),
            .spawn = position(2, 1, 2),
            .inventory_slots = 3,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(401, "Death Player");
    auto persistent = (*player_state)->default_persistent_state();
    persistent.equipment.slots[static_cast<size_t>(GamePlayerEquipmentSlot::kMainHand)] =
        GamePlayerItemStack::item("steel_sword", 1, {}, "durability=91");
    persistent.organs = {
        .schema_id = "source_law",
        .schema_version = 1,
        .payload = {std::byte{0x2A}},
    };
    ASSERT_TRUE((*player_state)->on_peer_authenticated(peer, persistent));
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer,
        {.additions = {GamePlayerItemStack::item("iron_ingot", 3),
                       GamePlayerItemStack::item("relic", 1, {}, "unique")}}));

    CheckpointSink checkpoint;
    MotionReset motion_reset;
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars, &checkpoint);
    ASSERT_TRUE(beds) << beds.error().format();
    ASSERT_TRUE((*beds)->on_bed_placed(position(6, 1, 6)));
    ASSERT_TRUE((*beds)->set_respawn_point_from_bed(peer, position(6, 1, 6)));
    auto graves = GameServerPlayerGraveStore::create(chunks, sidecars);
    ASSERT_TRUE(graves) << graves.error().format();
    auto respawn = GameServerPlayerRespawnResolver::create(
        chunks, *(*beds), {.world_spawn = position(12, 1, 12)});
    ASSERT_TRUE(respawn) << respawn.error().format();
    auto death = GameServerPlayerDeathService::create(
        *(*player_state), *(*graves), *(*respawn), &checkpoint, &motion_reset);
    ASSERT_TRUE(death) << death.error().format();

    auto result = (*death)->resolve_death(peer, 77);
    ASSERT_TRUE(result) << result.error().format();
    ASSERT_TRUE(result->grave_id.has_value());
    expect_position(result->respawn_position, position(6, 2, 6));
    EXPECT_EQ(motion_reset.resets, 1);
    EXPECT_GE(checkpoint.marks, 2);

    auto after_death = (*player_state)->capture_persistent_state(peer);
    ASSERT_TRUE(after_death) << after_death.error().format();
    EXPECT_TRUE(after_death->inventory.slots[0].is_empty());
    EXPECT_TRUE(after_death->inventory.slots[1].is_empty());
    EXPECT_EQ(after_death->equipment, persistent.equipment);
    EXPECT_EQ(after_death->organs, persistent.organs);
    ASSERT_TRUE(after_death->respawn_point.has_value());
    expect_position(*after_death->respawn_point, position(6, 1, 6));

    auto grave_contents = (*graves)->read_grave(
        *result->grave_id, {.requester_account_id = peer.identity.account_id});
    ASSERT_TRUE(grave_contents) << grave_contents.error().format();
    ASSERT_EQ(grave_contents->items.size(), 2u);
    expect_position(grave_contents->position, position(2, 1, 2));
    const auto* grave_cell = &terrain->terrain.cell_at(2, 1, 2);
    EXPECT_TRUE(grave_cell->is_solid());
    EXPECT_TRUE(grave_cell->is_indestructible());
    EXPECT_FALSE((*graves)->read_grave(
        *result->grave_id, {.requester_account_id = "local-name:Intruder"}));

    auto claimed = (*death)->reclaim_grave(peer, *result->grave_id);
    ASSERT_TRUE(claimed) << claimed.error().format();
    EXPECT_EQ(claimed->status, snt::game::GamePlayerGraveClaimStatus::kCollected);
    EXPECT_EQ((*graves)->active_grave_count(), 0u);
    auto after_claim = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(after_claim) << after_claim.error().format();
    EXPECT_EQ(after_claim->slots[0].resource.key.id, "iron_ingot");
    EXPECT_EQ(after_claim->slots[0].resource.amount, 3);
    EXPECT_EQ(after_claim->slots[1].resource.key.id, "relic");
    EXPECT_FALSE(terrain->terrain.cell_at(2, 1, 2).is_indestructible());

    ASSERT_TRUE((*beds)->on_bed_removed(position(6, 1, 6)));
    auto fallback = (*respawn)->resolve_respawn(peer.identity.account_id, after_death->respawn_point);
    ASSERT_TRUE(fallback) << fallback.error().format();
    expect_position(*fallback, position(12, 1, 12));
    (*player_state)->shutdown();
}

TEST(GameServerPlayerDeathTest, LeavesGraveForInventoryUiWhenDirectClaimCannotFit) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);

    auto player_state = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = player_resource_snapshot(),
            .spawn = position(2, 1, 2),
            .inventory_slots = 1,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const auto peer = make_peer(402, "Full Inventory Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, {.additions = {GamePlayerItemStack::item("iron_ingot", 1)}}));

    auto graves = GameServerPlayerGraveStore::create(chunks, sidecars);
    ASSERT_TRUE(graves) << graves.error().format();
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars);
    ASSERT_TRUE(beds) << beds.error().format();
    auto respawn = GameServerPlayerRespawnResolver::create(
        chunks, *(*beds), {.world_spawn = position(12, 1, 12)});
    ASSERT_TRUE(respawn) << respawn.error().format();
    auto death = GameServerPlayerDeathService::create(
        *(*player_state), *(*graves), *(*respawn));
    ASSERT_TRUE(death) << death.error().format();

    auto killed = (*death)->resolve_death(peer, 78);
    ASSERT_TRUE(killed) << killed.error().format();
    ASSERT_TRUE(killed->grave_id.has_value());
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, {.additions = {GamePlayerItemStack::item("cobblestone", 5)}}));

    auto claim = (*death)->reclaim_grave(peer, *killed->grave_id);
    ASSERT_TRUE(claim) << claim.error().format();
    EXPECT_EQ(claim->status, snt::game::GamePlayerGraveClaimStatus::kInventoryFull);
    EXPECT_EQ(claim->contents.items.size(), 1u);
    EXPECT_EQ((*graves)->active_grave_count(), 1u);
    auto inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0].resource.key.id, "cobblestone");
    EXPECT_EQ(inventory->slots[0].resource.amount, 5);
    (*player_state)->shutdown();
}
