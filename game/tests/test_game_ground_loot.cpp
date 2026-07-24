// Durable ground-loot authority, persistence, replication, and creature-drop tests.

#include "game/client/game_content_registry.h"
#include "game/client/ground_loot_interaction.h"
#include "game/network/game_ground_loot_replication.h"
#include "game/player/player_identity.h"
#include "game/server/game_server_creature_interaction.h"
#include "game/server/game_server_ground_loot.h"
#include "game/server/game_server_ground_loot_replication.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"
#include "game/simulation/ecosystem_system.h"
#include "game/simulation/wild_creature_system.h"
#include "game/world/save/chunk_serializer.h"

#include "ecs/world.h"
#include "voxel/data/chunk_registry.h"
#include "voxel/data/voxel_chunk.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::ChunkKey;
using snt::game::CreatureRole;
using snt::game::GameChunk;
using snt::game::GameChunkSidecar;
using snt::game::GameChunkSidecarRegistry;
using snt::game::GameContentRegistry;
using snt::game::GameClientGroundLootInteractionController;
using snt::game::GameClientGroundLootInteractionInput;
using snt::game::GameClientGroundLootRaycast;
using snt::game::GameEcosystemSystem;
using snt::game::GameEcosystemWildProxyRebalanceRequest;
using snt::game::GameGroundLootRecord;
using snt::game::GameItemDefinition;
using snt::game::GameWildCreatureConfig;
using snt::game::GameWildCreatureSystem;
using snt::game::ResourceContentStack;
using snt::game::WorldGenConfigSnapshot;
using snt::game::builtin_creature_species;
using snt::game::finalize_world_gen_config;
using snt::game::make_local_name_player_identity;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameClientCommand;
using snt::game::replication::GameCreatureAttackCommand;
using snt::game::replication::GameDelta;
using snt::game::replication::GameGroundLootPickupCommand;
using snt::game::replication::GameGroundLootPresentationSnapshot;
using snt::game::replication::GameGroundLootPresentationState;
using snt::game::replication::GameGroundLootSpawnRequest;
using snt::game::replication::GameRemoteGroundLootWorld;
using snt::game::replication::GameReplicationBudget;
using snt::game::replication::GameReplicationInterest;
using snt::game::replication::GameReplicationValueCollectionPhase;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameServerCreatureInteractionConfig;
using snt::game::replication::GameServerCreatureInteractionService;
using snt::game::replication::GameServerGroundLootReplication;
using snt::game::replication::GameServerGroundLootReplicationConfig;
using snt::game::replication::GameServerGroundLootService;
using snt::game::replication::GameServerPlayerState;
using snt::game::replication::IGameServerGroundLootStateSink;
using snt::game::replication::IGameServerPlayerStateCheckpointSink;
using snt::game::replication::decode_game_ground_loot_presentation_snapshot;
using snt::game::replication::encode_game_ground_loot_presentation_snapshot;
using snt::game::replication::make_game_ground_loot_pickup_command;
using snt::game::replication::parse_game_ground_loot_pickup_command;
using snt::game::IGameClientGroundLootInteractionCommandSink;
using snt::game::pick_game_client_ground_loot_interaction_target;

GameItemDefinition make_item(std::string id) {
    return {
        .id = std::move(id),
        .title_key = "item.test",
        .max_stack = 64,
    };
}

GameAuthenticatedPeer make_peer(snt::network::PeerId peer_id, std::string name) {
    auto identity = make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

void add_loaded_chunk(snt::voxel::ChunkRegistry& chunks, const ChunkKey& key,
                      int terrain_extent = 1) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(terrain_extent, terrain_extent, terrain_extent);
    chunks.set_chunk(key.dimension_id, key.chunk_x, key.chunk_y, key.chunk_z,
                     std::move(chunk));
}

WorldGenConfigSnapshot make_worldgen_config() {
    WorldGenConfigSnapshot config;
    const auto add_material = [&config](const char* key, uint32_t flags) {
        snt::game::TerrainMaterialDef material;
        material.key = key;
        material.flags = flags;
        config.materials.push_back(std::move(material));
    };
    add_material("snt:air", 0);
    add_material("snt:dirt", static_cast<uint32_t>(snt::voxel::TF_SOLID));
    add_material("snt:sand", static_cast<uint32_t>(snt::voxel::TF_SOLID));
    add_material("snt:stone", static_cast<uint32_t>(snt::voxel::TF_SOLID));
    add_material("snt:water", 0);
    config.role_keys.air = "snt:air";
    config.role_keys.dirt = "snt:dirt";
    config.role_keys.sand = "snt:sand";
    config.role_keys.stone = "snt:stone";
    config.role_keys.water = "snt:water";
    EXPECT_TRUE(finalize_world_gen_config(config));
    return config;
}

GameEcosystemWildProxyRebalanceRequest creature_request(
    const ChunkKey& chunk, uint64_t source_tick, uint64_t creature_id) {
    return {
        .chunk = chunk,
        .source_tick = source_tick,
        .proxies = {{
            .stable_id = creature_id,
            .species_id = 1,
            .role = CreatureRole::HERBIVORE,
            .slot = 0,
        }},
    };
}

GameGroundLootPresentationState presentation(uint64_t loot_id, int32_t chunk_x = 0) {
    return {
        .loot_id = loot_id,
        .chunk = ChunkKey{"overworld", chunk_x, 0, 0},
        .resource = ResourceContentStack::item("iron", 2),
        .position_x = static_cast<float>(chunk_x * snt::voxel::VoxelChunk::kChunkSize + 2),
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 9,
    };
}

GameReplicationBudget replication_budget() {
    return {
        .max_reliable_bytes_per_tick = 4096,
        .max_value_snapshots_per_tick = 1,
    };
}

struct CheckpointSink final : IGameServerPlayerStateCheckpointSink {
    int marks = 0;

    snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer&) override {
        ++marks;
        return {};
    }
};

struct GroundLootStateSink final : IGameServerGroundLootStateSink {
    std::vector<uint64_t> ticks;

    void on_ground_loot_state_changed(uint64_t source_tick) noexcept override {
        ticks.push_back(source_tick);
    }
};

struct GroundLootCommandSink final : IGameClientGroundLootInteractionCommandSink {
    std::vector<GameGroundLootPickupCommand> commands;

    snt::core::Expected<void> submit_ground_loot_pickup(
        GameGroundLootPickupCommand command) override {
        commands.push_back(command);
        return {};
    }
};

}  // namespace

TEST(GameGroundLootPersistenceTest, RoundTripsDurableRecordsAndRejectsInvalidRecords) {
    GameChunk chunk;
    chunk.terrain.resize(1, 1, 1);
    chunk.next_ground_loot_serial = 42;
    chunk.ground_loot = {{
        .loot_id = 17,
        .resource = ResourceContentStack::item("iron", 3),
        .position_x = 4.5f,
        .position_y = 1.25f,
        .position_z = -2.0f,
        .spawned_tick = 99,
    }};

    const snt::game::GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", chunk);
    GameChunk restored;
    std::string dimension;
    ASSERT_TRUE(serializer.deserialize(payload, dimension, restored));
    EXPECT_EQ(dimension, "overworld");
    EXPECT_EQ(restored.next_ground_loot_serial, 42u);
    ASSERT_EQ(restored.ground_loot.size(), 1u);
    const GameGroundLootRecord& restored_record = restored.ground_loot.front();
    const GameGroundLootRecord& original_record = chunk.ground_loot.front();
    EXPECT_EQ(restored_record.loot_id, original_record.loot_id);
    EXPECT_EQ(restored_record.resource, original_record.resource);
    EXPECT_FLOAT_EQ(restored_record.position_x, original_record.position_x);
    EXPECT_FLOAT_EQ(restored_record.position_y, original_record.position_y);
    EXPECT_FLOAT_EQ(restored_record.position_z, original_record.position_z);
    EXPECT_EQ(restored_record.spawned_tick, original_record.spawned_tick);

    GameChunk duplicate = chunk;
    duplicate.next_ground_loot_serial = 18;
    duplicate.ground_loot.push_back(duplicate.ground_loot.front());
    EXPECT_FALSE(serializer.deserialize(serializer.serialize("overworld", duplicate), dimension,
                                        restored));

    GameChunk non_finite = chunk;
    non_finite.ground_loot.front().position_x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(serializer.deserialize(serializer.serialize("overworld", non_finite), dimension,
                                        restored));
}

TEST(GameGroundLootProtocolTest, RoundTripsPickupAndReplacesRemotePresentationSet) {
    auto command = make_game_ground_loot_pickup_command(71, {.loot_id = 44});
    ASSERT_TRUE(command) << command.error().format();
    auto parsed = parse_game_ground_loot_pickup_command(*command);
    ASSERT_TRUE(parsed) << parsed.error().format();
    EXPECT_EQ(parsed->loot_id, 44u);

    GameClientCommand malformed = *command;
    malformed.payload.pop_back();
    EXPECT_FALSE(parse_game_ground_loot_pickup_command(malformed));

    const GameGroundLootPresentationSnapshot initial{
        .source_tick = 8,
        .loot = {presentation(10)},
    };
    auto initial_payload = encode_game_ground_loot_presentation_snapshot(initial);
    ASSERT_TRUE(initial_payload) << initial_payload.error().format();
    auto decoded = decode_game_ground_loot_presentation_snapshot(*initial_payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    ASSERT_EQ(decoded->loot.size(), 1u);
    EXPECT_EQ(decoded->loot.front().loot_id, initial.loot.front().loot_id);
    EXPECT_EQ(decoded->loot.front().chunk, initial.loot.front().chunk);
    EXPECT_EQ(decoded->loot.front().resource, initial.loot.front().resource);
    EXPECT_FLOAT_EQ(decoded->loot.front().position_x, initial.loot.front().position_x);
    EXPECT_FLOAT_EQ(decoded->loot.front().position_y, initial.loot.front().position_y);
    EXPECT_FLOAT_EQ(decoded->loot.front().position_z, initial.loot.front().position_z);
    EXPECT_EQ(decoded->loot.front().spawned_tick, initial.loot.front().spawned_tick);

    GameRemoteGroundLootWorld remote;
    ASSERT_TRUE(remote.apply({
        .snapshot_id = 91,
        .values = {{
            .kind = GameReplicationValueKind::kGroundLootPresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*initial_payload),
        }},
    }));
    ASSERT_TRUE(remote.find_loot(10).has_value());

    const GameGroundLootPresentationSnapshot replacement{
        .source_tick = 9,
        .loot = {presentation(20, 1)},
    };
    auto replacement_payload = encode_game_ground_loot_presentation_snapshot(replacement);
    ASSERT_TRUE(replacement_payload) << replacement_payload.error().format();
    ASSERT_TRUE(remote.apply(GameDelta{
        .base_snapshot_id = 91,
        .sequence = 1,
        .values = {{
            .kind = GameReplicationValueKind::kGroundLootPresentation,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*replacement_payload),
        }},
    }));
    EXPECT_FALSE(remote.find_loot(10).has_value());
    ASSERT_TRUE(remote.find_loot(20).has_value());
    EXPECT_EQ(remote.latest_source_tick(), 9u);

    auto unordered = initial;
    unordered.loot.push_back(initial.loot.front());
    EXPECT_FALSE(encode_game_ground_loot_presentation_snapshot(unordered));
}

TEST(GameGroundLootClientInteractionTest, SelectsVisibleLootAndSubmitsOnlyItsStableId) {
    GameGroundLootPresentationState near = presentation(20);
    near.position_x = 2.0f;
    near.position_y = 1.0f;
    near.position_z = 0.0f;
    GameGroundLootPresentationState far = presentation(10);
    far.position_x = 4.0f;
    far.position_y = 1.0f;
    far.position_z = 0.0f;
    const std::vector<GameGroundLootPresentationState> loot{far, near};
    const GameClientGroundLootRaycast raycast{
        .origin_x = 0.0f,
        .origin_y = 1.0f,
        .origin_z = 0.0f,
        .direction_x = 1.0f,
        .direction_y = 0.0f,
        .direction_z = 0.0f,
        .max_distance_blocks = 5.0f,
    };
    const auto target = pick_game_client_ground_loot_interaction_target(
        loot, "overworld", raycast);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->loot_id, near.loot_id);

    GroundLootCommandSink sink;
    GameClientGroundLootInteractionController controller;
    auto handled = controller.handle_input({.pickup_pressed = true}, target, sink);
    ASSERT_TRUE(handled) << handled.error().format();
    EXPECT_TRUE(*handled);
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands.front().loot_id, near.loot_id);

    GameClientGroundLootRaycast occluded = raycast;
    occluded.terrain_hit_distance_blocks = 1.0f;
    EXPECT_FALSE(pick_game_client_ground_loot_interaction_target(loot, "overworld", occluded));
}

TEST(GameGroundLootReplicationTest, UsesTerrainAoiAndIgnoresDuplicateInterestChunks) {
    GameChunkSidecarRegistry sidecars;
    const ChunkKey visible{"overworld", 0, 0, 0};
    const ChunkKey hidden{"overworld", 1, 0, 0};
    GameChunkSidecar visible_sidecar;
    visible_sidecar.next_ground_loot_serial = 12;
    visible_sidecar.ground_loot.push_back({
        .loot_id = 11,
        .resource = ResourceContentStack::item("iron", 1),
        .position_x = 2.0f,
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 4,
    });
    GameChunkSidecar hidden_sidecar;
    hidden_sidecar.next_ground_loot_serial = 13;
    hidden_sidecar.ground_loot.push_back({
        .loot_id = 12,
        .resource = ResourceContentStack::item("iron", 1),
        .position_x = 18.0f,
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 4,
    });
    sidecars.set(visible, std::move(visible_sidecar));
    sidecars.set(hidden, std::move(hidden_sidecar));

    auto source = GameServerGroundLootReplication::create(
        sidecars, GameServerGroundLootReplicationConfig{.max_visible_loot = 4});
    ASSERT_TRUE(source) << source.error().format();
    (*source)->on_ground_loot_state_changed(15);
    auto values = (*source)->collect_values(
        {}, {.chunks = {visible, visible}}, replication_budget(), {.tick_index = 16},
        GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(values) << values.error().format();
    ASSERT_EQ(values->size(), 1u);
    auto snapshot = decode_game_ground_loot_presentation_snapshot(values->front().payload);
    ASSERT_TRUE(snapshot) << snapshot.error().format();
    EXPECT_EQ(snapshot->source_tick, 15u);
    ASSERT_EQ(snapshot->loot.size(), 1u);
    EXPECT_EQ(snapshot->loot.front().loot_id, 11u);
}

TEST(GameGroundLootServiceTest, PicksUpOncePersistsAllocatorAndRejectsDistantLoot) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("iron")));
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = content.resource_runtime_index(),
            .spawn = {.dimension_id = "overworld", .position = {.x = 2, .y = 1, .z = 2}},
            .inventory_slots = 1,
            .inventory_max_stack_size = 5,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer peer = make_peer(701, "Ground Loot Player");
    ASSERT_TRUE((*players)->on_peer_authenticated(peer, (*players)->default_persistent_state()));

    snt::voxel::ChunkRegistry chunks;
    const ChunkKey chunk{"overworld", 0, 0, 0};
    add_loaded_chunk(chunks, chunk);
    GameChunkSidecarRegistry sidecars;
    CheckpointSink checkpoint;
    GroundLootStateSink state_sink;
    auto service = GameServerGroundLootService::create(
        *(*players), chunks, sidecars, content, &checkpoint, &state_sink);
    ASSERT_TRUE(service) << service.error().format();

    const GameGroundLootSpawnRequest nearby{
        .chunk = chunk,
        .resource = ResourceContentStack::item("iron", 2),
        .position_x = 2.0f,
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 7,
    };
    auto spawned = (*service)->spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest>(&nearby, 1));
    ASSERT_TRUE(spawned) << spawned.error().format();
    ASSERT_EQ(spawned->size(), 1u);
    EXPECT_EQ(spawned->front(), 1u);
    ASSERT_TRUE((*service)->pickup_ground_loot(peer, {.loot_id = spawned->front()}, 8));
    EXPECT_EQ((*service)->active_loot_count(), 0u);
    EXPECT_EQ(checkpoint.marks, 1);
    ASSERT_EQ(state_sink.ticks.size(), 2u);
    auto inventory = (*players)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    ASSERT_EQ(inventory->slots.size(), 1u);
    EXPECT_EQ(inventory->slots.front().resource.key.id, "iron");
    EXPECT_EQ(inventory->slots.front().resource.amount, 2);
    EXPECT_FALSE((*service)->pickup_ground_loot(peer, {.loot_id = spawned->front()}, 9));
    EXPECT_EQ(checkpoint.marks, 1);

    auto restarted = GameServerGroundLootService::create(*(*players), chunks, sidecars, content);
    ASSERT_TRUE(restarted) << restarted.error().format();
    const GameGroundLootSpawnRequest distant{
        .chunk = chunk,
        .resource = ResourceContentStack::item("iron", 1),
        .position_x = 12.0f,
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 10,
    };
    auto later_spawn = (*restarted)->spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest>(&distant, 1));
    ASSERT_TRUE(later_spawn) << later_spawn.error().format();
    ASSERT_EQ(later_spawn->size(), 1u);
    EXPECT_EQ(later_spawn->front(), 2u);
    EXPECT_FALSE((*restarted)->pickup_ground_loot(peer, {.loot_id = later_spawn->front()}, 11));
    EXPECT_EQ((*restarted)->active_loot_count(), 1u);
    (*players)->shutdown();
}

TEST(GameGroundLootServiceTest, LeavesLootWhenTheInventoryCannotAcceptIt) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("iron")));
    ASSERT_TRUE(content.register_builtin_item(make_item("stone")));
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = content.resource_runtime_index(),
            .spawn = {.dimension_id = "overworld", .position = {.x = 2, .y = 1, .z = 2}},
            .inventory_slots = 1,
            .inventory_max_stack_size = 1,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer peer = make_peer(702, "Full Ground Loot Player");
    ASSERT_TRUE((*players)->on_peer_authenticated(peer, (*players)->default_persistent_state()));
    ASSERT_TRUE((*players)->apply_inventory_transaction(
        peer, {.additions = {snt::game::GamePlayerItemStack::item("stone", 1)}}));

    snt::voxel::ChunkRegistry chunks;
    const ChunkKey chunk{"overworld", 0, 0, 0};
    add_loaded_chunk(chunks, chunk);
    GameChunkSidecarRegistry sidecars;
    CheckpointSink checkpoint;
    auto service = GameServerGroundLootService::create(*(*players), chunks, sidecars, content,
                                                        &checkpoint);
    ASSERT_TRUE(service) << service.error().format();
    const GameGroundLootSpawnRequest request{
        .chunk = chunk,
        .resource = ResourceContentStack::item("iron", 1),
        .position_x = 2.0f,
        .position_y = 1.0f,
        .position_z = 2.0f,
        .spawned_tick = 3,
    };
    auto spawned = (*service)->spawn_ground_loot(
        std::span<const GameGroundLootSpawnRequest>(&request, 1));
    ASSERT_TRUE(spawned) << spawned.error().format();
    EXPECT_FALSE((*service)->pickup_ground_loot(peer, {.loot_id = spawned->front()}, 4));
    EXPECT_EQ((*service)->active_loot_count(), 1u);
    EXPECT_EQ(checkpoint.marks, 0);
    (*players)->shutdown();
}

TEST(GameGroundLootCreatureLifecycleTest, CreatureKillSpawnsDeterministicLootWithoutInventoryGrant) {
    const WorldGenConfigSnapshot worldgen = make_worldgen_config();
    const ChunkKey chunk{"overworld", 0, 0, 0};
    snt::voxel::ChunkRegistry chunks;
    add_loaded_chunk(chunks, chunk, 2);
    GameChunkSidecarRegistry sidecars;
    GameEcosystemSystem ecosystem(chunks, sidecars, worldgen);
    ASSERT_TRUE(ecosystem.ensure_population_cell(chunk, 1));
    GameWildCreatureSystem wildlife(
        ecosystem, chunks, sidecars, builtin_creature_species(),
        GameWildCreatureConfig{.killed_proxy_suppression_ticks = 0});

    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("snt:glow_deer_antler")));
    ASSERT_TRUE(content.register_builtin_item(make_item("snt:purifying_pollen")));
    ASSERT_TRUE(content.register_builtin_item(make_item("meat.raw.glow_deer")));
    snt::ecs::World world;
    auto players = GameServerPlayerState::create(
        world,
        {
            .resource_runtime_index = content.resource_runtime_index(),
            .spawn = {.dimension_id = "overworld", .position = {}},
            .interaction_reach_blocks = 64,
        });
    ASSERT_TRUE(players) << players.error().format();
    const GameAuthenticatedPeer peer = make_peer(703, "Creature Loot Player");
    ASSERT_TRUE((*players)->on_peer_authenticated(peer, (*players)->default_persistent_state()));
    auto ground_loot = GameServerGroundLootService::create(*(*players), chunks, sidecars, content);
    ASSERT_TRUE(ground_loot) << ground_loot.error().format();
    auto interactions = GameServerCreatureInteractionService::create(
        *(*players), chunks, content, wildlife, *(*ground_loot),
        GameServerCreatureInteractionConfig{.unarmed_damage = 1.0f});
    ASSERT_TRUE(interactions) << interactions.error().format();

    const auto* species = builtin_creature_species().get_species(1);
    ASSERT_NE(species, nullptr);
    ASSERT_FALSE(species->drops.empty());
    EXPECT_EQ(species->drops.back().item_key, "meat.raw.glow_deer");

    wildlife.request_wild_proxy_rebalance(creature_request(chunk, 9, 88));
    ASSERT_TRUE((*interactions)->attack_creature(peer, {.creature_entity_id = 88}, 10));
    EXPECT_FALSE(wildlife.find_wild_creature(88).has_value());
    const auto inventory_before_second_kill = (*players)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory_before_second_kill) << inventory_before_second_kill.error().format();
    const GameChunkSidecar* first_sidecar = sidecars.get(chunk);
    ASSERT_NE(first_sidecar, nullptr);
    ASSERT_FALSE(first_sidecar->ground_loot.empty());
    const std::vector<GameGroundLootRecord> first_drops = first_sidecar->ground_loot;
    EXPECT_TRUE(std::any_of(first_drops.begin(), first_drops.end(),
                            [](const GameGroundLootRecord& record) {
                                return record.resource.key.id == "meat.raw.glow_deer";
                            }));

    wildlife.request_wild_proxy_rebalance(creature_request(chunk, 11, 88));
    ASSERT_TRUE((*interactions)->attack_creature(peer, {.creature_entity_id = 88}, 12));
    const GameChunkSidecar* second_sidecar = sidecars.get(chunk);
    ASSERT_NE(second_sidecar, nullptr);
    ASSERT_EQ(second_sidecar->ground_loot.size(), first_drops.size() * 2u);
    for (size_t index = 0; index < first_drops.size(); ++index) {
        const GameGroundLootRecord& repeated = second_sidecar->ground_loot[index + first_drops.size()];
        EXPECT_EQ(repeated.resource, first_drops[index].resource);
        EXPECT_FLOAT_EQ(repeated.position_x, first_drops[index].position_x);
        EXPECT_FLOAT_EQ(repeated.position_y, first_drops[index].position_y);
        EXPECT_FLOAT_EQ(repeated.position_z, first_drops[index].position_z);
        EXPECT_EQ(repeated.spawned_tick, 12u);
    }
    auto inventory_after_second_kill = (*players)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory_after_second_kill) << inventory_after_second_kill.error().format();
    EXPECT_EQ(*inventory_after_second_kill, *inventory_before_second_kill);
    (*players)->shutdown();
}
