// Offline standalone machine simulation tests.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/simulation/offline_machine_simulation.h"
#include "game/world/save/chunk_serializer.h"

namespace {

using snt::game::GameChunk;
using snt::game::GameChunkSidecarRegistry;
using snt::game::GameChunkSerializer;
using snt::game::GameContentRegistry;
using snt::game::IMachineTickEventSink;
using snt::game::MachineDefinition;
using snt::game::MachineOfflineSimulationMode;
using snt::game::MachineRunState;
using snt::game::MachineRuntimeComponent;
using snt::game::MachineRuntimePersistenceRecord;
using snt::game::MachineRuntimeResidency;
using snt::game::MachineTickEvent;
using snt::game::MachineTickEventKind;
using snt::game::OfflineMachineSimulationService;
using snt::game::RecipeDefinition;
using snt::game::RecipeInputDefinition;
using snt::game::RecipeOutputDefinition;
using snt::voxel::ChunkKey;

class CapturingMachineEvents final : public IMachineTickEventSink {
public:
    void on_machine_tick_event(const MachineTickEvent& event) override {
        events.push_back(event);
    }

    std::vector<MachineTickEvent> events;
};

void register_furnace_content(GameContentRegistry& content,
                              MachineOfflineSimulationMode mode,
                              bool requires_manual_activation = false,
                              int32_t recipe_duration_ticks = 2) {
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.ore", .title_key = "item.offline.ore", .max_stack = 64}));
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.ingot", .title_key = "item.offline.ingot", .max_stack = 64}));

    MachineDefinition furnace;
    furnace.id = "offline.furnace";
    furnace.display_name = "Offline Furnace";
    furnace.tier = 1;
    furnace.requires_manual_activation = requires_manual_activation;
    furnace.offline_simulation.mode = mode;
    furnace.offline_simulation.max_batch_ticks = 5;
    ASSERT_TRUE(content.register_builtin_machine(std::move(furnace)));

    RecipeDefinition recipe;
    recipe.id = "offline.furnace.recipe";
    recipe.machine_id = "offline.furnace";
    recipe.inputs = {RecipeInputDefinition{"offline.ore", 1}};
    recipe.outputs = {RecipeOutputDefinition{"offline.ingot", 1}};
    recipe.duration_ticks = recipe_duration_ticks;
    ASSERT_TRUE(content.register_builtin_recipe(std::move(recipe)));
}

ChunkKey make_chunk_key() {
    return {"overworld", 0, 0, 0};
}

TEST(OfflineMachineSimulationTest, DematerializesAdvancesAndRestoresStandaloneMachine) {
    GameContentRegistry content;
    register_furnace_content(content, MachineOfflineSimulationMode::kStandalone);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});

    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "offline.furnace";
    runtime.input_slots = {{"offline.ore", 3}};
    const auto anchored = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, std::move(runtime));
    ASSERT_TRUE(anchored);

    CapturingMachineEvents events;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_event_sink(&events);
    ASSERT_TRUE(offline.initialize(100));

    const auto transition = offline.dematerialize_chunk(world, chunk_key, 100);
    ASSERT_TRUE(transition);
    EXPECT_EQ(transition->standalone_machine_count, 1u);
    EXPECT_EQ(transition->paused_machine_count, 0u);
    EXPECT_TRUE(world.find_entity_by_guid(anchored->entity_guid) == entt::null);

    auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->machine_runtime_records.size(), 1u);
    EXPECT_EQ(sidecar->machine_runtime_records.front().residency,
              MachineRuntimeResidency::kOfflineStandalone);

    ASSERT_TRUE(offline.tick(105));
    const MachineRuntimePersistenceRecord& offline_record =
        sidecar->machine_runtime_records.front();
    ASSERT_EQ(offline_record.output_slots.size(), 1u);
    EXPECT_EQ(offline_record.output_slots.front().item_id, "offline.ingot");
    EXPECT_EQ(offline_record.output_slots.front().count, 2);
    ASSERT_TRUE(offline_record.active_recipe.has_value());
    EXPECT_EQ(offline_record.progress_ticks, 1);

    ASSERT_TRUE(offline.materialize_chunk(world, chunk_key, 106));
    const entt::entity entity = world.find_entity_by_guid(anchored->entity_guid);
    ASSERT_TRUE(entity != entt::null);
    const MachineRuntimeComponent& restored =
        world.get_component<MachineRuntimeComponent>(entity);
    EXPECT_EQ(restored.state, MachineRunState::Idle);
    EXPECT_FALSE(restored.active_recipe.has_value());
    ASSERT_EQ(restored.output_slots.size(), 1u);
    EXPECT_EQ(restored.output_slots.front().count, 3);
    EXPECT_TRUE(restored.input_slots.empty());
    EXPECT_EQ(sidecar->machine_runtime_records.front().residency,
              MachineRuntimeResidency::kLoaded);

    size_t completed = 0;
    for (const MachineTickEvent& event : events.events) {
        if (event.kind != MachineTickEventKind::RecipeCompleted) continue;
        ++completed;
    }
    EXPECT_EQ(completed, 3u);
}

TEST(OfflineMachineSimulationTest, DefersNetworkIslandUntilTopologyProviderExists) {
    GameContentRegistry content;
    register_furnace_content(content, MachineOfflineSimulationMode::kNetworkIsland);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "offline.furnace";
    runtime.input_slots = {{"offline.ore", 1}};
    ASSERT_TRUE(snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, std::move(runtime)));

    OfflineMachineSimulationService offline(content, sidecars);
    ASSERT_TRUE(offline.initialize(10));
    const auto transition = offline.dematerialize_chunk(world, chunk_key, 10);
    ASSERT_TRUE(transition);
    EXPECT_EQ(transition->deferred_network_machine_count, 1u);
    EXPECT_EQ(transition->standalone_machine_count, 0u);
    const auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    EXPECT_EQ(sidecar->machine_runtime_records.front().residency,
              MachineRuntimeResidency::kPaused);
    EXPECT_EQ(offline.offline_machine_count(), 0u);
}

TEST(OfflineMachineSimulationTest, ManualMachineFinishesActiveJobButDoesNotStartAnotherOffline) {
    GameContentRegistry content;
    register_furnace_content(content, MachineOfflineSimulationMode::kStandalone, true);

    MachineRuntimeComponent starting_runtime;
    starting_runtime.machine_id = "offline.furnace";
    starting_runtime.input_slots = {{"offline.ore", 2}};
    starting_runtime.activation_requested = true;
    starting_runtime.job_owner_account_id = "account:manual-owner";
    auto execution_input = snt::game::make_machine_execution_input(
        content, snt::ecs::EntityGuid{700}, std::move(starting_runtime));
    ASSERT_TRUE(execution_input);
    const snt::game::MachineExecutionResult started = snt::game::advance_machine_execution(
        std::move(*execution_input), 1, 1);
    ASSERT_EQ(started.advanced_ticks, 1u);
    ASSERT_TRUE(started.machine.active_recipe.has_value());
    ASSERT_EQ(started.machine.input_slots.size(), 1u);
    EXPECT_EQ(started.machine.input_slots.front().count, 1);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    const auto anchored = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, started.machine);
    ASSERT_TRUE(anchored);

    CapturingMachineEvents events;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_event_sink(&events);
    ASSERT_TRUE(offline.initialize(10));
    ASSERT_TRUE(offline.dematerialize_chunk(world, chunk_key, 10));
    ASSERT_TRUE(offline.tick(15));

    const auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    const MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records.front();
    EXPECT_FALSE(record.active_recipe.has_value());
    EXPECT_EQ(record.run_state, static_cast<uint8_t>(MachineRunState::Idle));
    ASSERT_EQ(record.input_slots.size(), 1u);
    EXPECT_EQ(record.input_slots.front().item_id, "offline.ore");
    EXPECT_EQ(record.input_slots.front().count, 1);
    ASSERT_EQ(record.output_slots.size(), 1u);
    EXPECT_EQ(record.output_slots.front().item_id, "offline.ingot");
    EXPECT_EQ(record.output_slots.front().count, 1);

    size_t completions = 0;
    for (const MachineTickEvent& event : events.events) {
        if (event.kind == MachineTickEventKind::RecipeCompleted) ++completions;
    }
    EXPECT_EQ(completions, 1u);
}

TEST(OfflineMachineSimulationTest, DelayedSchedulerUsesBoundedOfflineBatches) {
    GameContentRegistry content;
    register_furnace_content(
        content, MachineOfflineSimulationMode::kStandalone, false, 6);
    ASSERT_NE(content.find_machine("offline.furnace"), nullptr);
    EXPECT_EQ(content.find_machine("offline.furnace")->offline_simulation.max_batch_ticks, 5u);
    ASSERT_NE(content.find_recipe("offline.furnace.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("offline.furnace.recipe")->duration_ticks, 6);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "offline.furnace";
    runtime.input_slots = {{"offline.ore", 2}};
    auto initial_execution = snt::game::make_machine_execution_input(
        content, snt::ecs::EntityGuid{701}, std::move(runtime));
    ASSERT_TRUE(initial_execution);
    const snt::game::MachineExecutionResult initial_result =
        snt::game::advance_machine_execution(std::move(*initial_execution), 1, 1);
    ASSERT_TRUE(initial_result.machine.active_recipe.has_value());
    ASSERT_EQ(initial_result.machine.progress_ticks, 1);
    ASSERT_TRUE(snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, initial_result.machine));

    OfflineMachineSimulationService offline(content, sidecars);
    ASSERT_TRUE(offline.initialize(100));
    ASSERT_TRUE(offline.dematerialize_chunk(world, chunk_key, 100));
    ASSERT_TRUE(offline.tick(111));

    const auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    const MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records.front();
    EXPECT_EQ(record.offline_last_simulated_tick, 110u);
    ASSERT_EQ(record.output_slots.size(), 1u);
    EXPECT_EQ(record.output_slots.front().count, 1);
    ASSERT_TRUE(record.active_recipe.has_value());
    EXPECT_EQ(record.progress_ticks, 5);
}

TEST(OfflineMachineSimulationTest, OfflineRecordSurvivesChunkSaveAndRestartWithoutDowntimeCatchUp) {
    GameContentRegistry content;
    register_furnace_content(content, MachineOfflineSimulationMode::kStandalone);

    GameChunkSidecarRegistry original_sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    original_sidecars.set(chunk_key, {});
    snt::ecs::World original_world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "offline.furnace";
    runtime.input_slots = {{"offline.ore", 2}};
    const auto anchored = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        original_world, original_sidecars, chunk_key, 0, 0, 0, std::move(runtime));
    ASSERT_TRUE(anchored);

    OfflineMachineSimulationService original_offline(content, original_sidecars);
    ASSERT_TRUE(original_offline.initialize(100));
    ASSERT_TRUE(original_offline.dematerialize_chunk(original_world, chunk_key, 100));

    const auto* original_sidecar = original_sidecars.get(chunk_key);
    ASSERT_NE(original_sidecar, nullptr);
    GameChunk serialized_chunk;
    serialized_chunk.terrain.resize(1, 1, 1);
    serialized_chunk.sidecar() = *original_sidecar;
    const GameChunkSerializer serializer;
    const auto payload = serializer.serialize(chunk_key.dimension_id, serialized_chunk);

    GameChunk restored_chunk;
    std::string restored_dimension;
    ASSERT_TRUE(serializer.deserialize(payload, restored_dimension, restored_chunk));
    EXPECT_EQ(restored_dimension, chunk_key.dimension_id);
    GameChunkSidecarRegistry restarted_sidecars;
    restarted_sidecars.set(chunk_key, restored_chunk.sidecar());
    snt::ecs::World restarted_world;
    ASSERT_TRUE(snt::game::GameMachineRuntimePersistence::restore(
        restarted_world, restarted_sidecars));
    EXPECT_TRUE(restarted_world.find_entity_by_guid(anchored->entity_guid) == entt::null);

    OfflineMachineSimulationService restarted_offline(content, restarted_sidecars);
    ASSERT_TRUE(restarted_offline.initialize(500));
    const auto* restarted_sidecar = restarted_sidecars.get(chunk_key);
    ASSERT_NE(restarted_sidecar, nullptr);
    EXPECT_EQ(restarted_sidecar->machine_runtime_records.front().offline_last_simulated_tick, 500u);
    ASSERT_TRUE(restarted_offline.tick(505));
    ASSERT_TRUE(restarted_offline.materialize_chunk(restarted_world, chunk_key, 505));

    const entt::entity restored_entity =
        restarted_world.find_entity_by_guid(anchored->entity_guid);
    ASSERT_TRUE(restored_entity != entt::null);
    const MachineRuntimeComponent& restored_runtime =
        restarted_world.get_component<MachineRuntimeComponent>(restored_entity);
    ASSERT_EQ(restored_runtime.output_slots.size(), 1u);
    EXPECT_EQ(restored_runtime.output_slots.front().item_id, "offline.ingot");
    EXPECT_EQ(restored_runtime.output_slots.front().count, 2);
    EXPECT_TRUE(restored_runtime.input_slots.empty());
}

TEST(OfflineMachineSimulationTest, SerializerRoundTripsOfflineOwnershipMetadata) {
    GameChunk source;
    source.terrain.resize(1, 1, 1);
    MachineRuntimePersistenceRecord record;
    record.anchor_entity_id = {42};
    record.entity_guid = 99;
    record.machine_id = "offline.furnace";
    record.residency = MachineRuntimeResidency::kOfflineStandalone;
    record.offline_last_simulated_tick = 1234;
    record.offline_epoch = 7;
    source.machine_runtime_records.push_back(record);

    const GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", source);
    GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(payload, dimension_id, restored));
    ASSERT_EQ(restored.machine_runtime_records.size(), 1u);
    const MachineRuntimePersistenceRecord& restored_record =
        restored.machine_runtime_records.front();
    EXPECT_EQ(restored_record.residency, MachineRuntimeResidency::kOfflineStandalone);
    EXPECT_EQ(restored_record.offline_last_simulated_tick, 1234u);
    EXPECT_EQ(restored_record.offline_epoch, 7u);
}

}  // namespace
