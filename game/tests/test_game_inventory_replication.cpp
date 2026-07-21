// Player inventory replication codec, cache, source, and command-route tests.

#include "game/client/game_content_registry.h"
#include "game/network/game_inventory_replication.h"
#include "game/player/player_identity.h"
#include "game/quest/quest_registry.h"
#include "game/server/game_server_command_sink.h"
#include "game/server/game_server_inventory_replication.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"

#include "ecs/world.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameContentRegistry;
using snt::game::GamePlayerInventory;
using snt::game::GamePlayerItemStack;
using snt::game::PlayerIdentity;
using snt::game::QuestRegistry;
using snt::game::make_local_name_player_identity;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameClientInventoryState;
using snt::game::replication::GameDelta;
using snt::game::replication::GameInventoryCommandKind;
using snt::game::replication::GameInventoryDelta;
using snt::game::replication::GameInventoryReplicationPayload;
using snt::game::replication::GameInventorySlotChange;
using snt::game::replication::GameInventorySlotTransferCommand;
using snt::game::replication::GameInventorySlotTransferOutcome;
using snt::game::replication::GameInventorySnapshot;
using snt::game::replication::GameMachineInputSlotTransferCommand;
using snt::game::replication::GameMachineInputSlotTransferDirection;
using snt::game::replication::GameReplicationBudget;
using snt::game::replication::GameReplicationInterest;
using snt::game::replication::GameReplicationValue;
using snt::game::replication::GameReplicationValueCollectionPhase;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameServerCommandSink;
using snt::game::replication::GameServerInventoryReplication;
using snt::game::replication::GameServerPlayerState;
using snt::game::replication::GameSnapshot;
using snt::game::replication::IGameServerPlayerStateCheckpointSink;
using snt::game::replication::decode_game_inventory_replication_payload;
using snt::game::replication::encode_game_inventory_delta;
using snt::game::replication::encode_game_inventory_snapshot;
using snt::game::replication::make_game_inventory_slot_transfer_command;
using snt::game::replication::make_game_machine_input_slot_transfer_command;
using snt::game::replication::parse_game_machine_input_slot_transfer_command;

PlayerIdentity make_local_identity(std::string name) {
    auto identity = make_local_name_player_identity(std::move(name));
    return identity ? std::move(*identity) : PlayerIdentity{};
}

GameAuthenticatedPeer make_peer(snt::network::PeerId peer, std::string name) {
    return {.peer = peer, .identity = make_local_identity(std::move(name))};
}

GameInventorySnapshot make_inventory_snapshot(std::string account_id, uint64_t revision = 1) {
    GamePlayerInventory inventory;
    inventory.max_slots = 36;
    inventory.max_stack_size = 64;
    inventory.slots.resize(inventory.max_slots);
    inventory.slots[0] = GamePlayerItemStack::item("iron", 12);
    inventory.slots[7] = GamePlayerItemStack::item("hammer", 1, {}, "durability=97");
    return {
        .account_id = std::move(account_id),
        .inventory_revision = revision,
        .inventory = std::move(inventory),
    };
}

GameReplicationBudget inventory_budget() {
    return {
        .max_reliable_bytes_per_tick = 4096,
        .max_value_snapshots_per_tick = 1,
    };
}

class CountingCheckpointSink final : public IGameServerPlayerStateCheckpointSink {
public:
    snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer& peer) override {
        ++mark_count;
        last_peer = peer.peer;
        return {};
    }

    uint32_t mark_count = 0;
    snt::network::PeerId last_peer = snt::network::kInvalidPeerId;
};

TEST(GameInventoryReplicationCodecTest, SendsOnlyChangedSlotsAfterFullSnapshot) {
    const GameInventorySnapshot initial = make_inventory_snapshot("local-name:InventoryCodec");
    auto encoded_snapshot = encode_game_inventory_snapshot(initial);
    ASSERT_TRUE(encoded_snapshot) << encoded_snapshot.error().format();
    auto decoded_snapshot = decode_game_inventory_replication_payload(*encoded_snapshot);
    ASSERT_TRUE(decoded_snapshot) << decoded_snapshot.error().format();
    ASSERT_TRUE(std::holds_alternative<GameInventorySnapshot>(*decoded_snapshot));
    const auto& round_trip_snapshot = std::get<GameInventorySnapshot>(*decoded_snapshot);
    EXPECT_EQ(round_trip_snapshot.account_id, initial.account_id);
    EXPECT_EQ(round_trip_snapshot.inventory, initial.inventory);

    GameInventoryDelta delta{
        .account_id = initial.account_id,
        .inventory_revision = 2,
        .response_revision = 1,
        .response = {
            .request_id = 41,
            .kind = GameInventoryCommandKind::kInventorySlotTransfer,
            .outcome = GameInventorySlotTransferOutcome::kAccepted,
        },
        .changed_slots = {
            {.slot_index = 0, .stack = GamePlayerItemStack::item("iron", 8)},
            {.slot_index = 4, .stack = GamePlayerItemStack::item("iron", 4)},
        },
    };
    auto encoded_delta = encode_game_inventory_delta(delta);
    ASSERT_TRUE(encoded_delta) << encoded_delta.error().format();
    EXPECT_LT(encoded_delta->size(), encoded_snapshot->size());
    auto decoded_delta = decode_game_inventory_replication_payload(*encoded_delta);
    ASSERT_TRUE(decoded_delta) << decoded_delta.error().format();
    ASSERT_TRUE(std::holds_alternative<GameInventoryDelta>(*decoded_delta));
    const auto& round_trip_delta = std::get<GameInventoryDelta>(*decoded_delta);
    ASSERT_EQ(round_trip_delta.changed_slots.size(), 2u);
    EXPECT_EQ(round_trip_delta.changed_slots[0].slot_index, 0u);
    EXPECT_EQ(round_trip_delta.changed_slots[1].slot_index, 4u);
    EXPECT_EQ(round_trip_delta.response.request_id, 41u);

    GameInventoryDelta duplicate_slot = delta;
    duplicate_slot.changed_slots.push_back({.slot_index = 0, .stack = {}});
    EXPECT_FALSE(encode_game_inventory_delta(duplicate_slot));
}

TEST(GameMachineInputSlotTransferProtocolTest, RoundTripsAndRejectsMalformedPayloads) {
    const GameMachineInputSlotTransferCommand command{
        .request_id = 71,
        .expected_inventory_revision = 9,
        .direction = GameMachineInputSlotTransferDirection::kPlayerToMachineInput,
        .dimension_id = "overworld",
        .root_x = -4,
        .root_y = 12,
        .root_z = 7,
        .expected_material = 10,
        .player_slot = 3,
        .machine_input_slot = 1,
        .count = 2,
        .expected_player_slot = GamePlayerItemStack::item("iron_ore", 5),
        .expected_machine_input_slot = GamePlayerItemStack::item("iron_ore", 1),
    };
    auto encoded = make_game_machine_input_slot_transfer_command(44, command);
    ASSERT_TRUE(encoded) << encoded.error().format();
    EXPECT_EQ(encoded->client_sequence, 44u);
    auto decoded = parse_game_machine_input_slot_transfer_command(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_EQ(decoded->request_id, command.request_id);
    EXPECT_EQ(decoded->expected_inventory_revision, command.expected_inventory_revision);
    EXPECT_EQ(decoded->direction, command.direction);
    EXPECT_EQ(decoded->dimension_id, command.dimension_id);
    EXPECT_EQ(decoded->root_x, command.root_x);
    EXPECT_EQ(decoded->root_y, command.root_y);
    EXPECT_EQ(decoded->root_z, command.root_z);
    EXPECT_EQ(decoded->expected_material, command.expected_material);
    EXPECT_EQ(decoded->player_slot, command.player_slot);
    EXPECT_EQ(decoded->machine_input_slot, command.machine_input_slot);
    EXPECT_EQ(decoded->count, command.count);
    EXPECT_EQ(decoded->expected_player_slot, command.expected_player_slot);
    EXPECT_EQ(decoded->expected_machine_input_slot, command.expected_machine_input_slot);

    auto trailing = *encoded;
    trailing.payload.push_back(std::byte{0});
    EXPECT_FALSE(parse_game_machine_input_slot_transfer_command(trailing));

    GameMachineInputSlotTransferCommand machine_instance = command;
    machine_instance.expected_machine_input_slot.instance_data = "not-supported";
    EXPECT_FALSE(make_game_machine_input_slot_transfer_command(45, machine_instance));

    GameMachineInputSlotTransferCommand unavailable_slot = command;
    unavailable_slot.machine_input_slot =
        static_cast<uint16_t>(snt::game::replication::kMaxGameMachineInputSlots);
    EXPECT_FALSE(make_game_machine_input_slot_transfer_command(46, unavailable_slot));
}

TEST(GameClientInventoryStateTest, ReconstructsDeltasAndRejectsWrongAccount) {
    const GameInventorySnapshot initial = make_inventory_snapshot("local-name:InventoryClient");
    auto encoded_initial = encode_game_inventory_snapshot(initial);
    ASSERT_TRUE(encoded_initial) << encoded_initial.error().format();

    GameClientInventoryState state(initial.account_id);
    GameSnapshot full{
        .snapshot_id = 55,
        .values = {{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = *encoded_initial,
        }},
    };
    ASSERT_TRUE(state.apply(full));
    ASSERT_NE(state.snapshot(), nullptr);
    EXPECT_EQ(state.snapshot()->inventory.slots[0], initial.inventory.slots[0]);

    GameInventoryDelta changed{
        .account_id = initial.account_id,
        .inventory_revision = 2,
        .response_revision = 1,
        .response = {
            .request_id = 7,
            .kind = GameInventoryCommandKind::kInventorySlotTransfer,
            .outcome = GameInventorySlotTransferOutcome::kAccepted,
        },
        .changed_slots = {
            {.slot_index = 0, .stack = GamePlayerItemStack::item("iron", 7)},
            {.slot_index = 1, .stack = GamePlayerItemStack::item("iron", 5)},
        },
    };
    auto encoded_changed = encode_game_inventory_delta(changed);
    ASSERT_TRUE(encoded_changed) << encoded_changed.error().format();
    GameDelta delta{
        .base_snapshot_id = 55,
        .sequence = 1,
        .values = {{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*encoded_changed),
        }},
    };
    ASSERT_TRUE(state.apply(delta));
    ASSERT_NE(state.snapshot(), nullptr);
    EXPECT_EQ(state.snapshot()->inventory_revision, 2u);
    EXPECT_EQ(state.snapshot()->inventory.slots[0], GamePlayerItemStack::item("iron", 7));
    EXPECT_EQ(state.snapshot()->inventory.slots[1], GamePlayerItemStack::item("iron", 5));
    EXPECT_EQ(state.snapshot()->inventory.slots[7], initial.inventory.slots[7]);
    EXPECT_EQ(state.snapshot()->response.request_id, 7u);

    GameInventoryDelta revision_only{
        .account_id = initial.account_id,
        .inventory_revision = 3,
        .response_revision = 1,
    };
    auto encoded_revision_only = encode_game_inventory_delta(revision_only);
    ASSERT_TRUE(encoded_revision_only) << encoded_revision_only.error().format();
    GameDelta revision_only_delta{
        .base_snapshot_id = 55,
        .sequence = 2,
        .values = {{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*encoded_revision_only),
        }},
    };
    ASSERT_TRUE(state.apply(revision_only_delta));
    ASSERT_NE(state.snapshot(), nullptr);
    EXPECT_EQ(state.snapshot()->inventory_revision, 3u);
    EXPECT_EQ(state.snapshot()->inventory.slots[0], GamePlayerItemStack::item("iron", 7));

    GameInventoryDelta wrong_account = changed;
    wrong_account.account_id = "local-name:SomeoneElse";
    wrong_account.inventory_revision = 4;
    wrong_account.response_revision = 1;
    wrong_account.response = {};
    wrong_account.changed_slots = {
        {.slot_index = 2, .stack = GamePlayerItemStack::item("coal", 1)},
    };
    auto encoded_wrong_account = encode_game_inventory_delta(wrong_account);
    ASSERT_TRUE(encoded_wrong_account) << encoded_wrong_account.error().format();
    GameDelta rejected{
        .base_snapshot_id = 55,
        .sequence = 3,
        .values = {{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*encoded_wrong_account),
        }},
    };
    EXPECT_FALSE(state.apply(rejected));
}

TEST(GameServerInventoryReplicationTest, KeepsPendingDeltaUntilReliableCommit) {
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(
        world,
        {
            .inventory_slots = 4,
            .inventory_max_stack_size = 64,
        });
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer peer = make_peer(61, "Inventory Source");
    ASSERT_TRUE((*players)->on_peer_authenticated(peer, (*players)->default_persistent_state()));
    ASSERT_TRUE((*players)->apply_inventory_transaction(
        peer, {.additions = {GamePlayerItemStack::item("iron", 6)}}));

    CountingCheckpointSink checkpoint;
    auto source = GameServerInventoryReplication::create(**players, &checkpoint);
    ASSERT_TRUE(source) << source.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 4, .delta_seconds = 0.05f};
    auto initial_values = (*source)->collect_values(
        peer, GameReplicationInterest{}, inventory_budget(), context,
        GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(initial_values) << initial_values.error().format();
    ASSERT_EQ(initial_values->size(), 1u);
    auto initial_payload = decode_game_inventory_replication_payload(initial_values->front().payload);
    ASSERT_TRUE(initial_payload) << initial_payload.error().format();
    ASSERT_TRUE(std::holds_alternative<GameInventorySnapshot>(*initial_payload));
    const GameInventorySnapshot initial = std::get<GameInventorySnapshot>(*initial_payload);
    EXPECT_EQ(initial.inventory_revision, 1u);
    (*source)->on_values_committed(
        peer, GameReplicationValueCollectionPhase::kInitialSnapshot, *initial_values);

    ASSERT_TRUE((*source)->submit_slot_transfer(
        peer,
        {
            .request_id = 11,
            .expected_inventory_revision = initial.inventory_revision,
            .source_slot = 0,
            .target_slot = 2,
            .count = 2,
            .expected_source = initial.inventory.slots[0],
            .expected_target = initial.inventory.slots[2],
        }));
    EXPECT_EQ(checkpoint.mark_count, 1u);
    EXPECT_EQ(checkpoint.last_peer, peer.peer);

    auto pending_values = (*source)->collect_values(
        peer, GameReplicationInterest{}, inventory_budget(), context,
        GameReplicationValueCollectionPhase::kDelta);
    ASSERT_TRUE(pending_values) << pending_values.error().format();
    auto retry_values = (*source)->collect_values(
        peer, GameReplicationInterest{}, inventory_budget(), context,
        GameReplicationValueCollectionPhase::kDelta);
    ASSERT_TRUE(retry_values) << retry_values.error().format();
    ASSERT_EQ(pending_values->size(), 1u);
    ASSERT_EQ(retry_values->size(), 1u);
    EXPECT_EQ(retry_values->front().payload, pending_values->front().payload);
    auto pending_payload = decode_game_inventory_replication_payload(pending_values->front().payload);
    ASSERT_TRUE(pending_payload) << pending_payload.error().format();
    ASSERT_TRUE(std::holds_alternative<GameInventoryDelta>(*pending_payload));
    const GameInventoryDelta accepted = std::get<GameInventoryDelta>(*pending_payload);
    EXPECT_EQ(accepted.inventory_revision, 2u);
    EXPECT_EQ(accepted.response.outcome, GameInventorySlotTransferOutcome::kAccepted);
    ASSERT_EQ(accepted.changed_slots.size(), 2u);
    (*source)->on_values_committed(
        peer, GameReplicationValueCollectionPhase::kDelta, *pending_values);

    ASSERT_TRUE((*source)->submit_slot_transfer(
        peer,
        {
            .request_id = 12,
            .expected_inventory_revision = 1,
            .source_slot = 0,
            .target_slot = 2,
            .count = 1,
            .expected_source = initial.inventory.slots[0],
            .expected_target = initial.inventory.slots[2],
        }));
    EXPECT_EQ(checkpoint.mark_count, 1u);
    auto rejected_values = (*source)->collect_values(
        peer, GameReplicationInterest{}, inventory_budget(), context,
        GameReplicationValueCollectionPhase::kDelta);
    ASSERT_TRUE(rejected_values) << rejected_values.error().format();
    auto rejected_payload = decode_game_inventory_replication_payload(rejected_values->front().payload);
    ASSERT_TRUE(rejected_payload) << rejected_payload.error().format();
    ASSERT_TRUE(std::holds_alternative<GameInventoryDelta>(*rejected_payload));
    const GameInventoryDelta rejected = std::get<GameInventoryDelta>(*rejected_payload);
    EXPECT_TRUE(rejected.changed_slots.empty());
    EXPECT_EQ(rejected.response.outcome, GameInventorySlotTransferOutcome::kRejected);

    (*players)->shutdown();
}

TEST(GameServerCommandSinkTest, RoutesInventoryTransferToAuthoritativeSource) {
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(
        world,
        {
            .inventory_slots = 3,
            .inventory_max_stack_size = 64,
        });
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer peer = make_peer(62, "Inventory Route");
    ASSERT_TRUE((*players)->on_peer_authenticated(peer, (*players)->default_persistent_state()));
    ASSERT_TRUE((*players)->apply_inventory_transaction(
        peer, {.additions = {GamePlayerItemStack::item("iron", 3)}}));
    auto source = GameServerInventoryReplication::create(**players);
    ASSERT_TRUE(source) << source.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 9, .delta_seconds = 0.05f};
    auto initial_values = (*source)->collect_values(
        peer, GameReplicationInterest{}, inventory_budget(), context,
        GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(initial_values) << initial_values.error().format();
    auto initial_payload = decode_game_inventory_replication_payload(initial_values->front().payload);
    ASSERT_TRUE(initial_payload) << initial_payload.error().format();
    const GameInventorySnapshot initial = std::get<GameInventorySnapshot>(*initial_payload);
    (*source)->on_values_committed(
        peer, GameReplicationValueCollectionPhase::kInitialSnapshot, *initial_values);

    GameContentRegistry content;
    QuestRegistry quests(content);
    GameServerCommandSink sink(quests, nullptr, nullptr, source->get());
    auto command = make_game_inventory_slot_transfer_command(
        31,
        {
            .request_id = 91,
            .expected_inventory_revision = initial.inventory_revision,
            .source_slot = 0,
            .target_slot = 1,
            .count = 1,
            .expected_source = initial.inventory.slots[0],
            .expected_target = initial.inventory.slots[1],
        });
    ASSERT_TRUE(command) << command.error().format();
    ASSERT_TRUE(sink.enqueue_client_command(peer, std::move(*command), context));
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index));

    auto inventory = (*players)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0], GamePlayerItemStack::item("iron", 2));
    EXPECT_EQ(inventory->slots[1], GamePlayerItemStack::item("iron", 1));
    (*players)->shutdown();
}

}  // namespace
