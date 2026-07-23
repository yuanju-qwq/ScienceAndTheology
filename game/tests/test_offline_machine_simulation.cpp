// Offline machine and network-island simulation tests.

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"
#include "game/simulation/machine_runtime_persistence.h"
#include "game/simulation/offline_machine_simulation.h"
#include "game/simulation/offline_network_island_registry.h"
#include "game/simulation/offline_industrial_network_island.h"
#include "game/world/save/chunk_serializer.h"
#include "voxel/data/voxel_chunk.h"

namespace {

using snt::game::GameChunk;
using snt::game::GameChunkSidecarRegistry;
using snt::game::GameChunkSerializer;
using snt::game::GameContentRegistry;
using snt::game::GameFluidDefinition;
using snt::game::IMachineTickEventSink;
using snt::game::MachineDefinition;
using snt::game::MachineFluidTank;
using snt::game::MachineFluidTankAccess;
using snt::game::MachineFluidTankRecord;
using snt::game::MachineFluidTransport;
using snt::game::MachineOfflineSimulationMode;
using snt::game::MachineRunState;
using snt::game::MachineRuntimeComponent;
using snt::game::MachineRuntimePersistenceRecord;
using snt::game::MachineRuntimeResidency;
using snt::game::MachineTickEvent;
using snt::game::MachineTickEventKind;
using snt::game::OfflineMachineSimulationService;
using snt::game::OfflineNetworkBoundaryPort;
using snt::game::OfflineNetworkFluidTransport;
using snt::game::OfflineNetworkIslandRegistry;
using snt::game::OfflineNetworkIslandSnapshot;
using snt::game::OfflineNetworkResourceKind;
using snt::game::OfflineNetworkResourceLedger;
using snt::game::OfflineNetworkTransportSegment;
using snt::game::OfflineIndustrialNetworkIslandProvider;
using snt::game::OfflineIndustrialNetworkIslandSimulator;
using snt::game::PipeType;
using snt::game::RecipeDefinition;
using snt::game::RecipeInputDefinition;
using snt::game::RecipeOutputDefinition;
using snt::game::ResourceContentKey;
using snt::game::ResourceContentStack;
using snt::game::VoltageTier;
using snt::voxel::ChunkKey;

class CapturingMachineEvents final : public IMachineTickEventSink {
public:
    void on_machine_tick_event(const MachineTickEvent& event) override {
        events.push_back(event);
    }

    std::vector<MachineTickEvent> events;
};

[[nodiscard]] snt::game::ResourceStack machine_item_stack(
    const GameContentRegistry& content, std::string id, int64_t amount) {
    const auto stack = snt::game::resolve_resource_stack(
        ResourceContentStack::item(std::move(id), amount),
        content.resource_runtime_index());
    if (!stack) throw std::logic_error("Offline machine test item is absent from content");
    return *stack;
}

[[nodiscard]] snt::game::ResourceStack machine_fluid_stack(
    const GameContentRegistry& content, std::string id, int64_t amount) {
    const auto stack = snt::game::resolve_resource_stack(
        ResourceContentStack::fluid(std::move(id), amount),
        content.resource_runtime_index());
    if (!stack) throw std::logic_error("Offline machine test fluid is absent from content");
    return *stack;
}

void bind_machine_resources(MachineRuntimeComponent& runtime,
                            const GameContentRegistry& content) {
    runtime.resource_runtime_index = content.resource_runtime_index();
}

[[nodiscard]] snt::game::ResourceRuntimeIndex::Snapshot empty_machine_snapshot() {
    static snt::game::ResourceRuntimeIndex index;
    return index.snapshot();
}

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

ChunkKey make_chunk_key(int32_t chunk_x = 0) {
    return {"overworld", chunk_x, 0, 0};
}

void register_power_network_content(GameContentRegistry& content,
                                    uint32_t max_batch_ticks = 5,
                                    int32_t max_transfer_per_tick = 2,
                                    int32_t recipe_duration_ticks = 2) {
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.power_ore", .title_key = "item.offline.power_ore", .max_stack = 64}));
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.power_ingot", .title_key = "item.offline.power_ingot", .max_stack = 64}));

    MachineDefinition source;
    source.id = "offline.power_source";
    source.display_name = "Offline Power Source";
    source.tier = 1;
    source.power_capacity = 32;
    source.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    source.offline_simulation.max_batch_ticks = max_batch_ticks;
    source.offline_simulation.max_power_export_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(source)));

    MachineDefinition consumer;
    consumer.id = "offline.power_consumer";
    consumer.display_name = "Offline Power Consumer";
    consumer.tier = 1;
    consumer.power_capacity = 10;
    consumer.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    consumer.offline_simulation.max_batch_ticks = max_batch_ticks;
    consumer.offline_simulation.max_power_import_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(consumer)));

    RecipeDefinition recipe;
    recipe.id = "offline.power_consumer.recipe";
    recipe.machine_id = "offline.power_consumer";
    recipe.inputs = {RecipeInputDefinition{"offline.power_ore", 1}};
    recipe.outputs = {RecipeOutputDefinition{"offline.power_ingot", 1}};
    recipe.duration_ticks = recipe_duration_ticks;
    recipe.energy_per_tick = 1;
    ASSERT_TRUE(content.register_builtin_recipe(std::move(recipe)));
}

void register_item_network_content(GameContentRegistry& content,
                                   uint32_t max_batch_ticks = 5,
                                   int32_t max_transfer_per_tick = 2,
                                   int32_t recipe_duration_ticks = 2) {
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.item_ore", .title_key = "item.offline.item_ore", .max_stack = 64}));
    ASSERT_TRUE(content.register_builtin_item({
        .id = "offline.item_ingot", .title_key = "item.offline.item_ingot", .max_stack = 64}));

    MachineDefinition source;
    source.id = "offline.item_source";
    source.display_name = "Offline Item Source";
    source.tier = 1;
    source.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    source.offline_simulation.max_batch_ticks = max_batch_ticks;
    source.offline_simulation.max_item_export_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(source)));

    MachineDefinition consumer;
    consumer.id = "offline.item_consumer";
    consumer.display_name = "Offline Item Consumer";
    consumer.tier = 1;
    consumer.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    consumer.offline_simulation.max_batch_ticks = max_batch_ticks;
    consumer.offline_simulation.max_item_import_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(consumer)));

    RecipeDefinition recipe;
    recipe.id = "offline.item_consumer.recipe";
    recipe.machine_id = "offline.item_consumer";
    recipe.inputs = {RecipeInputDefinition{"offline.item_ore", 1}};
    recipe.outputs = {RecipeOutputDefinition{"offline.item_ingot", 1}};
    recipe.duration_ticks = recipe_duration_ticks;
    ASSERT_TRUE(content.register_builtin_recipe(std::move(recipe)));
}

void register_fluid_network_content(GameContentRegistry& content,
                                    uint32_t max_batch_ticks = 5,
                                    int32_t max_transfer_per_tick = 100) {
    ASSERT_TRUE(content.register_builtin_fluid(GameFluidDefinition{
        .id = "offline.coolant",
        .title_key = "fluid.offline.coolant",
        .chemical_formula = "H2O",
        .default_temperature_kelvin = 300,
        .is_gas = false,
    }));
    ASSERT_TRUE(content.register_builtin_fluid(GameFluidDefinition{
        .id = "offline.steam",
        .title_key = "fluid.offline.steam",
        .chemical_formula = "H2O",
        .default_temperature_kelvin = 450,
        .is_gas = true,
    }));

    MachineDefinition source;
    source.id = "offline.fluid_source";
    source.display_name = "Offline Fluid Source";
    source.tier = 1;
    source.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    source.offline_simulation.max_batch_ticks = max_batch_ticks;
    source.offline_simulation.max_fluid_export_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(source)));

    MachineDefinition consumer;
    consumer.id = "offline.fluid_consumer";
    consumer.display_name = "Offline Fluid Consumer";
    consumer.tier = 1;
    consumer.offline_simulation.mode = MachineOfflineSimulationMode::kNetworkIsland;
    consumer.offline_simulation.max_batch_ticks = max_batch_ticks;
    consumer.offline_simulation.max_fluid_import_per_tick = max_transfer_per_tick;
    ASSERT_TRUE(content.register_builtin_machine(std::move(consumer)));
}

void add_power_cable(GameChunkSidecarRegistry& sidecars,
                     const ChunkKey& chunk_key,
                     uint64_t entity_id,
                     int32_t root_x,
                     int32_t root_y,
                     int32_t root_z,
                     uint8_t connection_mask,
                     VoltageTier tier = VoltageTier::ULV) {
    auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    sidecar->block_entities.push_back({
        .id = {entity_id},
        .entity_type = snt::game::BlockEntityType::CABLE,
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
        .type_data_json = std::to_string(static_cast<int>(tier)) + "|" +
                          std::to_string(static_cast<int>(connection_mask)),
        .owned_cell_count = 1,
    });
}

void add_pipe(GameChunkSidecarRegistry& sidecars,
              const ChunkKey& chunk_key,
              uint64_t entity_id,
              int32_t root_x,
              int32_t root_y,
              int32_t root_z,
              uint8_t connection_mask,
              PipeType type = PipeType::ITEM) {
    auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    sidecar->block_entities.push_back({
        .id = {entity_id},
        .entity_type = snt::game::BlockEntityType::PIPE,
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
        .type_data_json = std::to_string(static_cast<int>(type)) + "|" +
                          std::to_string(static_cast<int>(connection_mask)),
        .owned_cell_count = 1,
    });
}

struct PowerMachinePair {
    uint64_t source_guid = 0;
    uint64_t consumer_guid = 0;
};

PowerMachinePair create_power_machine_pair(const GameContentRegistry& content,
                                           snt::ecs::World& world,
                                           GameChunkSidecarRegistry& sidecars,
                                           const ChunkKey& source_chunk,
                                           int32_t source_x,
                                           const ChunkKey& consumer_chunk,
                                           int32_t consumer_x,
                                           int32_t source_energy,
                                           int32_t consumer_energy,
                                           int32_t input_count) {
    MachineRuntimeComponent source_runtime;
    source_runtime.machine_id = "offline.power_source";
    bind_machine_resources(source_runtime, content);
    source_runtime.stored_energy = source_energy;
    source_runtime.energy_capacity = 32;
    const auto source = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, source_chunk, source_x, 0, 0, std::move(source_runtime));
    if (!source) {
        ADD_FAILURE() << source.error().format();
        return {};
    }

    MachineRuntimeComponent consumer_runtime;
    consumer_runtime.machine_id = "offline.power_consumer";
    bind_machine_resources(consumer_runtime, content);
    consumer_runtime.stored_energy = consumer_energy;
    consumer_runtime.energy_capacity = 10;
    if (input_count > 0) {
        consumer_runtime.input_slots = {
            machine_item_stack(content, "offline.power_ore", input_count)};
    }
    const auto consumer = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, consumer_chunk, consumer_x, 0, 0, std::move(consumer_runtime));
    if (!consumer) {
        ADD_FAILURE() << consumer.error().format();
        return {};
    }
    return {source->entity_guid.value, consumer->entity_guid.value};
}

struct ItemMachinePair {
    uint64_t source_guid = 0;
    uint64_t consumer_guid = 0;
};

ItemMachinePair create_item_machine_pair(const GameContentRegistry& content,
                                         snt::ecs::World& world,
                                         GameChunkSidecarRegistry& sidecars,
                                         const ChunkKey& source_chunk,
                                         int32_t source_x,
                                         const ChunkKey& consumer_chunk,
                                         int32_t consumer_x,
                                         int32_t output_count) {
    MachineRuntimeComponent source_runtime;
    source_runtime.machine_id = "offline.item_source";
    bind_machine_resources(source_runtime, content);
    if (output_count > 0) {
        source_runtime.output_slots = {
            machine_item_stack(content, "offline.item_ore", output_count)};
    }
    const auto source = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, source_chunk, source_x, 0, 0, std::move(source_runtime));
    if (!source) {
        ADD_FAILURE() << source.error().format();
        return {};
    }

    MachineRuntimeComponent consumer_runtime;
    consumer_runtime.machine_id = "offline.item_consumer";
    bind_machine_resources(consumer_runtime, content);
    const auto consumer = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, consumer_chunk, consumer_x, 0, 0, std::move(consumer_runtime));
    if (!consumer) {
        ADD_FAILURE() << consumer.error().format();
        return {};
    }
    return {source->entity_guid.value, consumer->entity_guid.value};
}

struct FluidMachinePair {
    uint64_t source_guid = 0;
    uint64_t consumer_guid = 0;
};

FluidMachinePair create_fluid_machine_pair(
    const GameContentRegistry& content,
    snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& source_chunk,
    int32_t source_x,
    const ChunkKey& consumer_chunk,
    int32_t consumer_x,
    MachineFluidTransport transport,
    std::string fluid_id,
    int64_t source_amount,
    int64_t source_capacity = 1'000,
    int64_t consumer_capacity = 1'000) {
    MachineRuntimeComponent source_runtime;
    source_runtime.machine_id = "offline.fluid_source";
    bind_machine_resources(source_runtime, content);
    source_runtime.fluid_tanks = {{
        .fluid = machine_fluid_stack(content, std::move(fluid_id), source_amount),
        .capacity_millibuckets = source_capacity,
        .temperature_kelvin = transport == MachineFluidTransport::kGas ? 450 : 330,
        .pressure_pascal = transport == MachineFluidTransport::kGas ? 180'000 : 110'000,
        .transport = transport,
        .access = MachineFluidTankAccess::kOutput,
    }};
    const auto source = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, source_chunk, source_x, 0, 0, std::move(source_runtime));
    if (!source) {
        ADD_FAILURE() << source.error().format();
        return {};
    }

    MachineRuntimeComponent consumer_runtime;
    consumer_runtime.machine_id = "offline.fluid_consumer";
    bind_machine_resources(consumer_runtime, content);
    consumer_runtime.fluid_tanks = {{
        .capacity_millibuckets = consumer_capacity,
        .transport = transport,
        .access = MachineFluidTankAccess::kInput,
    }};
    const auto consumer = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, consumer_chunk, consumer_x, 0, 0, std::move(consumer_runtime));
    if (!consumer) {
        ADD_FAILURE() << consumer.error().format();
        return {};
    }
    return {source->entity_guid.value, consumer->entity_guid.value};
}

std::vector<uint8_t> normalized_machine_payload(MachineRuntimePersistenceRecord record) {
    record.anchor_entity_id = {1};
    record.entity_guid = 1;
    record.offline_island_id = 1;
    record.offline_epoch = 1;

    GameChunk chunk;
    chunk.terrain.resize(1, 1, 1);
    chunk.machine_runtime_records.push_back(std::move(record));
    return GameChunkSerializer{}.serialize("overworld", chunk);
}

[[nodiscard]] const MachineRuntimePersistenceRecord* find_machine_record(
    const snt::game::GameChunkSidecar& sidecar, uint64_t entity_guid) {
    const auto found = std::find_if(
        sidecar.machine_runtime_records.begin(), sidecar.machine_runtime_records.end(),
        [entity_guid](const MachineRuntimePersistenceRecord& record) {
            return record.entity_guid == entity_guid;
        });
    return found == sidecar.machine_runtime_records.end() ? nullptr : &*found;
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
    bind_machine_resources(runtime, content);
    runtime.input_slots = {machine_item_stack(content, "offline.ore", 3)};
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
    EXPECT_EQ(offline_record.output_slots.front().key.id, "offline.ingot");
    EXPECT_EQ(offline_record.output_slots.front().amount, 2);
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
    EXPECT_EQ(restored.output_slots.front().amount, 3);
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
    bind_machine_resources(runtime, content);
    runtime.input_slots = {machine_item_stack(content, "offline.ore", 1)};
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
    bind_machine_resources(starting_runtime, content);
    starting_runtime.input_slots = {machine_item_stack(content, "offline.ore", 2)};
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
    EXPECT_EQ(started.machine.input_slots.front().amount, 1);

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
    EXPECT_EQ(record.input_slots.front().key.id, "offline.ore");
    EXPECT_EQ(record.input_slots.front().amount, 1);
    ASSERT_EQ(record.output_slots.size(), 1u);
    EXPECT_EQ(record.output_slots.front().key.id, "offline.ingot");
    EXPECT_EQ(record.output_slots.front().amount, 1);

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
    bind_machine_resources(runtime, content);
    runtime.input_slots = {machine_item_stack(content, "offline.ore", 2)};
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
    EXPECT_EQ(record.output_slots.front().amount, 1);
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
    bind_machine_resources(runtime, content);
    runtime.input_slots = {machine_item_stack(content, "offline.ore", 2)};
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
        restarted_world, restarted_sidecars, content.resource_runtime_index()));
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
    EXPECT_EQ(restored_runtime.output_slots.front().key,
              machine_item_stack(content, "offline.ingot", 1).key);
    EXPECT_EQ(restored_runtime.output_slots.front().amount, 2);
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
    record.fluid_tanks = {{
        .fluid = ResourceContentStack::fluid("offline.coolant", 250),
        .capacity_millibuckets = 1'000,
        .temperature_kelvin = 330,
        .pressure_pascal = 110'000,
        .transport = MachineFluidTransport::kLiquid,
        .access = MachineFluidTankAccess::kOutput,
    }};
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
    ASSERT_EQ(restored_record.fluid_tanks.size(), 1u);
    EXPECT_EQ(restored_record.fluid_tanks.front().fluid,
              ResourceContentStack::fluid("offline.coolant", 250));
    EXPECT_EQ(restored_record.fluid_tanks.front().capacity_millibuckets, 1'000);
    EXPECT_EQ(restored_record.fluid_tanks.front().transport,
              MachineFluidTransport::kLiquid);
}

TEST(OfflineMachineSimulationTest, SerializerRoundTripsOfflineNetworkIslandSnapshot) {
    const ChunkKey anchor_chunk{"overworld", -1, 0, 0};
    const ChunkKey member_chunk{"overworld", 0, 0, 0};

    GameChunk source;
    source.terrain.resize(1, 1, 1);
    source.offline_network_islands.push_back({
        .island_id = 901,
        .ownership_epoch = 17,
        .dimension_id = "overworld",
        .anchor_chunk = anchor_chunk,
        .member_chunks = {anchor_chunk, member_chunk},
        .topology_revision = 73,
        .last_simulated_tick = 12345,
        .machine_guids = {101, 202},
        .transport_segments = {
            {
                .segment_id = 73,
                .kind = OfflineNetworkResourceKind::kPower,
                .machine_guids = {101, 202},
                .capacity = 64,
                .max_transfer_per_tick = 8,
            },
            {
                .segment_id = 74,
                .kind = OfflineNetworkResourceKind::kItem,
                .machine_guids = {101, 202},
                .capacity = 32,
                .max_transfer_per_tick = 3,
            },
            {
                .segment_id = 75,
                .kind = OfflineNetworkResourceKind::kFluid,
                .fluid_transport = OfflineNetworkFluidTransport::kLiquid,
                .machine_guids = {101, 202},
                .capacity = 4000,
                .max_transfer_per_tick = 250,
            },
        },
        .boundary_ports = {{
            .segment_id = 73,
            .node_id = 444,
            .adjacent_chunk = {"overworld", 1, 0, 0},
            .direction = 1,
            .topology_revision = 73,
        }},
        .ledgers = {
            {
                .segment_id = 73,
                .kind = OfflineNetworkResourceKind::kPower,
                .resource = ResourceContentKey::power("snt.power.buffer"),
                .stored_amount = 19,
                .capacity = 64,
                .max_transfer_per_tick = 8,
            },
            {
                .segment_id = 74,
                .kind = OfflineNetworkResourceKind::kItem,
                .resource = ResourceContentKey::item("offline.item.buffer", "grade=refined"),
                .stored_amount = 7,
                .capacity = 32,
                .max_transfer_per_tick = 3,
            },
            {
                .segment_id = 75,
                .kind = OfflineNetworkResourceKind::kFluid,
                .resource = ResourceContentKey::fluid("offline.fluid.buffer"),
                .stored_amount = 1200,
                .capacity = 4000,
                .max_transfer_per_tick = 250,
            },
        },
    });

    const GameChunkSerializer serializer;
    const auto payload = serializer.serialize("overworld", source);
    GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(payload, dimension_id, restored));
    ASSERT_EQ(restored.offline_network_islands.size(), 1u);
    const OfflineNetworkIslandSnapshot& snapshot = restored.offline_network_islands.front();
    EXPECT_EQ(snapshot.island_id, 901u);
    EXPECT_EQ(snapshot.ownership_epoch, 17u);
    EXPECT_EQ(snapshot.dimension_id, "overworld");
    EXPECT_EQ(snapshot.anchor_chunk.dimension_id, anchor_chunk.dimension_id);
    EXPECT_EQ(snapshot.anchor_chunk.chunk_x, anchor_chunk.chunk_x);
    ASSERT_EQ(snapshot.member_chunks.size(), 2u);
    EXPECT_EQ(snapshot.member_chunks[1].chunk_x, member_chunk.chunk_x);
    EXPECT_EQ(snapshot.topology_revision, 73u);
    EXPECT_EQ(snapshot.last_simulated_tick, 12345u);
    EXPECT_EQ(snapshot.machine_guids, (std::vector<uint64_t>{101, 202}));
    ASSERT_EQ(snapshot.transport_segments.size(), 3u);
    EXPECT_EQ(snapshot.transport_segments[0].segment_id, 73u);
    EXPECT_EQ(snapshot.transport_segments[0].kind, OfflineNetworkResourceKind::kPower);
    EXPECT_EQ(snapshot.transport_segments[1].segment_id, 74u);
    EXPECT_EQ(snapshot.transport_segments[1].kind, OfflineNetworkResourceKind::kItem);
    EXPECT_EQ(snapshot.transport_segments[2].segment_id, 75u);
    EXPECT_EQ(snapshot.transport_segments[2].kind, OfflineNetworkResourceKind::kFluid);
    EXPECT_EQ(snapshot.transport_segments[2].fluid_transport,
              OfflineNetworkFluidTransport::kLiquid);
    EXPECT_EQ(snapshot.transport_segments[2].max_transfer_per_tick, 250);
    ASSERT_EQ(snapshot.boundary_ports.size(), 1u);
    EXPECT_EQ(snapshot.boundary_ports.front().segment_id, 73u);
    EXPECT_EQ(snapshot.boundary_ports.front().node_id, 444u);
    EXPECT_EQ(snapshot.boundary_ports.front().adjacent_chunk.chunk_x, 1);
    EXPECT_EQ(snapshot.boundary_ports.front().direction, 1u);
    ASSERT_EQ(snapshot.ledgers.size(), 3u);
    EXPECT_EQ(snapshot.ledgers[0].kind, OfflineNetworkResourceKind::kPower);
    EXPECT_EQ(snapshot.ledgers[0].stored_amount, 19);
    EXPECT_EQ(snapshot.ledgers[0].resource,
              ResourceContentKey::power("snt.power.buffer"));
    EXPECT_EQ(snapshot.ledgers[1].kind, OfflineNetworkResourceKind::kItem);
    EXPECT_EQ(snapshot.ledgers[1].resource,
              ResourceContentKey::item("offline.item.buffer", "grade=refined"));
    EXPECT_EQ(snapshot.ledgers[2].kind, OfflineNetworkResourceKind::kFluid);
    EXPECT_EQ(snapshot.ledgers[2].resource,
              ResourceContentKey::fluid("offline.fluid.buffer"));
    EXPECT_EQ(snapshot.ledgers[2].max_transfer_per_tick, 250);
}

TEST(OfflineMachineSimulationTest, NetworkIslandRegistryClaimsReleasesAndRecoversEpochOwnership) {
    GameChunkSidecarRegistry sidecars;
    const ChunkKey left_chunk = make_chunk_key(0);
    const ChunkKey right_chunk = make_chunk_key(1);
    sidecars.set(left_chunk, {});
    sidecars.set(right_chunk, {});

    snt::ecs::World world;
    MachineRuntimeComponent left_runtime;
    left_runtime.machine_id = "offline.registry_left";
    left_runtime.resource_runtime_index = empty_machine_snapshot();
    const auto left = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, left_chunk, 0, 0, 0, std::move(left_runtime));
    ASSERT_TRUE(left);
    MachineRuntimeComponent right_runtime;
    right_runtime.machine_id = "offline.registry_right";
    right_runtime.resource_runtime_index = empty_machine_snapshot();
    const auto right = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, right_chunk, snt::voxel::VoxelChunk::kChunkSize, 0, 0,
        std::move(right_runtime));
    ASSERT_TRUE(right);

    OfflineNetworkIslandSnapshot snapshot{
        .island_id = 700,
        .dimension_id = "overworld",
        .anchor_chunk = left_chunk,
        .member_chunks = {left_chunk, right_chunk},
        .topology_revision = 2,
        .machine_guids = {left->entity_guid.value, right->entity_guid.value},
        .transport_segments = {{
            .segment_id = 2,
            .kind = OfflineNetworkResourceKind::kPower,
            .machine_guids = {left->entity_guid.value, right->entity_guid.value},
            .capacity = 42,
            .max_transfer_per_tick = 3,
        }},
        .ledgers = {{
            .segment_id = 2,
            .kind = OfflineNetworkResourceKind::kPower,
            .resource = ResourceContentKey::power("snt.power.buffer"),
            .capacity = 42,
            .max_transfer_per_tick = 3,
        }},
    };

    OfflineNetworkIslandRegistry registry(sidecars);
    ASSERT_TRUE(registry.initialize());
    const auto claim = registry.claim(std::move(snapshot), 50);
    ASSERT_TRUE(claim);
    EXPECT_EQ(claim->ownership_epoch, 1u);
    const auto* left_sidecar = sidecars.get(left_chunk);
    const auto* right_sidecar = sidecars.get(right_chunk);
    ASSERT_NE(left_sidecar, nullptr);
    ASSERT_NE(right_sidecar, nullptr);
    ASSERT_EQ(left_sidecar->offline_network_islands.size(), 1u);
    EXPECT_EQ(left_sidecar->offline_network_islands.front().ownership_epoch,
              claim->ownership_epoch);
    ASSERT_EQ(left_sidecar->machine_runtime_records.size(), 1u);
    ASSERT_EQ(right_sidecar->machine_runtime_records.size(), 1u);
    EXPECT_EQ(left_sidecar->machine_runtime_records.front().residency,
              MachineRuntimeResidency::kOfflineNetworkIsland);
    EXPECT_EQ(right_sidecar->machine_runtime_records.front().offline_island_id,
              claim->island_id);
    EXPECT_EQ(right_sidecar->machine_runtime_records.front().offline_epoch,
              claim->ownership_epoch);

    OfflineNetworkIslandRegistry restarted_registry(sidecars);
    ASSERT_TRUE(restarted_registry.initialize());
    ASSERT_EQ(restarted_registry.size(), 1u);
    const auto release = restarted_registry.prepare_release(
        claim->island_id, claim->ownership_epoch);
    ASSERT_TRUE(release);
    EXPECT_EQ(release->last_simulated_tick, 50u);
    EXPECT_FALSE(restarted_registry.prepare_release(
        claim->island_id, claim->ownership_epoch + 1));

    for (const ChunkKey& chunk_key : release->member_chunks) {
        auto* sidecar = sidecars.get(chunk_key);
        ASSERT_NE(sidecar, nullptr);
        for (MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
            record.residency = MachineRuntimeResidency::kLoaded;
            record.offline_island_id = 0;
            ++record.offline_epoch;
        }
    }
    ASSERT_TRUE(restarted_registry.complete_release(
        claim->island_id, claim->ownership_epoch));
    EXPECT_TRUE(left_sidecar->offline_network_islands.empty());
    EXPECT_EQ(restarted_registry.size(), 0u);

    OfflineNetworkIslandRegistry after_release(sidecars);
    ASSERT_TRUE(after_release.initialize());
    EXPECT_EQ(after_release.size(), 0u);
}

TEST(OfflineMachineSimulationTest, NetworkIslandRegistryRejectsMismatchedRestartOwnership) {
    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    MachineRuntimeComponent runtime;
    runtime.machine_id = "offline.registry_corrupt";
    runtime.resource_runtime_index = empty_machine_snapshot();
    const auto machine = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, std::move(runtime));
    ASSERT_TRUE(machine);

    OfflineNetworkIslandRegistry registry(sidecars);
    ASSERT_TRUE(registry.initialize());
    const auto claim = registry.claim({
        .island_id = 701,
        .dimension_id = "overworld",
        .anchor_chunk = chunk_key,
        .member_chunks = {chunk_key},
        .topology_revision = 4,
        .machine_guids = {machine->entity_guid.value},
    }, 7);
    ASSERT_TRUE(claim);
    auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    sidecar->machine_runtime_records.front().offline_island_id = 702;

    OfflineNetworkIslandRegistry restarted_registry(sidecars);
    EXPECT_FALSE(restarted_registry.initialize());
}

TEST(OfflineMachineSimulationTest, PowerTopologyKeepsPartialTicketedComponentMaterialized) {
    GameContentRegistry content;
    register_power_network_content(content);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey left_chunk = make_chunk_key(0);
    const ChunkKey right_chunk = make_chunk_key(1);
    sidecars.set(left_chunk, {});
    sidecars.set(right_chunk, {});
    snt::ecs::World world;
    const int32_t chunk_size = snt::voxel::VoxelChunk::kChunkSize;
    const PowerMachinePair machines = create_power_machine_pair(
        content, world, sidecars, left_chunk, chunk_size - 1, right_chunk, chunk_size + 1,
        10, 0, 1);
    ASSERT_NE(machines.source_guid, 0u);
    ASSERT_NE(machines.consumer_guid, 0u);
    add_power_cable(sidecars, right_chunk, 9001, chunk_size, 0, 0,
                    static_cast<uint8_t>(snt::game::CONN_POS_X |
                                         snt::game::CONN_NEG_X));

    OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
    const std::vector<ChunkKey> left_candidate{left_chunk};
    const auto partial_topology = provider.build_offline_islands(left_candidate, 100);
    ASSERT_TRUE(partial_topology);
    EXPECT_TRUE(partial_topology->empty());

    OfflineIndustrialNetworkIslandSimulator simulator;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_network_island_provider(&provider);
    offline.set_network_island_simulator(&simulator);
    ASSERT_TRUE(offline.initialize(100));
    const auto partial_transition = offline.dematerialize_chunks(
        world, left_candidate, 100);
    ASSERT_TRUE(partial_transition);
    EXPECT_EQ(partial_transition->network_island_count, 0u);
    EXPECT_EQ(partial_transition->deferred_network_machine_count, 1u);
    const auto* left_sidecar = sidecars.get(left_chunk);
    ASSERT_NE(left_sidecar, nullptr);
    EXPECT_EQ(left_sidecar->machine_runtime_records.front().residency,
              MachineRuntimeResidency::kLoaded);
    EXPECT_TRUE(world.find_entity_by_guid(snt::ecs::EntityGuid{machines.source_guid}) != entt::null);

    const std::vector<ChunkKey> complete_candidates{left_chunk, right_chunk};
    const auto complete_transition = offline.dematerialize_chunks(
        world, complete_candidates, 100);
    ASSERT_TRUE(complete_transition);
    EXPECT_EQ(complete_transition->network_island_count, 1u);
    EXPECT_EQ(complete_transition->network_island_machine_count, 2u);
    EXPECT_EQ(complete_transition->deferred_network_machine_count, 0u);
    EXPECT_EQ(offline.offline_network_island_count(), 1u);
    EXPECT_TRUE(world.find_entity_by_guid(snt::ecs::EntityGuid{machines.source_guid}) == entt::null);
    EXPECT_TRUE(world.find_entity_by_guid(snt::ecs::EntityGuid{machines.consumer_guid}) == entt::null);
}

TEST(OfflineMachineSimulationTest, TicketExpansionIncludesEveryOfflineIslandMemberChunk) {
    GameContentRegistry content;
    register_power_network_content(content);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey left_chunk = make_chunk_key(0);
    const ChunkKey right_chunk = make_chunk_key(1);
    sidecars.set(left_chunk, {});
    sidecars.set(right_chunk, {});
    snt::ecs::World world;
    const int32_t chunk_size = snt::voxel::VoxelChunk::kChunkSize;
    const PowerMachinePair machines = create_power_machine_pair(
        content, world, sidecars, left_chunk, chunk_size - 1, right_chunk, chunk_size + 1,
        10, 0, 1);
    ASSERT_NE(machines.source_guid, 0u);
    ASSERT_NE(machines.consumer_guid, 0u);
    add_power_cable(sidecars, right_chunk, 9005, chunk_size, 0, 0,
                    static_cast<uint8_t>(snt::game::CONN_POS_X |
                                         snt::game::CONN_NEG_X));

    OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
    OfflineIndustrialNetworkIslandSimulator simulator;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_network_island_provider(&provider);
    offline.set_network_island_simulator(&simulator);
    ASSERT_TRUE(offline.initialize(100));
    const std::vector<ChunkKey> all_members{left_chunk, right_chunk};
    ASSERT_TRUE(offline.dematerialize_chunks(world, all_members, 100));

    const std::vector<ChunkKey> one_member_ticket{right_chunk};
    const auto expanded = offline.expand_materialization_chunks(one_member_ticket);
    ASSERT_TRUE(expanded);
    EXPECT_EQ(*expanded, all_members);
}

TEST(OfflineMachineSimulationTest, PowerIslandTransfersBufferedEnergyAndRestoresMachineProgress) {
    GameContentRegistry content;
    register_power_network_content(content);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    const PowerMachinePair machines = create_power_machine_pair(
        content, world, sidecars, chunk_key, 0, chunk_key, 2, 10, 0, 2);
    ASSERT_NE(machines.source_guid, 0u);
    ASSERT_NE(machines.consumer_guid, 0u);
    add_power_cable(sidecars, chunk_key, 9002, 1, 0, 0,
                    static_cast<uint8_t>(snt::game::CONN_POS_X |
                                         snt::game::CONN_NEG_X));

    OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
    OfflineIndustrialNetworkIslandSimulator simulator;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_network_island_provider(&provider);
    offline.set_network_island_simulator(&simulator);
    ASSERT_TRUE(offline.initialize(100));
    const std::vector<ChunkKey> candidates{chunk_key};
    const auto transition = offline.dematerialize_chunks(world, candidates, 100);
    ASSERT_TRUE(transition);
    EXPECT_EQ(transition->network_island_count, 1u);
    EXPECT_EQ(transition->network_island_machine_count, 2u);
    ASSERT_TRUE(offline.tick(105));

    auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->machine_runtime_records.size(), 2u);
    const MachineRuntimePersistenceRecord& source = sidecar->machine_runtime_records[0];
    const MachineRuntimePersistenceRecord& consumer = sidecar->machine_runtime_records[1];
    EXPECT_EQ(source.stored_energy, 0);
    EXPECT_EQ(consumer.stored_energy, 6);
    ASSERT_EQ(consumer.output_slots.size(), 1u);
    EXPECT_EQ(consumer.output_slots.front().key.id, "offline.power_ingot");
    EXPECT_EQ(consumer.output_slots.front().amount, 2);
    ASSERT_EQ(sidecar->offline_network_islands.size(), 1u);
    const OfflineNetworkIslandSnapshot& snapshot = sidecar->offline_network_islands.front();
    ASSERT_EQ(snapshot.ledgers.size(), 1u);
    EXPECT_EQ(snapshot.ledgers.front().kind, OfflineNetworkResourceKind::kPower);
    EXPECT_EQ(snapshot.ledgers.front().stored_amount, 0);
    EXPECT_EQ(snapshot.last_simulated_tick, 105u);

    ASSERT_TRUE(offline.materialize_chunk(world, chunk_key, 105));
    EXPECT_TRUE(sidecar->offline_network_islands.empty());
    EXPECT_EQ(sidecar->machine_runtime_records[0].residency, MachineRuntimeResidency::kLoaded);
    EXPECT_EQ(sidecar->machine_runtime_records[1].residency, MachineRuntimeResidency::kLoaded);
    EXPECT_TRUE(world.find_entity_by_guid(snt::ecs::EntityGuid{machines.source_guid}) != entt::null);
    EXPECT_TRUE(world.find_entity_by_guid(snt::ecs::EntityGuid{machines.consumer_guid}) != entt::null);
}

TEST(OfflineMachineSimulationTest, IndustrialTopologySeparatesPowerAndItemSegments) {
    GameContentRegistry content;
    register_power_network_content(content);
    register_item_network_content(content);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;

    MachineRuntimeComponent power_runtime;
    power_runtime.machine_id = "offline.power_source";
    bind_machine_resources(power_runtime, content);
    const auto power_machine = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 0, 0, 0, std::move(power_runtime));
    ASSERT_TRUE(power_machine);
    MachineRuntimeComponent bridge_runtime;
    bridge_runtime.machine_id = "offline.item_source";
    bind_machine_resources(bridge_runtime, content);
    const auto bridge_machine = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 2, 0, 0, std::move(bridge_runtime));
    ASSERT_TRUE(bridge_machine);
    MachineRuntimeComponent item_runtime;
    item_runtime.machine_id = "offline.item_consumer";
    bind_machine_resources(item_runtime, content);
    const auto item_machine = snt::game::GameMachineRuntimePersistence::create_anchored_machine(
        world, sidecars, chunk_key, 4, 0, 0, std::move(item_runtime));
    ASSERT_TRUE(item_machine);
    add_power_cable(sidecars, chunk_key, 9201, 1, 0, 0,
                    static_cast<uint8_t>(snt::game::CONN_POS_X |
                                         snt::game::CONN_NEG_X));
    add_pipe(sidecars, chunk_key, 9202, 3, 0, 0,
             static_cast<uint8_t>(snt::game::CONN_POS_X |
                                  snt::game::CONN_NEG_X));

    OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
    const std::vector<ChunkKey> candidates{chunk_key};
    const auto built = provider.build_offline_islands(candidates, 25);
    ASSERT_TRUE(built);
    ASSERT_EQ(built->size(), 1u);
    const OfflineNetworkIslandSnapshot& snapshot = built->front();
    EXPECT_EQ(snapshot.machine_guids,
              (std::vector<uint64_t>{power_machine->entity_guid.value,
                                     bridge_machine->entity_guid.value,
                                     item_machine->entity_guid.value}));
    ASSERT_EQ(snapshot.transport_segments.size(), 2u);
    const auto power_segment = std::find_if(
        snapshot.transport_segments.begin(), snapshot.transport_segments.end(),
        [](const OfflineNetworkTransportSegment& segment) {
            return segment.kind == OfflineNetworkResourceKind::kPower;
        });
    const auto item_segment = std::find_if(
        snapshot.transport_segments.begin(), snapshot.transport_segments.end(),
        [](const OfflineNetworkTransportSegment& segment) {
            return segment.kind == OfflineNetworkResourceKind::kItem;
        });
    ASSERT_NE(power_segment, snapshot.transport_segments.end());
    ASSERT_NE(item_segment, snapshot.transport_segments.end());
    EXPECT_EQ(power_segment->machine_guids,
              (std::vector<uint64_t>{power_machine->entity_guid.value,
                                     bridge_machine->entity_guid.value}));
    EXPECT_EQ(item_segment->machine_guids,
              (std::vector<uint64_t>{bridge_machine->entity_guid.value,
                                     item_machine->entity_guid.value}));
    EXPECT_EQ(item_segment->capacity, 0);
    EXPECT_EQ(item_segment->max_transfer_per_tick, 1);
    ASSERT_EQ(snapshot.ledgers.size(), 1u);
    EXPECT_EQ(snapshot.ledgers.front().segment_id, power_segment->segment_id);
}

TEST(OfflineMachineSimulationTest, ItemIslandTransfersOutputsBeforeMachineExecution) {
    GameContentRegistry content;
    register_item_network_content(content, 1, 2, 1);

    GameChunkSidecarRegistry sidecars;
    const ChunkKey chunk_key = make_chunk_key();
    sidecars.set(chunk_key, {});
    snt::ecs::World world;
    const ItemMachinePair machines = create_item_machine_pair(
        content, world, sidecars, chunk_key, 0, chunk_key, 2, 3);
    ASSERT_NE(machines.source_guid, 0u);
    ASSERT_NE(machines.consumer_guid, 0u);
    add_pipe(sidecars, chunk_key, 9203, 1, 0, 0,
             static_cast<uint8_t>(snt::game::CONN_POS_X |
                                  snt::game::CONN_NEG_X));

    OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
    OfflineIndustrialNetworkIslandSimulator simulator;
    OfflineMachineSimulationService offline(content, sidecars);
    offline.set_network_island_provider(&provider);
    offline.set_network_island_simulator(&simulator);
    ASSERT_TRUE(offline.initialize(0));
    const std::vector<ChunkKey> candidates{chunk_key};
    ASSERT_TRUE(offline.dematerialize_chunks(world, candidates, 0));
    ASSERT_TRUE(offline.tick(3));

    const auto* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->machine_runtime_records.size(), 2u);
    const MachineRuntimePersistenceRecord& source = sidecar->machine_runtime_records[0];
    const MachineRuntimePersistenceRecord& consumer = sidecar->machine_runtime_records[1];
    EXPECT_TRUE(source.output_slots.empty());
    EXPECT_TRUE(consumer.input_slots.empty());
    ASSERT_EQ(consumer.output_slots.size(), 1u);
    EXPECT_EQ(consumer.output_slots.front().key.id, "offline.item_ingot");
    EXPECT_EQ(consumer.output_slots.front().amount, 3);
    ASSERT_EQ(sidecar->offline_network_islands.size(), 1u);
    const OfflineNetworkIslandSnapshot& snapshot = sidecar->offline_network_islands.front();
    ASSERT_EQ(snapshot.transport_segments.size(), 1u);
    EXPECT_EQ(snapshot.transport_segments.front().kind, OfflineNetworkResourceKind::kItem);
    EXPECT_EQ(snapshot.transport_segments.front().capacity, 0);
    EXPECT_EQ(snapshot.transport_segments.front().max_transfer_per_tick, 1);
    EXPECT_TRUE(snapshot.ledgers.empty());
}

TEST(OfflineMachineSimulationTest, FluidIslandsTransferLiquidAndGasThroughTypedPipes) {
    GameContentRegistry content;
    register_fluid_network_content(content, 3, 150);

    const std::array phases{
        std::pair{PipeType::LIQUID, MachineFluidTransport::kLiquid},
        std::pair{PipeType::GAS, MachineFluidTransport::kGas},
    };
    for (const auto [pipe_type, transport] : phases) {
        SCOPED_TRACE(pipe_type == PipeType::LIQUID ? "liquid" : "gas");
        GameChunkSidecarRegistry sidecars;
        const ChunkKey chunk_key = make_chunk_key();
        sidecars.set(chunk_key, {});
        snt::ecs::World world;
        const FluidMachinePair machines = create_fluid_machine_pair(
            content, world, sidecars, chunk_key, 0, chunk_key, 2, transport,
            transport == MachineFluidTransport::kLiquid ? "offline.coolant" : "offline.steam",
            600);
        ASSERT_NE(machines.source_guid, 0u);
        ASSERT_NE(machines.consumer_guid, 0u);
        add_pipe(sidecars, chunk_key,
                 pipe_type == PipeType::LIQUID ? 9204 : 9205,
                 1, 0, 0,
                 static_cast<uint8_t>(snt::game::CONN_POS_X |
                                      snt::game::CONN_NEG_X),
                 pipe_type);

        OfflineIndustrialNetworkIslandProvider provider(content, sidecars);
        OfflineIndustrialNetworkIslandSimulator simulator;
        OfflineMachineSimulationService offline(content, sidecars);
        offline.set_network_island_provider(&provider);
        offline.set_network_island_simulator(&simulator);
        ASSERT_TRUE(offline.initialize(10));
        const std::vector<ChunkKey> candidates{chunk_key};
        const auto transition = offline.dematerialize_chunks(world, candidates, 10);
        ASSERT_TRUE(transition);
        EXPECT_EQ(transition->network_island_count, 1u);
        ASSERT_TRUE(offline.tick(13));

        const auto* sidecar = sidecars.get(chunk_key);
        ASSERT_NE(sidecar, nullptr);
        ASSERT_EQ(sidecar->machine_runtime_records.size(), 2u);
        const MachineRuntimePersistenceRecord* source_record =
            find_machine_record(*sidecar, machines.source_guid);
        const MachineRuntimePersistenceRecord* consumer_record =
            find_machine_record(*sidecar, machines.consumer_guid);
        ASSERT_NE(source_record, nullptr);
        ASSERT_NE(consumer_record, nullptr);
        ASSERT_EQ(source_record->fluid_tanks.size(), 1u);
        ASSERT_EQ(consumer_record->fluid_tanks.size(), 1u);
        const MachineFluidTankRecord& source_tank = source_record->fluid_tanks.front();
        const MachineFluidTankRecord& consumer_tank = consumer_record->fluid_tanks.front();
        const int64_t transferred_per_tick = pipe_type == PipeType::LIQUID ? 100 : 150;
        EXPECT_EQ(source_tank.fluid.amount, 600 - transferred_per_tick * 3);
        EXPECT_EQ(consumer_tank.fluid.amount, transferred_per_tick * 3);
        EXPECT_EQ(consumer_tank.transport, transport);
        EXPECT_EQ(consumer_tank.temperature_kelvin, source_tank.temperature_kelvin);
        EXPECT_EQ(consumer_tank.pressure_pascal, source_tank.pressure_pascal);
        ASSERT_EQ(sidecar->offline_network_islands.size(), 1u);
        ASSERT_EQ(sidecar->offline_network_islands.front().transport_segments.size(), 1u);
        EXPECT_EQ(sidecar->offline_network_islands.front().transport_segments.front().kind,
                  OfflineNetworkResourceKind::kFluid);
        EXPECT_EQ(sidecar->offline_network_islands.front().transport_segments.front().fluid_transport,
                  transport == MachineFluidTransport::kLiquid
                      ? OfflineNetworkFluidTransport::kLiquid
                      : OfflineNetworkFluidTransport::kGas);
    }
}

TEST(OfflineMachineSimulationTest, RandomPowerIslandPerTickAndBatchedExecutionAreEquivalent) {
    uint32_t random_state = 0x8b31f18du;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13u;
        random_state ^= random_state >> 17u;
        random_state ^= random_state << 5u;
        return random_state;
    };

    for (size_t case_index = 0; case_index < 24; ++case_index) {
        SCOPED_TRACE("power case " + std::to_string(case_index));
        const uint32_t batch_ticks = 2u + next_random() % 6u;
        const uint32_t total_ticks = 1u + next_random() % 31u;
        const int32_t transfer_per_tick = 1 + static_cast<int32_t>(next_random() % 5u);
        const int32_t recipe_duration = 1 + static_cast<int32_t>(next_random() % 4u);
        const int32_t source_energy = static_cast<int32_t>(next_random() % 33u);
        const int32_t consumer_energy = static_cast<int32_t>(next_random() % 8u);
        const int32_t input_count = static_cast<int32_t>(next_random() % 7u);

        GameContentRegistry per_tick_content;
        register_power_network_content(per_tick_content, 1, transfer_per_tick, recipe_duration);
        GameChunkSidecarRegistry per_tick_sidecars;
        const ChunkKey chunk_key = make_chunk_key();
        per_tick_sidecars.set(chunk_key, {});
        snt::ecs::World per_tick_world;
        const PowerMachinePair per_tick_machines = create_power_machine_pair(
            per_tick_content, per_tick_world, per_tick_sidecars, chunk_key, 0, chunk_key, 2,
            source_energy, consumer_energy, input_count);
        ASSERT_NE(per_tick_machines.source_guid, 0u);
        ASSERT_NE(per_tick_machines.consumer_guid, 0u);
        add_power_cable(per_tick_sidecars, chunk_key, 9100 + case_index, 1, 0, 0,
                        static_cast<uint8_t>(snt::game::CONN_POS_X |
                                             snt::game::CONN_NEG_X));
        OfflineIndustrialNetworkIslandProvider per_tick_provider(per_tick_content, per_tick_sidecars);
        OfflineIndustrialNetworkIslandSimulator per_tick_simulator;
        OfflineMachineSimulationService per_tick_service(per_tick_content, per_tick_sidecars);
        per_tick_service.set_network_island_provider(&per_tick_provider);
        per_tick_service.set_network_island_simulator(&per_tick_simulator);
        ASSERT_TRUE(per_tick_service.initialize(0));
        const std::vector<ChunkKey> candidates{chunk_key};
        ASSERT_TRUE(per_tick_service.dematerialize_chunks(per_tick_world, candidates, 0));
        for (uint32_t tick = 1; tick <= total_ticks; ++tick) {
            ASSERT_TRUE(per_tick_service.tick(tick));
        }

        GameContentRegistry batched_content;
        register_power_network_content(
            batched_content, batch_ticks, transfer_per_tick, recipe_duration);
        GameChunkSidecarRegistry batched_sidecars;
        batched_sidecars.set(chunk_key, {});
        snt::ecs::World batched_world;
        const PowerMachinePair batched_machines = create_power_machine_pair(
            batched_content, batched_world, batched_sidecars, chunk_key, 0, chunk_key, 2,
            source_energy, consumer_energy, input_count);
        ASSERT_NE(batched_machines.source_guid, 0u);
        ASSERT_NE(batched_machines.consumer_guid, 0u);
        add_power_cable(batched_sidecars, chunk_key, 9200 + case_index, 1, 0, 0,
                        static_cast<uint8_t>(snt::game::CONN_POS_X |
                                             snt::game::CONN_NEG_X));
        OfflineIndustrialNetworkIslandProvider batched_provider(batched_content, batched_sidecars);
        OfflineIndustrialNetworkIslandSimulator batched_simulator;
        OfflineMachineSimulationService batched_service(batched_content, batched_sidecars);
        batched_service.set_network_island_provider(&batched_provider);
        batched_service.set_network_island_simulator(&batched_simulator);
        ASSERT_TRUE(batched_service.initialize(0));
        ASSERT_TRUE(batched_service.dematerialize_chunks(batched_world, candidates, 0));
        for (uint32_t tick = batch_ticks; tick <= total_ticks; tick += batch_ticks) {
            ASSERT_TRUE(batched_service.tick(tick));
        }
        if (total_ticks % batch_ticks != 0) {
            ASSERT_TRUE(batched_service.flush(total_ticks));
        }

        const auto* per_tick_sidecar = per_tick_sidecars.get(chunk_key);
        const auto* batched_sidecar = batched_sidecars.get(chunk_key);
        ASSERT_NE(per_tick_sidecar, nullptr);
        ASSERT_NE(batched_sidecar, nullptr);
        ASSERT_EQ(per_tick_sidecar->machine_runtime_records.size(), 2u);
        ASSERT_EQ(batched_sidecar->machine_runtime_records.size(), 2u);
        EXPECT_EQ(normalized_machine_payload(per_tick_sidecar->machine_runtime_records[0]),
                  normalized_machine_payload(batched_sidecar->machine_runtime_records[0]));
        EXPECT_EQ(normalized_machine_payload(per_tick_sidecar->machine_runtime_records[1]),
                  normalized_machine_payload(batched_sidecar->machine_runtime_records[1]));
        ASSERT_EQ(per_tick_sidecar->offline_network_islands.size(), 1u);
        ASSERT_EQ(batched_sidecar->offline_network_islands.size(), 1u);
        const OfflineNetworkIslandSnapshot& per_tick_snapshot =
            per_tick_sidecar->offline_network_islands.front();
        const OfflineNetworkIslandSnapshot& batched_snapshot =
            batched_sidecar->offline_network_islands.front();
        ASSERT_EQ(per_tick_snapshot.ledgers.size(), 1u);
        ASSERT_EQ(batched_snapshot.ledgers.size(), 1u);
        EXPECT_EQ(per_tick_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(batched_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(per_tick_snapshot.ledgers.front().kind,
                  batched_snapshot.ledgers.front().kind);
        EXPECT_EQ(per_tick_snapshot.ledgers.front().resource,
                  batched_snapshot.ledgers.front().resource);
        EXPECT_EQ(per_tick_snapshot.ledgers.front().stored_amount,
                  batched_snapshot.ledgers.front().stored_amount);
        EXPECT_EQ(per_tick_snapshot.ledgers.front().capacity,
                  batched_snapshot.ledgers.front().capacity);
        EXPECT_EQ(per_tick_snapshot.ledgers.front().max_transfer_per_tick,
                  batched_snapshot.ledgers.front().max_transfer_per_tick);
    }
}

TEST(OfflineMachineSimulationTest, RandomItemIslandPerTickAndBatchedExecutionAreEquivalent) {
    uint32_t random_state = 0x75f2a9c1u;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13u;
        random_state ^= random_state >> 17u;
        random_state ^= random_state << 5u;
        return random_state;
    };

    for (size_t case_index = 0; case_index < 24; ++case_index) {
        SCOPED_TRACE("item case " + std::to_string(case_index));
        const uint32_t batch_ticks = 2u + next_random() % 6u;
        const uint32_t total_ticks = 1u + next_random() % 31u;
        const int32_t transfer_per_tick = 1 + static_cast<int32_t>(next_random() % 5u);
        const int32_t recipe_duration = 1 + static_cast<int32_t>(next_random() % 4u);
        const int32_t output_count = static_cast<int32_t>(next_random() % 12u);

        GameContentRegistry per_tick_content;
        register_item_network_content(per_tick_content, 1, transfer_per_tick, recipe_duration);
        GameChunkSidecarRegistry per_tick_sidecars;
        const ChunkKey chunk_key = make_chunk_key();
        per_tick_sidecars.set(chunk_key, {});
        snt::ecs::World per_tick_world;
        const ItemMachinePair per_tick_machines = create_item_machine_pair(
            per_tick_content, per_tick_world, per_tick_sidecars, chunk_key, 0, chunk_key, 2,
            output_count);
        ASSERT_NE(per_tick_machines.source_guid, 0u);
        ASSERT_NE(per_tick_machines.consumer_guid, 0u);
        add_pipe(per_tick_sidecars, chunk_key, 9300 + case_index, 1, 0, 0,
                 static_cast<uint8_t>(snt::game::CONN_POS_X |
                                      snt::game::CONN_NEG_X));
        OfflineIndustrialNetworkIslandProvider per_tick_provider(
            per_tick_content, per_tick_sidecars);
        OfflineIndustrialNetworkIslandSimulator per_tick_simulator;
        OfflineMachineSimulationService per_tick_service(per_tick_content, per_tick_sidecars);
        per_tick_service.set_network_island_provider(&per_tick_provider);
        per_tick_service.set_network_island_simulator(&per_tick_simulator);
        ASSERT_TRUE(per_tick_service.initialize(0));
        const std::vector<ChunkKey> candidates{chunk_key};
        ASSERT_TRUE(per_tick_service.dematerialize_chunks(per_tick_world, candidates, 0));
        for (uint32_t tick = 1; tick <= total_ticks; ++tick) {
            ASSERT_TRUE(per_tick_service.tick(tick));
        }

        GameContentRegistry batched_content;
        register_item_network_content(
            batched_content, batch_ticks, transfer_per_tick, recipe_duration);
        GameChunkSidecarRegistry batched_sidecars;
        batched_sidecars.set(chunk_key, {});
        snt::ecs::World batched_world;
        const ItemMachinePair batched_machines = create_item_machine_pair(
            batched_content, batched_world, batched_sidecars, chunk_key, 0, chunk_key, 2,
            output_count);
        ASSERT_NE(batched_machines.source_guid, 0u);
        ASSERT_NE(batched_machines.consumer_guid, 0u);
        add_pipe(batched_sidecars, chunk_key, 9400 + case_index, 1, 0, 0,
                 static_cast<uint8_t>(snt::game::CONN_POS_X |
                                      snt::game::CONN_NEG_X));
        OfflineIndustrialNetworkIslandProvider batched_provider(batched_content, batched_sidecars);
        OfflineIndustrialNetworkIslandSimulator batched_simulator;
        OfflineMachineSimulationService batched_service(batched_content, batched_sidecars);
        batched_service.set_network_island_provider(&batched_provider);
        batched_service.set_network_island_simulator(&batched_simulator);
        ASSERT_TRUE(batched_service.initialize(0));
        ASSERT_TRUE(batched_service.dematerialize_chunks(batched_world, candidates, 0));
        for (uint32_t tick = batch_ticks; tick <= total_ticks; tick += batch_ticks) {
            ASSERT_TRUE(batched_service.tick(tick));
        }
        if (total_ticks % batch_ticks != 0) {
            ASSERT_TRUE(batched_service.flush(total_ticks));
        }

        const auto* per_tick_sidecar = per_tick_sidecars.get(chunk_key);
        const auto* batched_sidecar = batched_sidecars.get(chunk_key);
        ASSERT_NE(per_tick_sidecar, nullptr);
        ASSERT_NE(batched_sidecar, nullptr);
        ASSERT_EQ(per_tick_sidecar->machine_runtime_records.size(), 2u);
        ASSERT_EQ(batched_sidecar->machine_runtime_records.size(), 2u);
        EXPECT_EQ(normalized_machine_payload(per_tick_sidecar->machine_runtime_records[0]),
                  normalized_machine_payload(batched_sidecar->machine_runtime_records[0]));
        EXPECT_EQ(normalized_machine_payload(per_tick_sidecar->machine_runtime_records[1]),
                  normalized_machine_payload(batched_sidecar->machine_runtime_records[1]));
        ASSERT_EQ(per_tick_sidecar->offline_network_islands.size(), 1u);
        ASSERT_EQ(batched_sidecar->offline_network_islands.size(), 1u);
        const OfflineNetworkIslandSnapshot& per_tick_snapshot =
            per_tick_sidecar->offline_network_islands.front();
        const OfflineNetworkIslandSnapshot& batched_snapshot =
            batched_sidecar->offline_network_islands.front();
        ASSERT_EQ(per_tick_snapshot.transport_segments.size(), 1u);
        ASSERT_EQ(batched_snapshot.transport_segments.size(), 1u);
        EXPECT_EQ(per_tick_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(batched_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().kind,
                  OfflineNetworkResourceKind::kItem);
        EXPECT_EQ(batched_snapshot.transport_segments.front().kind,
                  OfflineNetworkResourceKind::kItem);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().capacity,
                  batched_snapshot.transport_segments.front().capacity);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().max_transfer_per_tick,
                  batched_snapshot.transport_segments.front().max_transfer_per_tick);
        EXPECT_TRUE(per_tick_snapshot.ledgers.empty());
        EXPECT_TRUE(batched_snapshot.ledgers.empty());
    }
}

TEST(OfflineMachineSimulationTest, RandomFluidIslandPerTickAndBatchedExecutionAreEquivalent) {
    uint32_t random_state = 0xc9a4d51bu;
    const auto next_random = [&random_state]() {
        random_state ^= random_state << 13u;
        random_state ^= random_state >> 17u;
        random_state ^= random_state << 5u;
        return random_state;
    };

    for (size_t case_index = 0; case_index < 24; ++case_index) {
        SCOPED_TRACE("fluid case " + std::to_string(case_index));
        const uint32_t batch_ticks = 2u + next_random() % 6u;
        const uint32_t total_ticks = 1u + next_random() % 31u;
        const int32_t transfer_per_tick =
            1 + static_cast<int32_t>(next_random() % 251u);
        const MachineFluidTransport transport = next_random() % 2u == 0u
            ? MachineFluidTransport::kLiquid
            : MachineFluidTransport::kGas;
        const PipeType pipe_type = transport == MachineFluidTransport::kLiquid
            ? PipeType::LIQUID
            : PipeType::GAS;
        const std::string fluid_id = transport == MachineFluidTransport::kLiquid
            ? "offline.coolant"
            : "offline.steam";
        const int64_t source_amount = 1 + static_cast<int64_t>(next_random() % 1'001u);
        const int64_t source_capacity = source_amount +
            static_cast<int64_t>(next_random() % 1'001u);
        const int64_t consumer_capacity = 1 + static_cast<int64_t>(next_random() % 1'001u);

        GameContentRegistry per_tick_content;
        register_fluid_network_content(per_tick_content, 1, transfer_per_tick);
        GameChunkSidecarRegistry per_tick_sidecars;
        const ChunkKey chunk_key = make_chunk_key();
        per_tick_sidecars.set(chunk_key, {});
        snt::ecs::World per_tick_world;
        const FluidMachinePair per_tick_machines = create_fluid_machine_pair(
            per_tick_content, per_tick_world, per_tick_sidecars, chunk_key, 0, chunk_key, 2,
            transport, fluid_id, source_amount, source_capacity, consumer_capacity);
        ASSERT_NE(per_tick_machines.source_guid, 0u);
        ASSERT_NE(per_tick_machines.consumer_guid, 0u);
        add_pipe(per_tick_sidecars, chunk_key, 9500 + case_index, 1, 0, 0,
                 static_cast<uint8_t>(snt::game::CONN_POS_X |
                                      snt::game::CONN_NEG_X),
                 pipe_type);
        OfflineIndustrialNetworkIslandProvider per_tick_provider(
            per_tick_content, per_tick_sidecars);
        OfflineIndustrialNetworkIslandSimulator per_tick_simulator;
        OfflineMachineSimulationService per_tick_service(per_tick_content, per_tick_sidecars);
        per_tick_service.set_network_island_provider(&per_tick_provider);
        per_tick_service.set_network_island_simulator(&per_tick_simulator);
        ASSERT_TRUE(per_tick_service.initialize(0));
        const std::vector<ChunkKey> candidates{chunk_key};
        ASSERT_TRUE(per_tick_service.dematerialize_chunks(per_tick_world, candidates, 0));
        for (uint32_t tick = 1; tick <= total_ticks; ++tick) {
            ASSERT_TRUE(per_tick_service.tick(tick));
        }

        GameContentRegistry batched_content;
        register_fluid_network_content(batched_content, batch_ticks, transfer_per_tick);
        GameChunkSidecarRegistry batched_sidecars;
        batched_sidecars.set(chunk_key, {});
        snt::ecs::World batched_world;
        const FluidMachinePair batched_machines = create_fluid_machine_pair(
            batched_content, batched_world, batched_sidecars, chunk_key, 0, chunk_key, 2,
            transport, fluid_id, source_amount, source_capacity, consumer_capacity);
        ASSERT_NE(batched_machines.source_guid, 0u);
        ASSERT_NE(batched_machines.consumer_guid, 0u);
        add_pipe(batched_sidecars, chunk_key, 9500 + case_index, 1, 0, 0,
                 static_cast<uint8_t>(snt::game::CONN_POS_X |
                                      snt::game::CONN_NEG_X),
                 pipe_type);
        OfflineIndustrialNetworkIslandProvider batched_provider(batched_content, batched_sidecars);
        OfflineIndustrialNetworkIslandSimulator batched_simulator;
        OfflineMachineSimulationService batched_service(batched_content, batched_sidecars);
        batched_service.set_network_island_provider(&batched_provider);
        batched_service.set_network_island_simulator(&batched_simulator);
        ASSERT_TRUE(batched_service.initialize(0));
        ASSERT_TRUE(batched_service.dematerialize_chunks(batched_world, candidates, 0));
        for (uint32_t tick = batch_ticks; tick <= total_ticks; tick += batch_ticks) {
            ASSERT_TRUE(batched_service.tick(tick));
        }
        if (total_ticks % batch_ticks != 0) {
            ASSERT_TRUE(batched_service.flush(total_ticks));
        }

        const auto* per_tick_sidecar = per_tick_sidecars.get(chunk_key);
        const auto* batched_sidecar = batched_sidecars.get(chunk_key);
        ASSERT_NE(per_tick_sidecar, nullptr);
        ASSERT_NE(batched_sidecar, nullptr);
        const MachineRuntimePersistenceRecord* per_tick_source =
            find_machine_record(*per_tick_sidecar, per_tick_machines.source_guid);
        const MachineRuntimePersistenceRecord* per_tick_consumer =
            find_machine_record(*per_tick_sidecar, per_tick_machines.consumer_guid);
        const MachineRuntimePersistenceRecord* batched_source =
            find_machine_record(*batched_sidecar, batched_machines.source_guid);
        const MachineRuntimePersistenceRecord* batched_consumer =
            find_machine_record(*batched_sidecar, batched_machines.consumer_guid);
        ASSERT_NE(per_tick_source, nullptr);
        ASSERT_NE(per_tick_consumer, nullptr);
        ASSERT_NE(batched_source, nullptr);
        ASSERT_NE(batched_consumer, nullptr);
        EXPECT_EQ(normalized_machine_payload(*per_tick_source),
                  normalized_machine_payload(*batched_source));
        EXPECT_EQ(normalized_machine_payload(*per_tick_consumer),
                  normalized_machine_payload(*batched_consumer));
        ASSERT_EQ(per_tick_sidecar->offline_network_islands.size(), 1u);
        ASSERT_EQ(batched_sidecar->offline_network_islands.size(), 1u);
        const OfflineNetworkIslandSnapshot& per_tick_snapshot =
            per_tick_sidecar->offline_network_islands.front();
        const OfflineNetworkIslandSnapshot& batched_snapshot =
            batched_sidecar->offline_network_islands.front();
        ASSERT_EQ(per_tick_snapshot.transport_segments.size(), 1u);
        ASSERT_EQ(batched_snapshot.transport_segments.size(), 1u);
        EXPECT_EQ(per_tick_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(batched_snapshot.last_simulated_tick, total_ticks);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().kind,
                  OfflineNetworkResourceKind::kFluid);
        EXPECT_EQ(batched_snapshot.transport_segments.front().kind,
                  OfflineNetworkResourceKind::kFluid);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().fluid_transport,
                  batched_snapshot.transport_segments.front().fluid_transport);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().capacity,
                  batched_snapshot.transport_segments.front().capacity);
        EXPECT_EQ(per_tick_snapshot.transport_segments.front().max_transfer_per_tick,
                  batched_snapshot.transport_segments.front().max_transfer_per_tick);
        EXPECT_TRUE(per_tick_snapshot.ledgers.empty());
        EXPECT_TRUE(batched_snapshot.ledgers.empty());
    }
}

}  // namespace
