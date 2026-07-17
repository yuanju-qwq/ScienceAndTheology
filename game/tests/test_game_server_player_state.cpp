// Dedicated-server authoritative player state coverage.

#include "game/server/game_server_player_state.h"

#include "ecs/world.h"
#include "game/player/player_death.h"
#include "game/player/player_identity.h"
#include "game/quest/quest_book.h"

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

}  // namespace

TEST(GameServerPlayerStateTest, TransfersTakeoverButDestroysDisconnectedActor) {
    snt::ecs::World world;
    auto state = snt::game::replication::GameServerPlayerState::create(world);
    ASSERT_TRUE(state) << state.error().format();

    auto original_identity = snt::game::make_steam_player_identity(
        76561198000000001ull, "State Player");
    ASSERT_TRUE(original_identity) << original_identity.error().format();
    snt::game::replication::GameAuthenticatedPeer original{
        .peer = 101,
        .identity = std::move(*original_identity),
    };
    ASSERT_TRUE((*state)->on_peer_authenticated(
        original, (*state)->default_persistent_state()));
    auto first = (*state)->snapshot_for_peer(original);
    ASSERT_TRUE(first) << first.error().format();
    EXPECT_TRUE(first->entity_guid.valid());
    EXPECT_EQ(first->position.dimension_id, "overworld");
    EXPECT_EQ(first->position.position.x, 4);
    EXPECT_EQ((*state)->active_player_count(), 1u);

    ASSERT_TRUE((*state)->set_authoritative_position(
        original, {.dimension_id = "overworld", .position = {.x = 12, .y = 7, .z = -3}}));
    ASSERT_TRUE((*state)->set_respawn_point(
        original, snt::game::GamePlayerWorldPosition{
                      .dimension_id = "overworld", .position = {.x = 3, .y = 66, .z = 9}}));
    auto reachable = (*state)->is_target_reachable(
        original, {.dimension_id = "overworld", .position = {.x = 15, .y = 7, .z = -3}});
    ASSERT_TRUE(reachable) << reachable.error().format();
    EXPECT_TRUE(*reachable);
    auto too_far = (*state)->is_target_reachable(
        original, {.dimension_id = "overworld", .position = {.x = 18, .y = 7, .z = -3}});
    ASSERT_TRUE(too_far) << too_far.error().format();
    EXPECT_FALSE(*too_far);
    auto wrong_dimension = (*state)->is_target_reachable(
        original, {.dimension_id = "other", .position = {.x = 12, .y = 7, .z = -3}});
    ASSERT_TRUE(wrong_dimension) << wrong_dimension.error().format();
    EXPECT_FALSE(*wrong_dimension);

    auto replacement_identity = snt::game::make_steam_player_identity(
        76561198000000001ull, "Renamed State Player");
    ASSERT_TRUE(replacement_identity) << replacement_identity.error().format();
    snt::game::replication::GameAuthenticatedPeer replacement{
        .peer = 102,
        .identity = std::move(*replacement_identity),
    };
    ASSERT_TRUE((*state)->on_peer_replaced(original, replacement));
    const auto original_snapshot = (*state)->snapshot_for_peer(original);
    EXPECT_FALSE(original_snapshot);
    auto moved = (*state)->snapshot_for_peer(replacement);
    ASSERT_TRUE(moved) << moved.error().format();
    EXPECT_EQ(moved->entity_guid, first->entity_guid);
    EXPECT_EQ(moved->display_name, "Renamed State Player");
    EXPECT_EQ(moved->position.position.x, 12);

    auto saved_state = (*state)->capture_persistent_state(replacement);
    ASSERT_TRUE(saved_state) << saved_state.error().format();
    ASSERT_TRUE(saved_state->respawn_point.has_value());
    EXPECT_EQ(saved_state->respawn_point->position.x, 3);
    const auto disconnected_guid = moved->entity_guid;
    (*state)->on_peer_disconnected(replacement, "test disconnect");
    EXPECT_EQ((*state)->active_player_count(), 0u);
    EXPECT_TRUE(world.find_entity_by_guid(disconnected_guid) == entt::null);

    auto reconnect_identity = snt::game::make_steam_player_identity(
        76561198000000001ull, "Reconnected State Player");
    ASSERT_TRUE(reconnect_identity) << reconnect_identity.error().format();
    snt::game::replication::GameAuthenticatedPeer reconnect{
        .peer = 103,
        .identity = std::move(*reconnect_identity),
    };
    ASSERT_TRUE((*state)->on_peer_authenticated(reconnect, *saved_state));
    auto restored = (*state)->snapshot_for_peer(reconnect);
    ASSERT_TRUE(restored) << restored.error().format();
    EXPECT_NE(restored->entity_guid, first->entity_guid);
    EXPECT_EQ(restored->position.position.x, 12);

    const auto guid = restored->entity_guid;
    (*state)->shutdown();
    EXPECT_TRUE(world.find_entity_by_guid(guid) == entt::null);
}

TEST(GameServerPlayerStateTest, CommitsInventoryChangesAtomicallyAndKeepsToolTagsTrusted) {
    snt::ecs::World world;
    auto state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .spawn = {.dimension_id = "overworld", .position = {}},
            .inventory_slots = 2,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(state) << state.error().format();
    auto peer = make_peer(104, "Inventory Player");
    ASSERT_TRUE((*state)->on_peer_authenticated(
        peer, (*state)->default_persistent_state()));

    ASSERT_TRUE((*state)->apply_inventory_transaction(
        peer,
        {
            .additions = {{.item_id = "iron", .count = 3},
                          {.item_id = "coal", .count = 5}},
        }));
    auto initial_inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(initial_inventory) << initial_inventory.error().format();
    ASSERT_EQ(initial_inventory->slots.size(), 2u);

    const auto rejected = (*state)->apply_inventory_transaction(
        peer,
        {
            .removals = {{.item_id = "iron", .count = 4}},
            .additions = {{.item_id = "copper", .count = 1}},
        });
    ASSERT_FALSE(rejected);
    auto unchanged_inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(unchanged_inventory) << unchanged_inventory.error().format();
    EXPECT_EQ(*unchanged_inventory, *initial_inventory);

    ASSERT_TRUE((*state)->apply_inventory_transaction(
        peer,
        {
            .removals = {{.item_id = "iron", .count = 2}},
            .additions = {{.item_id = "iron", .count = 4}},
        }));
    auto updated_inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(updated_inventory) << updated_inventory.error().format();
    ASSERT_EQ(updated_inventory->slots.size(), 2u);
    EXPECT_EQ(updated_inventory->slots[0].item_id, "iron");
    EXPECT_EQ(updated_inventory->slots[0].count, 5);

    ASSERT_TRUE((*state)->replace_trusted_held_tool_tags(peer, {"hammer", "axe", "hammer"}));
    auto tags = (*state)->held_tool_tags_for_peer(peer);
    ASSERT_TRUE(tags) << tags.error().format();
    EXPECT_EQ(*tags, (std::vector<std::string>{"axe", "hammer"}));

    (*state)->on_peer_disconnected(peer, "test disconnect");
    const auto offline_mutation = (*state)->apply_inventory_transaction(
        peer, {.additions = {{.item_id = "copper", .count = 1}}});
    EXPECT_FALSE(offline_mutation);
    (*state)->shutdown();
}

TEST(GameServerPlayerStateTest, AppliesConditionalSlotTransfersAtomically) {
    snt::ecs::World world;
    auto state = snt::game::replication::GameServerPlayerState::create(
        world,
        {
            .spawn = {.dimension_id = "overworld", .position = {}},
            .inventory_slots = 4,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(state) << state.error().format();
    const auto peer = make_peer(105, "Slot Transfer Player");
    ASSERT_TRUE((*state)->on_peer_authenticated(
        peer, (*state)->default_persistent_state()));
    ASSERT_TRUE((*state)->apply_inventory_transaction(
        peer,
        {
            .additions = {{.item_id = "iron", .count = 3},
                          {.item_id = "coal", .count = 2}},
        }));

    auto inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    ASSERT_TRUE((*state)->apply_inventory_slot_transfer(
        peer,
        {
            .source_slot = 0,
            .target_slot = 2,
            .count = 1,
            .expected_source = inventory->slots[0],
            .expected_target = inventory->slots[2],
        }));
    inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0], (snt::game::GamePlayerItemStack{"iron", 2}));
    EXPECT_EQ(inventory->slots[2], (snt::game::GamePlayerItemStack{"iron", 1}));

    ASSERT_TRUE((*state)->apply_inventory_slot_transfer(
        peer,
        {
            .source_slot = 0,
            .target_slot = 2,
            .count = 2,
            .expected_source = inventory->slots[0],
            .expected_target = inventory->slots[2],
        }));
    inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0], (snt::game::GamePlayerItemStack{}));
    EXPECT_EQ(inventory->slots[2], (snt::game::GamePlayerItemStack{"iron", 3}));

    ASSERT_TRUE((*state)->apply_inventory_slot_transfer(
        peer,
        {
            .source_slot = 1,
            .target_slot = 2,
            .count = 2,
            .expected_source = inventory->slots[1],
            .expected_target = inventory->slots[2],
        }));
    inventory = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[1], (snt::game::GamePlayerItemStack{"iron", 3}));
    EXPECT_EQ(inventory->slots[2], (snt::game::GamePlayerItemStack{"coal", 2}));

    const auto stale = (*state)->can_apply_inventory_slot_transfer(
        peer,
        {
            .source_slot = 1,
            .target_slot = 2,
            .count = 3,
            .expected_source = {.item_id = "coal", .count = 2},
            .expected_target = {.item_id = "iron", .count = 3},
        });
    ASSERT_TRUE(stale) << stale.error().format();
    EXPECT_FALSE(*stale);

    const auto rejected = (*state)->apply_inventory_slot_transfer(
        peer,
        {
            .source_slot = 1,
            .target_slot = 2,
            .count = 3,
            .expected_source = {.item_id = "coal", .count = 2},
            .expected_target = {.item_id = "iron", .count = 3},
        });
    EXPECT_FALSE(rejected);
    auto unchanged = (*state)->inventory_for_peer(peer);
    ASSERT_TRUE(unchanged) << unchanged.error().format();
    EXPECT_EQ(*unchanged, *inventory);
    (*state)->shutdown();
}
