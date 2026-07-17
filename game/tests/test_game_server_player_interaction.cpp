// Dedicated-server host-authoritative/client-hint interaction coverage.

#include "game/server/game_server_player_interaction.h"

#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"
#include "game/player/player_identity.h"
#include "game/server/game_server_command_sink.h"
#include "game/server/game_server_player_death.h"
#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"
#include "game/simulation/machine_interaction_service.h"
#include "game/world/game_chunk.h"
#include "voxel/data/chunk_registry.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using snt::game::GameChunkSidecarRegistry;
using snt::game::GameContentRegistry;
using snt::game::GamePlayerWorldPosition;
using snt::game::MachineActivationRequirements;
using snt::game::MachineDefinition;
using snt::game::MachineInteractionService;
using snt::game::MachineRunState;
using snt::game::MachineRuntimeComponent;
using snt::game::QuestRegistry;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameBlockInteractionAction;
using snt::game::replication::GameBlockInteractionCommand;
using snt::game::replication::GameServerCommandSink;
using snt::game::replication::GameServerPlayerBedService;
using snt::game::replication::GameServerPlayerInteractionEvent;
using snt::game::replication::GameServerPlayerInteractionEventKind;
using snt::game::replication::GameServerPlayerInteractionService;
using snt::game::replication::GameServerPlayerState;
using snt::game::replication::IGameServerPlayerInteractionEventSink;
using snt::game::replication::IGameServerPlayerInteractionService;
using snt::game::replication::IGameServerPlayerStateCheckpointSink;

GameAuthenticatedPeer make_peer(snt::network::PeerId peer_id, std::string name) {
    auto identity = snt::game::make_local_name_player_identity(std::move(name));
    return {
        .peer = peer_id,
        .identity = identity ? std::move(*identity) : snt::game::PlayerIdentity{},
    };
}

GamePlayerWorldPosition position(int x, int y, int z) {
    return {.dimension_id = "overworld", .position = {.x = x, .y = y, .z = z}};
}

void add_ground_chunk(snt::voxel::ChunkRegistry& chunks) {
    snt::voxel::VoxelChunk chunk;
    chunk.state = snt::voxel::ChunkState::Active;
    chunk.terrain.resize(snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize,
                         snt::voxel::VoxelChunk::kChunkSize);
    for (int z = 0; z < snt::voxel::VoxelChunk::kChunkSize; ++z) {
        for (int x = 0; x < snt::voxel::VoxelChunk::kChunkSize; ++x) {
            chunk.terrain.set_cell(x, 0, z, 1,
                                   snt::voxel::TF_SOLID | snt::voxel::TF_MINEABLE);
        }
    }
    chunks.set_chunk("overworld", 0, 0, 0, std::move(chunk));
}

struct CheckpointSink final : IGameServerPlayerStateCheckpointSink {
    int marks = 0;

    snt::core::Expected<void> mark_player_state_dirty(
        const GameAuthenticatedPeer&) override {
        ++marks;
        return {};
    }
};

struct EventSink final : IGameServerPlayerInteractionEventSink {
    std::vector<GameServerPlayerInteractionEvent> events;

    void on_player_interaction(const GameServerPlayerInteractionEvent& event) override {
        events.push_back(event);
    }
};

class RecordingInteractionService final : public IGameServerPlayerInteractionService {
public:
    snt::core::Expected<void> apply_block_interaction(
        const GameAuthenticatedPeer& peer, const GameBlockInteractionCommand& command,
        uint64_t tick_index) override {
        ++calls;
        last_peer = peer;
        last_command = command;
        last_tick = tick_index;
        return {};
    }

    int calls = 0;
    GameAuthenticatedPeer last_peer;
    GameBlockInteractionCommand last_command;
    uint64_t last_tick = 0;
};

MachineDefinition make_manual_machine() {
    MachineDefinition definition;
    definition.id = "bloomery";
    definition.display_name = "Bloomery";
    definition.tier = 1;
    definition.requires_manual_activation = true;
    definition.activation_requirements = MachineActivationRequirements{
        .requires_cover = true,
        .requires_ignition = true,
        .requires_valid_structure = true,
        .required_tool_tag = "hammer",
    };
    return definition;
}

}  // namespace

TEST(GameBlockInteractionProtocolTest, EncodesTrustedClientHintsWithoutTerrainMutationValues) {
    const GameBlockInteractionCommand original{
        .action = GameBlockInteractionAction::kActivateMachine,
        .dimension_id = "overworld",
        .block_x = -9,
        .block_y = 31,
        .block_z = 7,
        .expected_material = 6,
        .selected_item_id = "hammer",
        .client_hints = snt::game::replication::kGameBlockInteractionHintCover |
                        snt::game::replication::kGameBlockInteractionHintIgnition |
                        snt::game::replication::kGameBlockInteractionHintStructure,
    };
    auto encoded = snt::game::replication::make_game_block_interaction_command(9, original);
    ASSERT_TRUE(encoded) << encoded.error().format();
    auto decoded = snt::game::replication::parse_game_block_interaction_command(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_EQ(decoded->action, original.action);
    EXPECT_EQ(decoded->dimension_id, original.dimension_id);
    EXPECT_EQ(decoded->block_x, original.block_x);
    EXPECT_EQ(decoded->block_y, original.block_y);
    EXPECT_EQ(decoded->block_z, original.block_z);
    EXPECT_EQ(decoded->expected_material, original.expected_material);
    EXPECT_EQ(decoded->selected_item_id, original.selected_item_id);
    EXPECT_EQ(decoded->client_hints, original.client_hints);

    auto invalid = original;
    invalid.client_hints = 0x80;
    EXPECT_FALSE(snt::game::replication::make_game_block_interaction_command(10, invalid));
}

TEST(GameServerPlayerInteractionTest, CommitsBedInventoryAndRespawnThroughHostTransactions) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);
    auto player_state = GameServerPlayerState::create(
        world,
        {
            .spawn = position(2, 1, 2),
            .inventory_slots = 4,
            .inventory_max_stack_size = 8,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const GameAuthenticatedPeer peer = make_peer(601, "Interaction Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, {.additions = {{.item_id = "bed", .count = 1}}}));

    CheckpointSink checkpoint;
    EventSink events;
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars, &checkpoint);
    ASSERT_TRUE(beds) << beds.error().format();
    GameContentRegistry content;
    MachineInteractionService machine_interactions(content);
    auto interactions = GameServerPlayerInteractionService::create(
        world, chunks, sidecars, *(*player_state), *(*beds), content, machine_interactions,
        &checkpoint, &events);
    ASSERT_TRUE(interactions) << interactions.error().format();

    const GameBlockInteractionCommand place{
        .action = GameBlockInteractionAction::kPlace,
        .dimension_id = "overworld",
        .block_x = 3,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 0,
        .selected_item_id = "bed",
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, place, 10));
    const auto* terrain = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(terrain, nullptr);
    EXPECT_EQ(terrain->terrain.cell_at(3, 1, 2).material, 5u);
    auto bed_present = (*beds)->has_bed_at(position(3, 1, 2));
    ASSERT_TRUE(bed_present) << bed_present.error().format();
    EXPECT_TRUE(*bed_present);

    auto inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_TRUE(inventory->slots[0].item_id.empty());

    const GameBlockInteractionCommand use_bed{
        .action = GameBlockInteractionAction::kUse,
        .dimension_id = "overworld",
        .block_x = 3,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 5,
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, use_bed, 11));
    auto persistent = (*player_state)->capture_persistent_state(peer);
    ASSERT_TRUE(persistent) << persistent.error().format();
    ASSERT_TRUE(persistent->respawn_point.has_value());
    EXPECT_EQ(persistent->respawn_point->position.x, 3);

    const GameBlockInteractionCommand mine_bed{
        .action = GameBlockInteractionAction::kMine,
        .dimension_id = "overworld",
        .block_x = 3,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 5,
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, mine_bed, 12));
    EXPECT_EQ(terrain->terrain.cell_at(3, 1, 2).material, 0u);
    bed_present = (*beds)->has_bed_at(position(3, 1, 2));
    ASSERT_TRUE(bed_present) << bed_present.error().format();
    EXPECT_FALSE(*bed_present);
    inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0].item_id, "bed");
    EXPECT_EQ(inventory->slots[0].count, 1);
    EXPECT_EQ(checkpoint.marks, 3);
    ASSERT_EQ(events.events.size(), 3u);
    EXPECT_EQ(events.events.front().tick_index, 10u);
    (*player_state)->shutdown();
}

TEST(GameServerPlayerInteractionTest, PlacesAnchoredMachineThroughTheHostInventoryTransaction) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);
    auto player_state = GameServerPlayerState::create(
        world,
        {
            .spawn = position(2, 1, 2),
            .inventory_slots = 4,
            .inventory_max_stack_size = 8,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const GameAuthenticatedPeer peer = make_peer(604, "Machine Placement Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));
    ASSERT_TRUE((*player_state)->apply_inventory_transaction(
        peer, {.additions = {{.item_id = "bloomery", .count = 1}}}));

    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_machine(make_manual_machine()));
    ASSERT_TRUE(content.register_builtin_machine_placement(
        {.item_id = "bloomery", .machine_id = "bloomery", .material_id = 10}));
    MachineInteractionService machine_interactions(content);
    CheckpointSink checkpoint;
    EventSink events;
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars, &checkpoint);
    ASSERT_TRUE(beds) << beds.error().format();
    auto interactions = GameServerPlayerInteractionService::create(
        world, chunks, sidecars, *(*player_state), *(*beds), content, machine_interactions,
        &checkpoint, &events);
    ASSERT_TRUE(interactions) << interactions.error().format();

    const GameBlockInteractionCommand place{
        .action = GameBlockInteractionAction::kPlace,
        .dimension_id = "overworld",
        .block_x = 3,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 0,
        .selected_item_id = "bloomery",
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, place, 30));

    const auto* terrain = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(terrain, nullptr);
    EXPECT_EQ(terrain->terrain.cell_at(3, 1, 2).material, 10u);
    EXPECT_TRUE(terrain->terrain.cell_at(3, 1, 2).is_solid());

    const snt::voxel::ChunkKey key{"overworld", 0, 0, 0};
    const auto* sidecar = sidecars.get(key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->block_entities.size(), 1u);
    EXPECT_EQ(sidecar->block_entities.front().entity_type, snt::game::BlockEntityType::MACHINE);
    ASSERT_EQ(sidecar->machine_runtime_records.size(), 1u);
    EXPECT_EQ(sidecar->machine_runtime_records.front().machine_id, "bloomery");
    const snt::ecs::EntityGuid machine_guid{
        sidecar->machine_runtime_records.front().entity_guid};
    EXPECT_TRUE(world.find_entity_by_guid(machine_guid) != entt::null);

    const auto inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_TRUE(inventory->slots[0].item_id.empty());
    ASSERT_EQ(events.events.size(), 1u);
    EXPECT_EQ(events.events.front().kind, GameServerPlayerInteractionEventKind::kMachinePlaced);
    EXPECT_EQ(events.events.front().machine_id, "bloomery");
    EXPECT_EQ(events.events.front().current_material, 10u);

    const GameBlockInteractionCommand mine_machine{
        .action = GameBlockInteractionAction::kMine,
        .dimension_id = "overworld",
        .block_x = 3,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 10,
    };
    EXPECT_FALSE((*interactions)->apply_block_interaction(peer, mine_machine, 31));
    (*player_state)->shutdown();
}

TEST(GameServerPlayerInteractionTest, RejectsUnknownMachinePlacementAtServiceCreation) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);
    auto player_state = GameServerPlayerState::create(
        world,
        {
            .spawn = position(2, 1, 2),
            .inventory_slots = 4,
            .inventory_max_stack_size = 8,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_machine_placement(
        {.item_id = "orphan_machine", .machine_id = "missing.machine", .material_id = 10}));
    MachineInteractionService machine_interactions(content);
    CheckpointSink checkpoint;
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars, &checkpoint);
    ASSERT_TRUE(beds) << beds.error().format();
    auto interactions = GameServerPlayerInteractionService::create(
        world, chunks, sidecars, *(*player_state), *(*beds), content, machine_interactions,
        &checkpoint);
    EXPECT_FALSE(interactions);
    (*player_state)->shutdown();
}

TEST(GameServerPlayerInteractionTest, TrustsMachineHintsButKeepsOutputAndSidecarsHostOwned) {
    snt::ecs::World world;
    snt::voxel::ChunkRegistry chunks;
    GameChunkSidecarRegistry sidecars;
    add_ground_chunk(chunks);
    auto player_state = GameServerPlayerState::create(
        world,
        {
            .spawn = position(2, 1, 2),
            .inventory_slots = 4,
            .inventory_max_stack_size = 8,
            .interaction_reach_blocks = 5,
        });
    ASSERT_TRUE(player_state) << player_state.error().format();
    const GameAuthenticatedPeer peer = make_peer(602, "Machine Player");
    ASSERT_TRUE((*player_state)->on_peer_authenticated(
        peer, (*player_state)->default_persistent_state()));

    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_machine(make_manual_machine()));
    MachineInteractionService machine_interactions(content);
    const entt::entity machine_entity = world.create_entity();
    auto& runtime = world.add_component<MachineRuntimeComponent>(machine_entity);
    runtime.machine_id = "bloomery";
    runtime.state = MachineRunState::WaitingForActivation;
    runtime.output_slots = {{.item_id = "iron_bloom", .count = 2}};
    const auto machine_guid = world.guid_of(machine_entity);

    auto* terrain = chunks.get_chunk("overworld", 0, 0, 0);
    ASSERT_NE(terrain, nullptr);
    terrain->terrain.set_cell(4, 1, 2, 1,
                              snt::voxel::TF_SOLID | snt::voxel::TF_MINEABLE);

    const snt::voxel::ChunkKey key{"overworld", 0, 0, 0};
    sidecars.set(key, {});
    auto* sidecar = sidecars.get(key);
    ASSERT_NE(sidecar, nullptr);
    sidecar->block_entities.push_back({
        .id = {.id = 81},
        .entity_type = snt::game::BlockEntityType::MACHINE,
        .root_x = 4,
        .root_y = 1,
        .root_z = 2,
    });
    sidecar->machine_runtime_records.push_back({
        .anchor_entity_id = {.id = 81},
        .entity_guid = machine_guid.value,
        .machine_id = "bloomery",
    });

    CheckpointSink checkpoint;
    auto beds = GameServerPlayerBedService::create(*(*player_state), chunks, sidecars, &checkpoint);
    ASSERT_TRUE(beds) << beds.error().format();
    auto interactions = GameServerPlayerInteractionService::create(
        world, chunks, sidecars, *(*player_state), *(*beds), content, machine_interactions,
        &checkpoint);
    ASSERT_TRUE(interactions) << interactions.error().format();

    const GameBlockInteractionCommand activate{
        .action = GameBlockInteractionAction::kActivateMachine,
        .dimension_id = "overworld",
        .block_x = 4,
        .block_y = 1,
        .block_z = 2,
        .selected_item_id = "hammer",
        .client_hints = snt::game::replication::kGameBlockInteractionHintCover |
                        snt::game::replication::kGameBlockInteractionHintIgnition |
                        snt::game::replication::kGameBlockInteractionHintStructure,
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, activate, 20));
    EXPECT_TRUE(runtime.activation_requested);
    EXPECT_EQ(runtime.job_owner_account_id, peer.identity.account_id);

    const GameBlockInteractionCommand collect{
        .action = GameBlockInteractionAction::kCollectMachineOutput,
        .dimension_id = "overworld",
        .block_x = 4,
        .block_y = 1,
        .block_z = 2,
    };
    ASSERT_TRUE((*interactions)->apply_block_interaction(peer, collect, 21));
    EXPECT_TRUE(runtime.output_slots[0].empty());
    auto inventory = (*player_state)->inventory_for_peer(peer);
    ASSERT_TRUE(inventory) << inventory.error().format();
    EXPECT_EQ(inventory->slots[0].item_id, "iron_bloom");
    EXPECT_EQ(inventory->slots[0].count, 2);

    const GameBlockInteractionCommand mine_machine{
        .action = GameBlockInteractionAction::kMine,
        .dimension_id = "overworld",
        .block_x = 4,
        .block_y = 1,
        .block_z = 2,
        .expected_material = 1,
    };
    EXPECT_FALSE((*interactions)->apply_block_interaction(peer, mine_machine, 22));
    (*player_state)->shutdown();
}

TEST(GameServerCommandSinkTest, DispatchesBlockInteractionInTheSharedCommandOrder) {
    GameContentRegistry content;
    QuestRegistry quests(content);
    RecordingInteractionService interactions;
    GameServerCommandSink sink(quests, nullptr, &interactions);
    const GameAuthenticatedPeer peer = make_peer(603, "Command Interaction Player");
    const snt::network::ReplicationTickContext context{.tick_index = 31, .delta_seconds = 0.05f};
    auto command = snt::game::replication::make_game_block_interaction_command(
        1,
        {
            .action = GameBlockInteractionAction::kMine,
            .dimension_id = "overworld",
            .block_x = 2,
            .block_y = 1,
            .block_z = 2,
            .expected_material = 1,
        });
    ASSERT_TRUE(command) << command.error().format();
    ASSERT_TRUE(sink.enqueue_client_command(peer, std::move(*command), context));
    ASSERT_TRUE(sink.apply_pending_commands(context.tick_index));
    EXPECT_EQ(interactions.calls, 1);
    EXPECT_EQ(interactions.last_peer.peer, peer.peer);
    EXPECT_EQ(interactions.last_command.block_x, 2);
    EXPECT_EQ(interactions.last_tick, context.tick_index);
}
