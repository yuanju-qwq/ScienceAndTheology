// Authoritative player AOI, payload codec, and client value-world tests.

#include "game/player/player_replication.h"
#include "game/server/game_server_player_replication.h"
#include "game/server/game_server_player_state.h"
#include "game/world/game_chunk.h"

#include "ecs/world.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
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
using snt::game::replication::GameReplicationInterest;
using snt::game::replication::GameReplicationValue;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameReplicatedPlayerState;
using snt::game::replication::GameServerPlayerReplication;
using snt::game::replication::GameServerPlayerReplicationConfig;
using snt::game::replication::GameServerPlayerState;
using snt::game::replication::IGameReplicationValueSource;

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

class MutableQuestBookValueSource final : public IGameReplicationValueSource {
public:
    snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer&, const GameReplicationInterest&,
        const snt::game::replication::GameReplicationBudget&,
        const snt::network::ReplicationTickContext&,
        snt::game::replication::GameReplicationValueCollectionPhase) override {
        ++collect_call_count;
        if (!present) return std::vector<GameReplicationValue>{};
        std::vector<std::byte> bytes;
        bytes.reserve(payload.size());
        for (const char value : payload) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        }
        return std::vector<GameReplicationValue>{
            {
                .kind = GameReplicationValueKind::kQuestBook,
                .operation = GameReplicationValueOperation::kUpsert,
                .payload = std::move(bytes),
            },
        };
    }

    void on_peer_disconnected(const GameAuthenticatedPeer&,
                              std::string_view) noexcept override {
        ++disconnect_call_count;
    }

    void on_values_committed(
        const GameAuthenticatedPeer&,
        snt::game::replication::GameReplicationValueCollectionPhase phase,
        std::span<const GameReplicationValue> values) noexcept override {
        ++commit_call_count;
        last_commit_phase = phase;
        last_committed_values.assign(values.begin(), values.end());
    }

    bool present = true;
    std::string payload = "task-book-revision-1";
    int collect_call_count = 0;
    int disconnect_call_count = 0;
    int commit_call_count = 0;
    snt::game::replication::GameReplicationValueCollectionPhase last_commit_phase =
        snt::game::replication::GameReplicationValueCollectionPhase::kInitialSnapshot;
    std::vector<GameReplicationValue> last_committed_values;
};

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
    snt::voxel::ChunkRegistry chunks;
    snt::game::GameChunkSidecarRegistry sidecars;
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
        **players, world, chunks, sidecars,
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
    snt::voxel::ChunkRegistry chunks;
    snt::game::GameChunkSidecarRegistry sidecars;
    auto players = GameServerPlayerState::create(world);
    ASSERT_TRUE(players) << players.error().format();

    const GameAuthenticatedPeer observer = make_peer(201, "Observer");
    const GameAuthenticatedPeer nearby = make_peer(202, "Nearby");
    ASSERT_TRUE((*players)->on_peer_authenticated(observer, state_at(**players, "overworld", 0, 64, 0)));
    ASSERT_TRUE((*players)->on_peer_authenticated(nearby, state_at(**players, "overworld", 1, 64, 0)));
    auto replication = GameServerPlayerReplication::create(
        **players, world, chunks, sidecars,
        {.horizontal_aoi_radius_blocks = 16, .vertical_aoi_radius_blocks = 8,
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

TEST(GameServerPlayerReplicationTest, ReplicatesValueBaselinesWithoutRepeatingUnchangedValues) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    snt::game::GameChunkSidecarRegistry sidecars;
    auto players = GameServerPlayerState::create(world);
    ASSERT_TRUE(players) << players.error().format();

    const GameAuthenticatedPeer observer = make_peer(301, "QuestObserver");
    ASSERT_TRUE((*players)->on_peer_authenticated(
        observer, state_at(**players, "overworld", 0, 64, 0)));
    MutableQuestBookValueSource task_book_source;
    auto replication = GameServerPlayerReplication::create(
        **players, world, chunks, sidecars,
        {
            .horizontal_aoi_radius_blocks = 16,
            .vertical_aoi_radius_blocks = 8,
            .max_visible_players = 8,
        },
        {&task_book_source});
    ASSERT_TRUE(replication) << replication.error().format();

    const snt::network::ReplicationTickContext context{.tick_index = 12, .delta_seconds = 0.05f};
    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_entity_snapshots_per_tick = 8,
        .max_value_snapshots_per_tick = 1,
    };
    auto interest = (*replication)->compute_interest(observer, context);
    ASSERT_TRUE(interest) << interest.error().format();
    auto initial_messages = (*replication)->build_initial_snapshot(observer, *interest, budget, context);
    ASSERT_TRUE(initial_messages) << initial_messages.error().format();
    ASSERT_EQ(initial_messages->size(), 1u);
    auto initial = snt::game::replication::parse_game_snapshot(initial_messages->front());
    ASSERT_TRUE(initial) << initial.error().format();
    ASSERT_EQ(initial->values.size(), 1u);
    EXPECT_EQ(initial->values.front().kind, GameReplicationValueKind::kQuestBook);
    EXPECT_EQ(task_book_source.commit_call_count, 1);
    EXPECT_EQ(task_book_source.last_commit_phase,
              snt::game::replication::GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_EQ(task_book_source.last_committed_values.size(), 1u);

    auto unchanged = (*replication)->build_deltas(observer, *interest, budget, context);
    ASSERT_TRUE(unchanged) << unchanged.error().format();
    EXPECT_TRUE(unchanged->empty());
    EXPECT_EQ(task_book_source.commit_call_count, 1);

    task_book_source.payload = "task-book-revision-2";
    auto changed = (*replication)->build_deltas(observer, *interest, budget, context);
    ASSERT_TRUE(changed) << changed.error().format();
    ASSERT_EQ(changed->size(), 1u);
    auto delta = snt::game::replication::parse_game_delta(changed->front());
    ASSERT_TRUE(delta) << delta.error().format();
    EXPECT_EQ(delta->sequence, 1u);
    ASSERT_EQ(delta->values.size(), 1u);
    EXPECT_EQ(delta->values.front().operation, GameReplicationValueOperation::kUpsert);
    EXPECT_EQ(task_book_source.commit_call_count, 2);
    EXPECT_EQ(task_book_source.last_commit_phase,
              snt::game::replication::GameReplicationValueCollectionPhase::kDelta);

    auto unchanged_after_delta = (*replication)->build_deltas(observer, *interest, budget, context);
    ASSERT_TRUE(unchanged_after_delta) << unchanged_after_delta.error().format();
    EXPECT_TRUE(unchanged_after_delta->empty());

    task_book_source.present = false;
    auto removal = (*replication)->build_deltas(observer, *interest, budget, context);
    ASSERT_TRUE(removal) << removal.error().format();
    ASSERT_EQ(removal->size(), 1u);
    auto removal_delta = snt::game::replication::parse_game_delta(removal->front());
    ASSERT_TRUE(removal_delta) << removal_delta.error().format();
    EXPECT_EQ(removal_delta->sequence, 2u);
    ASSERT_EQ(removal_delta->values.size(), 1u);
    EXPECT_EQ(removal_delta->values.front().operation, GameReplicationValueOperation::kRemove);
    EXPECT_TRUE(removal_delta->values.front().payload.empty());
    EXPECT_EQ(task_book_source.commit_call_count, 3);
    ASSERT_EQ(task_book_source.last_committed_values.size(), 1u);
    EXPECT_EQ(task_book_source.last_committed_values.front().operation,
              GameReplicationValueOperation::kRemove);

    (*replication)->on_peer_disconnected(observer, "test completed");
    EXPECT_EQ(task_book_source.disconnect_call_count, 1);
    (*players)->shutdown();
}

TEST(GameChunkReplicationTest, AppliesChunkSnapshotsDeltasAndRestoresLocalBootstrapOnRemoval) {
    const snt::voxel::ChunkKey key{"overworld", 0, 0, 0};
    snt::voxel::ChunkRegistry chunks;
    snt::voxel::VoxelChunk local;
    local.terrain.resize(2, 1, 1);
    local.terrain.cells[0] = {.material = 1, .flags = snt::voxel::TF_SOLID};
    local.terrain.cells[1] = {.material = 1, .flags = snt::voxel::TF_SOLID};
    chunks.set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z, local);

    snt::voxel::VoxelChunk authoritative = local;
    authoritative.terrain.cells[0] = {.material = 3, .flags = snt::voxel::TF_MINEABLE};
    authoritative.terrain.cells[1] = {.material = 3, .flags = snt::voxel::TF_MINEABLE};
    auto payload = snt::game::replication::encode_game_terrain_chunk_snapshot(authoritative);
    ASSERT_TRUE(payload) << payload.error().format();
    auto decoded = snt::game::replication::decode_game_terrain_chunk_snapshot(*payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    ASSERT_EQ(decoded->cells.size(), 2u);
    EXPECT_EQ(decoded->cells[0].material, 3u);

    snt::game::replication::GameClientRemoteChunkWorld remote_chunks(chunks);
    const snt::game::replication::GameSnapshot snapshot{
        .snapshot_id = 71,
        .chunks = {{.chunk = key, .payload = *payload}},
    };
    ASSERT_TRUE(remote_chunks.apply(snapshot));
    ASSERT_EQ(remote_chunks.chunk_count(), 1u);
    const snt::voxel::VoxelChunk* applied = chunks.get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->terrain.cells[0].material, 3u);

    const snt::game::replication::GameDelta delta{
        .base_snapshot_id = 71,
        .sequence = 1,
        .chunks = {{.chunk = key,
                    .blocks = {{.local_index = 1,
                                .material = 4,
                                .flags = snt::voxel::TF_WALKABLE}}}},
    };
    ASSERT_TRUE(remote_chunks.apply(delta));
    applied = chunks.get_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->terrain.cells[1].material, 4u);
    EXPECT_EQ(applied->terrain.cells[1].flags, static_cast<uint32_t>(snt::voxel::TF_WALKABLE));

    const snt::game::replication::GameDelta removal{
        .base_snapshot_id = 71,
        .sequence = 2,
        .removed_chunks = {key},
    };
    ASSERT_TRUE(remote_chunks.apply(removal));
    applied = chunks.get_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->terrain.cells[0].material, 1u);
    EXPECT_EQ(applied->terrain.cells[1].material, 1u);
}

TEST(GameMachineReplicationTest, RoundTripsPresentationStateAndAppliesOrderedRemoval) {
    const snt::game::replication::GameReplicatedMachineState state{
        .anchor_chunk = {"overworld", 0, 0, 0},
        .root_x = 4,
        .root_y = 8,
        .root_z = -2,
        .machine_id = "primitive_furnace",
        .input_slots = {{.item_id = "iron_crushed", .count = 2}, {}},
        .output_slots = {{.item_id = "iron_ingot", .count = 1}},
        .max_input_slots = 6,
        .max_output_slots = 3,
        .stored_energy = 20,
        .energy_capacity = 100,
        .progress_ticks = 30,
        .active_recipe_duration_ticks = 80,
        .run_state = 1,
    };
    auto payload = snt::game::replication::encode_game_machine_replication_entity({
        .operation = snt::game::replication::GameMachineReplicationOperation::kUpsert,
        .machine = state,
    });
    ASSERT_TRUE(payload) << payload.error().format();
    auto decoded = snt::game::replication::decode_game_machine_replication_entity(*payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    ASSERT_TRUE(decoded->machine.has_value());
    EXPECT_EQ(decoded->machine->machine_id, "primitive_furnace");
    EXPECT_EQ(decoded->machine->input_slots.size(), 2u);
    EXPECT_EQ(decoded->machine->max_input_slots, 6u);
    EXPECT_EQ(decoded->machine->max_output_slots, 3u);

    snt::game::replication::GameRemoteMachineWorld machines;
    ASSERT_TRUE(machines.apply(snt::game::replication::GameSnapshot{
        .snapshot_id = 91,
        .entities = {{.entity_guid = {.value = 9001}, .payload = *payload}},
    }));
    ASSERT_EQ(machines.machine_count(), 1u);
    const auto found = machines.find_machine_at("overworld", 4, 8, -2);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->entity_guid.value, 9001u);
    EXPECT_EQ(found->machine.machine_id, "primitive_furnace");
    EXPECT_FALSE(machines.find_machine_at("overworld", 4, 8, -1).has_value());
    auto remove_payload = snt::game::replication::encode_game_machine_replication_entity({
        .operation = snt::game::replication::GameMachineReplicationOperation::kRemove,
    });
    ASSERT_TRUE(remove_payload) << remove_payload.error().format();
    ASSERT_TRUE(machines.apply(snt::game::replication::GameDelta{
        .base_snapshot_id = 91,
        .sequence = 1,
        .entities = {{.entity_guid = {.value = 9001}, .payload = *remove_payload}},
    }));
    EXPECT_EQ(machines.machine_count(), 0u);
    EXPECT_FALSE(machines.find_machine_at("overworld", 4, 8, -2).has_value());
}

TEST(GameServerPlayerReplicationTest, EmitsCommittedBlockDeltaForVisibleAuthoritativeChunk) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    snt::game::GameChunkSidecarRegistry sidecars;
    const snt::voxel::ChunkKey key{"overworld", 0, 0, 0};
    snt::voxel::VoxelChunk terrain;
    terrain.terrain.resize(2, 1, 1);
    terrain.terrain.cells[0] = {.material = 1, .flags = snt::voxel::TF_SOLID};
    terrain.terrain.cells[1] = {.material = 1, .flags = snt::voxel::TF_SOLID};
    chunks.set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z, std::move(terrain));

    auto players = GameServerPlayerState::create(world);
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer observer = make_peer(701, "TerrainObserver");
    ASSERT_TRUE((*players)->on_peer_authenticated(
        observer, state_at(**players, "overworld", 0, 0, 0)));
    auto replication = GameServerPlayerReplication::create(
        **players, world, chunks, sidecars,
        {
            .horizontal_aoi_radius_blocks = 16,
            .vertical_aoi_radius_blocks = 16,
            .max_visible_players = 4,
            .chunk_horizontal_aoi_radius_blocks = 32,
            .chunk_vertical_aoi_radius_blocks = 32,
            .max_visible_chunks = 1,
        });
    ASSERT_TRUE(replication) << replication.error().format();
    const snt::network::ReplicationTickContext context{.tick_index = 19, .delta_seconds = 0.05f};
    auto interest = (*replication)->compute_interest(observer, context);
    ASSERT_TRUE(interest) << interest.error().format();
    ASSERT_EQ(interest->chunks.size(), 1u);
    const snt::game::replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_chunk_snapshots_per_tick = 1,
        .max_entity_snapshots_per_tick = 4,
        .max_block_deltas_per_tick = 4,
    };
    auto initial = (*replication)->build_initial_snapshot(observer, *interest, budget, context);
    ASSERT_TRUE(initial) << initial.error().format();
    ASSERT_EQ(initial->size(), 1u);
    auto parsed_initial = snt::game::replication::parse_game_snapshot(initial->front());
    ASSERT_TRUE(parsed_initial) << parsed_initial.error().format();
    ASSERT_EQ(parsed_initial->chunks.size(), 1u);

    snt::voxel::VoxelChunk* current = chunks.get_chunk(
        key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z);
    ASSERT_NE(current, nullptr);
    current->terrain.cells[0] = {.material = 4, .flags = snt::voxel::TF_MINEABLE};
    (*replication)->mark_block_dirty("overworld", 0, 0, 0);
    auto changed = (*replication)->build_deltas(observer, *interest, budget, context);
    ASSERT_TRUE(changed) << changed.error().format();
    ASSERT_EQ(changed->size(), 1u);
    auto parsed_delta = snt::game::replication::parse_game_delta(changed->front());
    ASSERT_TRUE(parsed_delta) << parsed_delta.error().format();
    ASSERT_EQ(parsed_delta->chunks.size(), 1u);
    ASSERT_EQ(parsed_delta->chunks.front().blocks.size(), 1u);
    EXPECT_EQ(parsed_delta->chunks.front().blocks.front().local_index, 0u);
    EXPECT_EQ(parsed_delta->chunks.front().blocks.front().material, 4u);
    (*players)->shutdown();
}

}  // namespace
