// Authoritative player AOI, payload codec, and client value-world tests.

#include "game/player/player_replication.h"
#include "game/server/game_server_player_replication.h"
#include "game/server/game_server_player_state.h"

#include "ecs/world.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GamePlayerPersistentState;
using snt::game::PlayerIdentity;
using snt::game::make_local_name_player_identity;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GamePlayerReplicationEntity;
using snt::game::replication::GamePlayerReplicationOperation;
using snt::game::replication::GameRemotePlayerWorld;
using snt::game::replication::GameReplicatedPlayerState;
using snt::game::replication::GameServerPlayerReplication;
using snt::game::replication::GameServerPlayerReplicationConfig;
using snt::game::replication::GameServerPlayerState;

PlayerIdentity make_local_identity(std::string name) {
    auto identity = make_local_name_player_identity(std::move(name));
    return identity ? std::move(*identity) : PlayerIdentity{};
}

GameAuthenticatedPeer make_peer(snt::network::PeerId peer, std::string name) {
    return {.peer = peer, .identity = make_local_identity(std::move(name))};
}

GamePlayerPersistentState state_at(GameServerPlayerState& players, std::string dimension,
                                   int32_t x, int32_t y, int32_t z) {
    GamePlayerPersistentState state = players.default_persistent_state();
    state.position.dimension_id = std::move(dimension);
    state.position.position = {.x = x, .y = y, .z = z};
    return state;
}

const snt::game::replication::GameEntitySnapshot* find_entity(
    const snt::game::replication::GameSnapshot& snapshot, snt::ecs::EntityGuid guid) {
    const auto result = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                                     [guid](const auto& entity) {
                                         return entity.entity_guid == guid;
                                     });
    return result == snapshot.entities.end() ? nullptr : &*result;
}

TEST(GamePlayerReplicationCodecTest, RoundTripsPresentationOnlyPlayerValues) {
    const PlayerIdentity identity = make_local_identity("VisiblePlayer");
    GameReplicatedPlayerState state{
        .identity = identity,
        .position = {
            .dimension_id = "overworld",
            .position = {.x = -13, .y = 67, .z = 22},
        },
    };
    state.equipment_item_ids[0] = "iron_pickaxe";
    state.equipment_item_ids[2] = "iron_helmet";

    auto encoded = snt::game::replication::encode_game_player_replication_entity({
        .operation = GamePlayerReplicationOperation::kUpsert,
        .player = state,
    });
    ASSERT_TRUE(encoded) << encoded.error().format();
    ASSERT_TRUE(snt::game::replication::is_game_player_replication_entity_payload(*encoded));
    auto decoded = snt::game::replication::decode_game_player_replication_entity(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().format();
    ASSERT_EQ(decoded->operation, GamePlayerReplicationOperation::kUpsert);
    ASSERT_TRUE(decoded->player.has_value());
    EXPECT_EQ(decoded->player->identity.account_id, identity.account_id);
    EXPECT_EQ(decoded->player->identity.display_name, identity.display_name);
    EXPECT_EQ(decoded->player->position.dimension_id, "overworld");
    EXPECT_EQ(decoded->player->position.position.x, -13);
    EXPECT_EQ(decoded->player->equipment_item_ids[0], "iron_pickaxe");
    EXPECT_EQ(decoded->player->equipment_item_ids[2], "iron_helmet");

    auto remove = snt::game::replication::encode_game_player_replication_entity({
        .operation = GamePlayerReplicationOperation::kRemove,
    });
    ASSERT_TRUE(remove) << remove.error().format();
    auto decoded_remove = snt::game::replication::decode_game_player_replication_entity(*remove);
    ASSERT_TRUE(decoded_remove) << decoded_remove.error().format();
    EXPECT_EQ(decoded_remove->operation, GamePlayerReplicationOperation::kRemove);
    EXPECT_FALSE(decoded_remove->player.has_value());

    encoded->push_back(std::byte{0});
    const auto trailing = snt::game::replication::decode_game_player_replication_entity(*encoded);
    EXPECT_FALSE(trailing);
}

TEST(GameServerPlayerReplicationTest, FiltersAoiAndAppliesAuthoritativeDeltasToRemoteWorld) {
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(world);
    ASSERT_TRUE(players) << players.error().format();

    const GameAuthenticatedPeer alice = make_peer(101, "Alice");
    const GameAuthenticatedPeer bob = make_peer(102, "Bob");
    const GameAuthenticatedPeer carol = make_peer(103, "Carol");
    const GameAuthenticatedPeer dana = make_peer(104, "Dana");
    ASSERT_TRUE((*players)->on_peer_authenticated(alice, state_at(**players, "overworld", 0, 64, 0)));
    ASSERT_TRUE((*players)->on_peer_authenticated(bob, state_at(**players, "overworld", 8, 64, 0)));
    ASSERT_TRUE((*players)->on_peer_authenticated(carol, state_at(**players, "overworld", 24, 64, 0)));
    ASSERT_TRUE((*players)->on_peer_authenticated(dana, state_at(**players, "nether", 0, 64, 0)));

    auto replication = GameServerPlayerReplication::create(
        **players,
        {
            .horizontal_aoi_radius_blocks = 12,
            .vertical_aoi_radius_blocks = 8,
            .max_visible_players = 8,
        });
    ASSERT_TRUE(replication) << replication.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 9, .delta_seconds = 0.05f};
    auto interest = (*replication)->compute_interest(alice, context);
    ASSERT_TRUE(interest) << interest.error().format();
    auto alice_snapshot = (*players)->snapshot_for_peer(alice);
    auto bob_snapshot = (*players)->snapshot_for_peer(bob);
    ASSERT_TRUE(alice_snapshot) << alice_snapshot.error().format();
    ASSERT_TRUE(bob_snapshot) << bob_snapshot.error().format();
    ASSERT_EQ(interest->entities.size(), 2u);
    EXPECT_EQ(interest->entities[0], alice_snapshot->entity_guid);
    EXPECT_EQ(interest->entities[1], bob_snapshot->entity_guid);

    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_chunk_snapshots_per_tick = 0,
        .max_entity_snapshots_per_tick = 8,
        .max_block_deltas_per_tick = 0,
    };
    auto initial_messages = (*replication)->build_initial_snapshot(alice, *interest, budget, context);
    ASSERT_TRUE(initial_messages) << initial_messages.error().format();
    ASSERT_EQ(initial_messages->size(), 1u);
    auto initial = snt::game::replication::parse_game_snapshot(initial_messages->front());
    ASSERT_TRUE(initial) << initial.error().format();
    ASSERT_EQ(initial->entities.size(), 2u);
    const auto* bob_entity = find_entity(*initial, bob_snapshot->entity_guid);
    ASSERT_NE(bob_entity, nullptr);
    auto decoded_bob = snt::game::replication::decode_game_player_replication_entity(bob_entity->payload);
    ASSERT_TRUE(decoded_bob) << decoded_bob.error().format();
    ASSERT_TRUE(decoded_bob->player.has_value());
    EXPECT_EQ(decoded_bob->player->identity.account_id, bob.identity.account_id);

    GameRemotePlayerWorld remote_world(alice.identity.account_id);
    ASSERT_TRUE(remote_world.apply(*initial));
    EXPECT_EQ(remote_world.player_count(), 2u);
    ASSERT_TRUE(remote_world.authoritative_local_player().has_value());
    ASSERT_EQ(remote_world.remote_players().size(), 1u);
    EXPECT_EQ(remote_world.remote_players().front().player.identity.account_id,
              bob.identity.account_id);

    ASSERT_TRUE((*players)->set_authoritative_position(
        bob, {.dimension_id = "overworld", .position = {.x = 9, .y = 64, .z = 1}}));
    const entt::entity bob_actor = world.find_entity_by_guid(bob_snapshot->entity_guid);
    ASSERT_TRUE(bob_actor != entt::null);
    world.get_component<snt::game::GamePlayerEquipment>(bob_actor).slots[0] = {
        .item_id = "iron_pickaxe",
        .count = 1,
    };

    auto changed_interest = (*replication)->compute_interest(alice, context);
    ASSERT_TRUE(changed_interest) << changed_interest.error().format();
    auto delta_messages = (*replication)->build_deltas(alice, *changed_interest, budget, context);
    ASSERT_TRUE(delta_messages) << delta_messages.error().format();
    ASSERT_EQ(delta_messages->size(), 1u);
    auto delta = snt::game::replication::parse_game_delta(delta_messages->front());
    ASSERT_TRUE(delta) << delta.error().format();
    EXPECT_EQ(delta->base_snapshot_id, initial->snapshot_id);
    EXPECT_EQ(delta->sequence, 1u);
    ASSERT_TRUE(remote_world.apply(*delta));
    const auto remote_bob = remote_world.remote_players();
    ASSERT_EQ(remote_bob.size(), 1u);
    EXPECT_EQ(remote_bob.front().player.position.position.x, 9);
    EXPECT_EQ(remote_bob.front().player.equipment_item_ids[0], "iron_pickaxe");

    ASSERT_TRUE((*players)->set_authoritative_position(
        bob, {.dimension_id = "overworld", .position = {.x = 40, .y = 64, .z = 1}}));
    auto departed_interest = (*replication)->compute_interest(alice, context);
    ASSERT_TRUE(departed_interest) << departed_interest.error().format();
    auto departure_messages = (*replication)->build_deltas(alice, *departed_interest, budget, context);
    ASSERT_TRUE(departure_messages) << departure_messages.error().format();
    ASSERT_EQ(departure_messages->size(), 1u);
    auto departure = snt::game::replication::parse_game_delta(departure_messages->front());
    ASSERT_TRUE(departure) << departure.error().format();
    ASSERT_TRUE(remote_world.apply(*departure));
    EXPECT_TRUE(remote_world.remote_players().empty());
    EXPECT_EQ(remote_world.player_count(), 1u);

    (*replication)->on_peer_disconnected(alice, "test completed");
    const auto missing_baseline = (*replication)->build_deltas(alice, *departed_interest, budget, context);
    EXPECT_FALSE(missing_baseline);
    (*players)->shutdown();
}

TEST(GameServerPlayerReplicationTest, LimitsInitialPlayerSnapshotToObserverBudget) {
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(world);
    ASSERT_TRUE(players) << players.error().format();

    const GameAuthenticatedPeer observer = make_peer(201, "Observer");
    const GameAuthenticatedPeer nearby = make_peer(202, "Nearby");
    ASSERT_TRUE((*players)->on_peer_authenticated(observer, state_at(**players, "overworld", 0, 64, 0)));
    ASSERT_TRUE((*players)->on_peer_authenticated(nearby, state_at(**players, "overworld", 1, 64, 0)));
    auto replication = GameServerPlayerReplication::create(
        **players, {.horizontal_aoi_radius_blocks = 16, .vertical_aoi_radius_blocks = 8,
                    .max_visible_players = 8});
    ASSERT_TRUE(replication) << replication.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 4, .delta_seconds = 0.05f};
    auto interest = (*replication)->compute_interest(observer, context);
    ASSERT_TRUE(interest) << interest.error().format();
    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_entity_snapshots_per_tick = 1,
    };
    auto messages = (*replication)->build_initial_snapshot(observer, *interest, budget, context);
    ASSERT_TRUE(messages) << messages.error().format();
    ASSERT_EQ(messages->size(), 1u);
    auto snapshot = snt::game::replication::parse_game_snapshot(messages->front());
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    ASSERT_EQ(snapshot->entities.size(), 1u);
    auto observer_snapshot = (*players)->snapshot_for_peer(observer);
    ASSERT_TRUE(observer_snapshot) << observer_snapshot.error().format();
    EXPECT_EQ(snapshot->entities.front().entity_guid, observer_snapshot->entity_guid);
    (*players)->shutdown();
}

}  // namespace
