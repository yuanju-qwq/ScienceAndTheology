// AE autocrafting service coverage.

#include "game/automation/ae_autocrafting.h"
#include "game/automation/ae_storage_cell.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"
#include "game/simulation/ae_machine_pattern_provider_persistence.h"
#include "game/simulation/ae_machine_pattern_provider_runtime.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/simulation/automation_controller_persistence.h"
#include "game/world/save/chunk_serializer.h"
#include "ecs/world.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] ResourceRuntimeIndex::Snapshot make_snapshot(
    std::vector<ResourceContentKey> keys = {
        ResourceContentKey::item("ore.iron"),
        ResourceContentKey::item("ingot.iron"),
        ResourceContentKey::item("plate.iron"),
        ResourceContentKey::item("slag"),
    }) {
    ResourceRuntimeIndex index;
    EXPECT_TRUE(index.rebuild(keys));
    return index.snapshot();
}

[[nodiscard]] GameChunkSidecar make_autocrafting_component_sidecar(
    EntityId controller_anchor, EntityId drive_anchor) {
    GameChunkSidecar sidecar;
    sidecar.block_entities = {
        {
            .id = controller_anchor,
            .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
            .root_x = 0,
            .root_y = 0,
            .root_z = 0,
            .owned_cell_count = 1,
        },
        {
            .id = drive_anchor,
            .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
            .root_x = 1,
            .root_y = 0,
            .root_z = 0,
            .owned_cell_count = 1,
        },
    };
    sidecar.automation_controller_records.push_back({
        .anchor_entity_id = controller_anchor,
        .kind = AutomationControllerKind::kAeController,
        .controller_key = std::string(kAeControllerKey),
        .revision = 1,
    });
    sidecar.ae_network_node_records = {
        {
            .anchor_entity_id = controller_anchor,
            .node_key = std::string(kAeControllerKey),
            .type = AeNetworkNodeType::kController,
            .connection_mask = CONN_POS_X,
            .revision = 1,
        },
        {
            .anchor_entity_id = drive_anchor,
            .node_key = "automation.ae_drive.1k",
            .type = AeNetworkNodeType::kDrive,
            .connection_mask = CONN_NEG_X,
            .revision = 1,
        },
    };
    return sidecar;
}

[[nodiscard]] GameChunkSidecar make_machine_provider_sidecar(
    EntityId controller_anchor,
    EntityId interface_anchor,
    EntityId drive_anchor,
    EntityId machine_anchor,
    uint64_t machine_guid,
    int32_t root_x_offset = 0) {
    GameChunkSidecar sidecar;
    sidecar.block_entities = {
        {.id = controller_anchor,
         .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
         .root_x = root_x_offset,
         .root_y = 0,
         .root_z = 0,
         .owned_cell_count = 1},
        {.id = interface_anchor,
         .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
         .root_x = root_x_offset + 1,
         .root_y = 0,
         .root_z = 0,
         .owned_cell_count = 1},
        {.id = drive_anchor,
         .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
         .root_x = root_x_offset + 2,
         .root_y = 0,
         .root_z = 0,
         .owned_cell_count = 1},
        {.id = machine_anchor,
         .entity_type = BlockEntityType::MACHINE,
         .root_x = root_x_offset + 3,
         .root_y = 0,
         .root_z = 0,
         .owned_cell_count = 1},
    };
    sidecar.automation_controller_records.push_back({
        .anchor_entity_id = controller_anchor,
        .kind = AutomationControllerKind::kAeController,
        .controller_key = std::string(kAeControllerKey),
        .revision = 1,
    });
    sidecar.ae_network_node_records = {
        {.anchor_entity_id = controller_anchor,
         .node_key = std::string(kAeControllerKey),
         .type = AeNetworkNodeType::kController,
         .connection_mask = CONN_POS_X,
         .revision = 1},
        {.anchor_entity_id = interface_anchor,
         .node_key = "automation.ae_interface",
         .type = AeNetworkNodeType::kInterface,
         .connection_mask = CONN_NEG_X | CONN_POS_X,
         .revision = 1},
        {.anchor_entity_id = drive_anchor,
         .node_key = "automation.ae_drive.1k",
         .type = AeNetworkNodeType::kDrive,
         .connection_mask = CONN_NEG_X,
         .revision = 1},
    };
    sidecar.machine_runtime_records.push_back({
        .anchor_entity_id = machine_anchor,
        .entity_guid = machine_guid,
        .machine_id = "furnace",
    });
    sidecar.ae_machine_pattern_provider_records.push_back({
        .interface_anchor_entity_id = interface_anchor,
        .machine_anchor_entity_id = machine_anchor,
        .enabled = true,
        .priority = 7,
        .next_job_serial = 1,
        .revision = 1,
    });
    return sidecar;
}

void register_machine_provider_content(GameContentRegistry& content,
                                       bool include_slag = false) {
    ASSERT_TRUE(content.register_builtin_item({
        .id = "ore.iron",
        .title_key = "item.ore_iron",
        .max_stack = 64,
    }));
    ASSERT_TRUE(content.register_builtin_item({
        .id = "ingot.iron",
        .title_key = "item.ingot_iron",
        .max_stack = 64,
    }));
    if (include_slag) {
        ASSERT_TRUE(content.register_builtin_item({
            .id = "slag",
            .title_key = "item.slag",
            .max_stack = 64,
        }));
    }
    ASSERT_TRUE(content.register_builtin_machine({
        .id = "furnace",
        .display_name = "Furnace",
        .tier = 1,
    }));
    RecipeDefinition recipe{
        .id = "snt.furnace.iron",
        .machine_id = "furnace",
        .inputs = {{.item_id = "ore.iron", .count = 1}},
        .outputs = {{.item_id = "ingot.iron", .count = 1}},
        .duration_ticks = 2,
        .energy_per_tick = 3,
    };
    if (include_slag) recipe.outputs.push_back({.item_id = "slag", .count = 1});
    ASSERT_TRUE(content.register_builtin_recipe(std::move(recipe)));
}

TEST(AeAutocraftingServiceTest, PlansRecursivelyAndExecutesThroughTheRealStorageOwner) {
    const ResourceRuntimeIndex::Snapshot snapshot = make_snapshot();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));
    const ResourceKey plate = *snapshot.resolve_runtime(ResourceContentKey::item("plate.iron"));

    ResourceLedgerStorage ledger{snapshot.key_context()};
    ASSERT_EQ(ledger.insert(snapshot.key_context(), {.key = ore, .amount = 4},
                            ResourceTransferMode::kExecute),
              4);
    AeAutocraftingStorageAccess access{ledger};
    AeAutocraftingService service{snapshot};
    ASSERT_TRUE(service.replace_patterns({
        {
            .id = "snt.smelting.iron",
            .inputs = {ResourceContentStack::item("ore.iron", 1)},
            .outputs = {ResourceContentStack::item("ingot.iron", 1)},
        },
        {
            .id = "snt.pressing.iron_plate",
            .inputs = {ResourceContentStack::item("ingot.iron", 2)},
            .outputs = {ResourceContentStack::item("plate.iron", 1)},
        },
    }));

    const auto planned = service.plan(access, ResourceContentStack::item("plate.iron", 2));
    ASSERT_TRUE(planned);
    ASSERT_EQ(planned->steps.size(), 2u);
    EXPECT_EQ(planned->steps[0].pattern_id, "snt.smelting.iron");
    EXPECT_EQ(planned->steps[0].operations, 4u);
    EXPECT_EQ(planned->steps[1].pattern_id, "snt.pressing.iron_plate");
    EXPECT_EQ(planned->steps[1].operations, 2u);

    const auto job = service.submit_job(access, ResourceContentStack::item("plate.iron", 2));
    ASSERT_TRUE(job);
    AeAutocraftingJobSnapshot result;
    for (int iteration = 0; iteration < 6; ++iteration) {
        auto ticked = service.tick(*job, access, 1);
        ASSERT_TRUE(ticked);
        result = *ticked;
    }
    EXPECT_EQ(result.state, AeAutocraftingJobState::kCompleted);
    EXPECT_EQ(result.completed_operations, 6u);
    EXPECT_EQ(ledger.amount_of(snapshot.key_context(), ore), 0);
    EXPECT_EQ(ledger.amount_of(snapshot.key_context(), ingot), 0);
    EXPECT_EQ(ledger.amount_of(snapshot.key_context(), plate), 2);
}

TEST(AeAutocraftingServiceTest, RestoresInputsWhenOneOutputCannotFit) {
    const ResourceRuntimeIndex::Snapshot snapshot = make_snapshot();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));
    const ResourceKey slag = *snapshot.resolve_runtime(ResourceContentKey::item("slag"));

    auto created = AeStorageCell::create({
        .byte_capacity = 32,
        .max_distinct_resources = 1,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 1,
    }, snapshot);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                          ResourceTransferMode::kExecute),
              1);
    AeAutocraftingStorageAccess access{cell};
    AeAutocraftingService service{snapshot};
    ASSERT_TRUE(service.replace_patterns({{
        .id = "snt.smelting.with_slag",
        .inputs = {ResourceContentStack::item("ore.iron", 1)},
        .outputs = {
            ResourceContentStack::item("ingot.iron", 1),
            ResourceContentStack::item("slag", 1),
        },
    }}));

    const auto job = service.submit_job(access, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);
    const auto result = service.tick(*job, access, 1);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->state, AeAutocraftingJobState::kBlockedOutputCapacity);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), ore), 1);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), ingot), 0);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), slag), 0);
}

TEST(AeAutocraftingServiceTest,
     ExecutesAgainstTheActiveAeComponentOwnerAndUpdatesItsAggregate) {
    const ResourceRuntimeIndex::Snapshot snapshot = make_snapshot();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));
    constexpr EntityId controller_anchor{0x2000000000000041ull};
    constexpr EntityId drive_anchor{0x1000000000000041ull};
    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(
        {"snt:overworld", 0, 0, 0},
        make_autocrafting_component_sidecar(controller_anchor, drive_anchor)));
    ResourceLedgerStorage drive_owner{snapshot.key_context()};
    ASSERT_EQ(drive_owner.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                                  ResourceTransferMode::kExecute),
              1);
    ASSERT_TRUE(runtime.attach_storage(drive_anchor, drive_owner));

    AeNetworkComponentStorage component_owner{runtime, controller_anchor};
    AeAutocraftingStorageAccess access{component_owner};
    AeAutocraftingService service{snapshot};
    ASSERT_TRUE(service.replace_patterns({{
        .id = "snt.smelting.iron",
        .inputs = {ResourceContentStack::item("ore.iron", 1)},
        .outputs = {ResourceContentStack::item("ingot.iron", 1)},
    }}));

    const auto job = service.submit_job(access, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);
    const auto result = service.tick(*job, access, 1);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->state, AeAutocraftingJobState::kCompleted);
    EXPECT_EQ(drive_owner.amount_of(snapshot.key_context(), ore), 0);
    EXPECT_EQ(drive_owner.amount_of(snapshot.key_context(), ingot), 1);
    EXPECT_EQ(runtime.amount_at_node(controller_anchor, snapshot.key_context(), ingot), 1);
}

TEST(AeAutocraftingServiceTest, RecompilesPatternsAndCancelsJobsAtResourceReloadBoundary) {
    const ResourceRuntimeIndex::Snapshot first = make_snapshot({
        ResourceContentKey::item("ore.iron"),
        ResourceContentKey::item("ingot.iron"),
    });
    const ResourceKey first_ore =
        *first.resolve_runtime(ResourceContentKey::item("ore.iron"));
    ResourceLedgerStorage ledger{first.key_context()};
    ASSERT_EQ(ledger.insert(first.key_context(), {.key = first_ore, .amount = 1},
                            ResourceTransferMode::kExecute),
              1);
    AeAutocraftingStorageAccess access{ledger};
    AeAutocraftingService service{first};
    ASSERT_TRUE(service.replace_patterns({{
        .id = "snt.smelting.iron",
        .inputs = {ResourceContentStack::item("ore.iron", 1)},
        .outputs = {ResourceContentStack::item("ingot.iron", 1)},
    }}));
    const auto job = service.submit_job(access, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);

    const ResourceRuntimeIndex::Snapshot second = make_snapshot({
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::item("ore.iron"),
        ResourceContentKey::item("ingot.iron"),
    });
    ASSERT_TRUE(service.prepare_resource_runtime_snapshot(second));
    service.commit_resource_runtime_snapshot();
    const auto cancelled = service.find_job(*job);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->state, AeAutocraftingJobState::kCancelledByContentReload);

    ASSERT_TRUE(ledger.rebind(first, second));
    const auto replanned = service.plan(access, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(replanned);
    ASSERT_EQ(replanned->steps.size(), 1u);
    EXPECT_EQ(replanned->steps.front().pattern_id, "snt.smelting.iron");
}

TEST(AeMachinePatternProviderRuntimeTest,
     QueuesDurableMachineWorkBeforeAcknowledgingNetworkDelivery) {
    constexpr EntityId controller_anchor{0x2000000000000081ull};
    constexpr EntityId interface_anchor{0x1000000000000081ull};
    constexpr EntityId drive_anchor{0x1000000000000082ull};
    constexpr EntityId machine_anchor{0x3000000000000081ull};
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};

    GameContentRegistry content;
    register_machine_provider_content(content);
    const ResourceRuntimeIndex::Snapshot snapshot = content.resource_runtime_index();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.resource_runtime_index = snapshot;
    machine.stored_energy = 4;

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk, make_machine_provider_sidecar(
        controller_anchor, interface_anchor, drive_anchor, machine_anchor, world.guid_of(entity).value));
    ASSERT_TRUE(GameAeMachinePatternProviderPersistence::validate_all(sidecars));
    ASSERT_TRUE(GameAeMachinePatternProviderPersistence::validate_content_references(sidecars, content));

    AeNetworkRuntimeService network;
    const GameChunkSidecar* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_TRUE(network.materialize_chunk(chunk, *sidecar));
    ResourceLedgerStorage drive_owner{snapshot.key_context()};
    ASSERT_EQ(drive_owner.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                                 ResourceTransferMode::kExecute),
              1);
    ASSERT_TRUE(network.attach_storage(drive_anchor, drive_owner));

    AeMachinePatternProviderRuntimeService providers{content, world, sidecars, network};
    ASSERT_TRUE(providers.materialize_chunk(chunk, *sidecar));
    ASSERT_EQ(providers.active_provider_count(), 1u);
    const auto job = providers.submit_job(
        interface_anchor, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);

    ASSERT_TRUE(providers.tick(10));
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ore), 0);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 0);
    ASSERT_TRUE(machine.automation_work_order.has_value());
    EXPECT_EQ(machine.automation_work_order->state, MachineAutomationWorkOrderState::kQueued);
    EXPECT_EQ(machine.automation_work_order->identity.provider_anchor_entity_id, interface_anchor);
    EXPECT_EQ(machine.automation_work_order->identity.provider_job_serial, 1u);

    auto first_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(first_tick);
    auto first_advanced = advance_machine_execution(std::move(*first_tick), 11, 1);
    machine = std::move(first_advanced.machine);
    EXPECT_EQ(machine.state, MachineRunState::Running);
    EXPECT_EQ(machine.progress_ticks, 1);
    EXPECT_EQ(machine.stored_energy, 1);
    ASSERT_TRUE(providers.tick(11));
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 0);
    ASSERT_TRUE(machine.automation_work_order.has_value());

    machine.stored_energy = 3;
    auto second_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(second_tick);
    auto second_advanced = advance_machine_execution(std::move(*second_tick), 12, 1);
    machine = std::move(second_advanced.machine);
    ASSERT_TRUE(machine.automation_work_order.has_value());
    EXPECT_EQ(machine.automation_work_order->state,
              MachineAutomationWorkOrderState::kOutputReady);
    ASSERT_EQ(machine.output_slots.size(), 1u);
    EXPECT_EQ(machine.output_slots.front().key, ingot);

    ASSERT_TRUE(providers.tick(12));
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 1);
    EXPECT_FALSE(machine.automation_work_order.has_value());
    EXPECT_TRUE(machine.output_slots.empty());
    const auto completed = providers.find_job(*job);
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->state, AePatternProviderJobState::kCompleted);
    sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->ae_machine_pattern_provider_records.size(), 1u);
    EXPECT_EQ(sidecar->ae_machine_pattern_provider_records.front().next_job_serial, 2u);
}

TEST(AeMachinePatternProviderRuntimeTest,
     RoutesOnlyThroughTheInterfacePhysicalComponent) {
    constexpr EntityId controller_a{0x20000000000000c1ull};
    constexpr EntityId interface_a{0x10000000000000c1ull};
    constexpr EntityId drive_a{0x10000000000000c2ull};
    constexpr EntityId machine_a_anchor{0x30000000000000c1ull};
    constexpr EntityId controller_b{0x20000000000000d1ull};
    constexpr EntityId interface_b{0x10000000000000d1ull};
    constexpr EntityId drive_b{0x10000000000000d2ull};
    constexpr EntityId machine_b_anchor{0x30000000000000d1ull};
    const ChunkKey chunk_a{"snt:overworld", 0, 0, 0};
    const ChunkKey chunk_b{"snt:overworld", 1, 0, 0};

    GameContentRegistry content;
    register_machine_provider_content(content);
    const ResourceRuntimeIndex::Snapshot snapshot = content.resource_runtime_index();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));

    snt::ecs::World world;
    const entt::entity machine_a_entity = world.create_entity();
    const entt::entity machine_b_entity = world.create_entity();
    auto& machine_a = world.add_component<MachineRuntimeComponent>(machine_a_entity);
    auto& machine_b = world.add_component<MachineRuntimeComponent>(machine_b_entity);
    machine_a.machine_id = "furnace";
    machine_a.resource_runtime_index = snapshot;
    machine_a.stored_energy = 6;
    machine_b.machine_id = "furnace";
    machine_b.resource_runtime_index = snapshot;
    machine_b.stored_energy = 6;

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_a, make_machine_provider_sidecar(
        controller_a, interface_a, drive_a, machine_a_anchor,
        world.guid_of(machine_a_entity).value));
    sidecars.set(chunk_b, make_machine_provider_sidecar(
        controller_b, interface_b, drive_b, machine_b_anchor,
        world.guid_of(machine_b_entity).value, 32));
    GameChunkSidecar* const sidecar_a = sidecars.get(chunk_a);
    GameChunkSidecar* const sidecar_b = sidecars.get(chunk_b);
    ASSERT_NE(sidecar_a, nullptr);
    ASSERT_NE(sidecar_b, nullptr);
    // If component scope were ignored, this provider would win the shared
    // recipe route. The assertion below proves it cannot leak across A/B.
    sidecar_b->ae_machine_pattern_provider_records.front().priority = 100;
    ASSERT_TRUE(GameAeMachinePatternProviderPersistence::validate_all(sidecars));

    AeNetworkRuntimeService network;
    ASSERT_TRUE(network.materialize_chunk(chunk_a, *sidecar_a));
    ASSERT_TRUE(network.materialize_chunk(chunk_b, *sidecar_b));
    ResourceLedgerStorage storage_a{snapshot.key_context()};
    ResourceLedgerStorage storage_b{snapshot.key_context()};
    ASSERT_EQ(storage_a.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                               ResourceTransferMode::kExecute),
              1);
    ASSERT_EQ(storage_b.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                               ResourceTransferMode::kExecute),
              1);
    ASSERT_TRUE(network.attach_storage(drive_a, storage_a));
    ASSERT_TRUE(network.attach_storage(drive_b, storage_b));

    AeMachinePatternProviderRuntimeService providers{content, world, sidecars, network};
    ASSERT_TRUE(providers.materialize_chunk(chunk_a, *sidecar_a));
    ASSERT_TRUE(providers.materialize_chunk(chunk_b, *sidecar_b));
    const auto planned = providers.plan(interface_a, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(planned);
    ASSERT_EQ(planned->steps.size(), 1u);
    EXPECT_EQ(planned->steps.front().provider_key,
              std::string{"ae.interface."} + std::to_string(interface_a.id));

    const auto job = providers.submit_job(
        interface_a, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);
    ASSERT_TRUE(providers.tick(40));
    ASSERT_TRUE(machine_a.automation_work_order.has_value());
    EXPECT_FALSE(machine_b.automation_work_order.has_value());
    EXPECT_EQ(network.amount_at_node(interface_a, snapshot.key_context(), ore), 0);
    EXPECT_EQ(network.amount_at_node(interface_b, snapshot.key_context(), ore), 1);

    auto first_tick = make_machine_execution_input(content, world.guid_of(machine_a_entity), machine_a);
    ASSERT_TRUE(first_tick);
    machine_a = std::move(advance_machine_execution(std::move(*first_tick), 41, 1).machine);
    ASSERT_TRUE(providers.tick(41));
    auto second_tick = make_machine_execution_input(content, world.guid_of(machine_a_entity), machine_a);
    ASSERT_TRUE(second_tick);
    machine_a = std::move(advance_machine_execution(std::move(*second_tick), 42, 1).machine);
    ASSERT_TRUE(providers.tick(42));

    EXPECT_FALSE(machine_a.automation_work_order.has_value());
    EXPECT_FALSE(machine_b.automation_work_order.has_value());
    EXPECT_EQ(network.amount_at_node(interface_a, snapshot.key_context(), ingot), 1);
    EXPECT_EQ(network.amount_at_node(interface_b, snapshot.key_context(), ingot), 0);
}

TEST(AeMachinePatternProviderRuntimeTest,
     KeepsMachineOutputWhenTheAeOwnerCannotAcceptEveryProducedResource) {
    constexpr EntityId controller_anchor{0x2000000000000091ull};
    constexpr EntityId interface_anchor{0x1000000000000091ull};
    constexpr EntityId drive_anchor{0x1000000000000092ull};
    constexpr EntityId machine_anchor{0x3000000000000091ull};
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};

    GameContentRegistry content;
    register_machine_provider_content(content, true);
    const ResourceRuntimeIndex::Snapshot snapshot = content.resource_runtime_index();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));
    const ResourceKey slag = *snapshot.resolve_runtime(ResourceContentKey::item("slag"));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.resource_runtime_index = snapshot;
    machine.stored_energy = 6;

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk, make_machine_provider_sidecar(
        controller_anchor, interface_anchor, drive_anchor, machine_anchor, world.guid_of(entity).value));
    const GameChunkSidecar* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);

    AeNetworkRuntimeService network;
    ASSERT_TRUE(network.materialize_chunk(chunk, *sidecar));
    auto storage = AeStorageCell::create({
        .byte_capacity = 64,
        .max_distinct_resources = 1,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 1,
    }, snapshot);
    ASSERT_TRUE(storage);
    AeStorageCell drive_owner = std::move(*storage);
    ASSERT_EQ(drive_owner.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                                 ResourceTransferMode::kExecute),
              1);
    ASSERT_TRUE(network.attach_storage(drive_anchor, drive_owner));

    AeMachinePatternProviderRuntimeService providers{content, world, sidecars, network};
    ASSERT_TRUE(providers.materialize_chunk(chunk, *sidecar));
    const auto job = providers.submit_job(
        interface_anchor, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);
    ASSERT_TRUE(providers.tick(20));

    auto first_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(first_tick);
    machine = std::move(advance_machine_execution(std::move(*first_tick), 21, 1).machine);
    auto second_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(second_tick);
    machine = std::move(advance_machine_execution(std::move(*second_tick), 22, 1).machine);
    ASSERT_TRUE(machine.automation_work_order.has_value());
    ASSERT_EQ(machine.automation_work_order->state,
              MachineAutomationWorkOrderState::kOutputReady);

    ASSERT_TRUE(providers.tick(22));
    const auto blocked = providers.find_job(*job);
    ASSERT_TRUE(blocked);
    EXPECT_EQ(blocked->state, AePatternProviderJobState::kBlockedOutputCapacity);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 0);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), slag), 0);
    ASSERT_TRUE(machine.automation_work_order.has_value());
    EXPECT_EQ(machine.automation_work_order->state,
              MachineAutomationWorkOrderState::kOutputReady);
    EXPECT_TRUE(machine.output_slots.size() >= 2u);
}

TEST(AeMachinePatternProviderRuntimeTest,
     UnregistersAfterAnInFlightOperationSettlesAtABindingBoundary) {
    constexpr EntityId controller_anchor{0x20000000000000b1ull};
    constexpr EntityId interface_anchor{0x10000000000000b1ull};
    constexpr EntityId drive_anchor{0x10000000000000b2ull};
    constexpr EntityId machine_anchor{0x30000000000000b1ull};
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};

    GameContentRegistry content;
    register_machine_provider_content(content);
    const ResourceRuntimeIndex::Snapshot snapshot = content.resource_runtime_index();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.resource_runtime_index = snapshot;
    machine.stored_energy = 6;

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk, make_machine_provider_sidecar(
        controller_anchor, interface_anchor, drive_anchor, machine_anchor, world.guid_of(entity).value));
    GameChunkSidecar* const mutable_sidecar = sidecars.get(chunk);
    ASSERT_NE(mutable_sidecar, nullptr);

    AeNetworkRuntimeService network;
    ASSERT_TRUE(network.materialize_chunk(chunk, *mutable_sidecar));
    ResourceLedgerStorage drive_owner{snapshot.key_context()};
    ASSERT_EQ(drive_owner.insert(snapshot.key_context(), {.key = ore, .amount = 1},
                                 ResourceTransferMode::kExecute),
              1);
    ASSERT_TRUE(network.attach_storage(drive_anchor, drive_owner));

    AeMachinePatternProviderRuntimeService providers{content, world, sidecars, network};
    ASSERT_TRUE(providers.materialize_chunk(chunk, *mutable_sidecar));
    const auto job = providers.submit_job(
        interface_anchor, ResourceContentStack::item("ingot.iron", 1));
    ASSERT_TRUE(job);
    ASSERT_TRUE(providers.tick(30));
    ASSERT_TRUE(machine.automation_work_order.has_value());

    mutable_sidecar->ae_machine_pattern_provider_records.front().enabled = false;
    ASSERT_TRUE(providers.refresh_topology());

    auto first_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(first_tick);
    machine = std::move(advance_machine_execution(std::move(*first_tick), 31, 1).machine);
    auto second_tick = make_machine_execution_input(content, world.guid_of(entity), machine);
    ASSERT_TRUE(second_tick);
    machine = std::move(advance_machine_execution(std::move(*second_tick), 32, 1).machine);
    ASSERT_TRUE(machine.automation_work_order.has_value());
    EXPECT_EQ(machine.automation_work_order->state,
              MachineAutomationWorkOrderState::kOutputReady);

    ASSERT_TRUE(providers.tick(32));
    const auto cancelled = providers.find_job(*job);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->state, AePatternProviderJobState::kCancelledByContentReload);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 0);
    EXPECT_TRUE(machine.automation_work_order.has_value());
    ASSERT_EQ(machine.output_slots.size(), 1u);
    EXPECT_EQ(machine.output_slots.front().key, ingot);

    EXPECT_FALSE(providers.submit_job(
        interface_anchor, ResourceContentStack::item("ingot.iron", 1)));
}

TEST(AeMachinePatternProviderRuntimeTest,
     RecoversPersistedOutputWithoutExtractingInputsAgain) {
    constexpr EntityId controller_anchor{0x20000000000000e1ull};
    constexpr EntityId interface_anchor{0x10000000000000e1ull};
    constexpr EntityId drive_anchor{0x10000000000000e2ull};
    constexpr EntityId machine_anchor{0x30000000000000e1ull};
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};

    GameContentRegistry content;
    register_machine_provider_content(content);
    const ResourceRuntimeIndex::Snapshot snapshot = content.resource_runtime_index();
    const ResourceKey ore = *snapshot.resolve_runtime(ResourceContentKey::item("ore.iron"));
    const ResourceKey ingot = *snapshot.resolve_runtime(ResourceContentKey::item("ingot.iron"));

    snt::ecs::World world;
    const entt::entity entity = world.create_entity();
    auto& machine = world.add_component<MachineRuntimeComponent>(entity);
    machine.machine_id = "furnace";
    machine.resource_runtime_index = snapshot;
    machine.state = MachineRunState::WaitingForOutput;
    machine.output_slots = {{.key = ingot, .amount = 1}};
    machine.automation_work_order = {
        .identity = {
            .provider_anchor_entity_id = interface_anchor,
            .provider_job_serial = 1,
        },
        .recipe_id = "snt.furnace.iron",
        .expected_outputs = {{.key = ingot, .amount = 1}},
        .state = MachineAutomationWorkOrderState::kOutputReady,
    };

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk, make_machine_provider_sidecar(
        controller_anchor, interface_anchor, drive_anchor, machine_anchor, world.guid_of(entity).value));
    GameChunkSidecar* const sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    sidecar->ae_machine_pattern_provider_records.front().next_job_serial = 2;
    auto& persisted_machine = sidecar->machine_runtime_records.front();
    persisted_machine.output_slots = {ResourceContentStack::item("ingot.iron", 1)};
    persisted_machine.automation_work_order = {
        .identity = {
            .provider_anchor_entity_id = interface_anchor,
            .provider_job_serial = 1,
        },
        .recipe_id = "snt.furnace.iron",
        .expected_outputs = {ResourceContentStack::item("ingot.iron", 1)},
        .state = MachineAutomationWorkOrderState::kOutputReady,
    };
    ASSERT_TRUE(GameAeMachinePatternProviderPersistence::validate_all(sidecars));
    ASSERT_TRUE(GameAeMachinePatternProviderPersistence::validate_content_references(
        sidecars, content));

    AeNetworkRuntimeService network;
    ASSERT_TRUE(network.materialize_chunk(chunk, *sidecar));
    ResourceLedgerStorage drive_owner{snapshot.key_context()};
    ASSERT_TRUE(network.attach_storage(drive_anchor, drive_owner));

    AeMachinePatternProviderRuntimeService providers{content, world, sidecars, network};
    ASSERT_TRUE(providers.materialize_chunk(chunk, *sidecar));
    EXPECT_EQ(providers.active_job_count(), 1u);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ore), 0);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 0);

    ASSERT_TRUE(providers.tick(50));
    EXPECT_EQ(providers.active_job_count(), 0u);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ore), 0);
    EXPECT_EQ(network.amount_at_node(interface_anchor, snapshot.key_context(), ingot), 1);
    EXPECT_FALSE(machine.automation_work_order.has_value());
    EXPECT_TRUE(machine.output_slots.empty());
}

TEST(AeMachinePatternProviderPersistenceTest, RoundTripsBindingSerialAndWorkOrderOwner) {
    constexpr EntityId controller_anchor{0x20000000000000a1ull};
    constexpr EntityId interface_anchor{0x10000000000000a1ull};
    constexpr EntityId drive_anchor{0x10000000000000a2ull};
    constexpr EntityId machine_anchor{0x30000000000000a1ull};
    const ChunkKey chunk_key{"snt:overworld", 0, 0, 0};

    GameChunk source;
    source.chunk_x = 0;
    source.chunk_y = 0;
    source.chunk_z = 0;
    source.terrain.resize(1, 1, 1);
    source.sidecar() = make_machine_provider_sidecar(
        controller_anchor, interface_anchor, drive_anchor, machine_anchor, 91);
    auto& binding = source.ae_machine_pattern_provider_records.front();
    binding.enabled = false;
    binding.priority = -12;
    binding.next_job_serial = 77;
    binding.revision = 19;
    auto& persisted_machine = source.machine_runtime_records.front();
    persisted_machine.output_slots = {ResourceContentStack::item("ingot.iron", 1)};
    persisted_machine.automation_work_order = {
        .identity = {
            .provider_anchor_entity_id = interface_anchor,
            .provider_job_serial = 76,
        },
        .recipe_id = "snt.furnace.iron",
        .expected_outputs = {ResourceContentStack::item("ingot.iron", 1)},
        .state = MachineAutomationWorkOrderState::kOutputReady,
    };

    const GameChunkSerializer serializer;
    const std::vector<uint8_t> bytes = serializer.serialize("snt:overworld", source);
    ASSERT_EQ(bytes.front(), GameChunkSerializer::kCurrentVersion);
    GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(bytes, dimension_id, restored));
    EXPECT_EQ(dimension_id, "snt:overworld");
    ASSERT_EQ(restored.ae_machine_pattern_provider_records.size(), 1u);
    const auto& restored_binding = restored.ae_machine_pattern_provider_records.front();
    EXPECT_EQ(restored_binding.interface_anchor_entity_id, interface_anchor);
    EXPECT_EQ(restored_binding.machine_anchor_entity_id, machine_anchor);
    EXPECT_FALSE(restored_binding.enabled);
    EXPECT_EQ(restored_binding.priority, -12);
    EXPECT_EQ(restored_binding.next_job_serial, 77u);
    EXPECT_EQ(restored_binding.revision, 19u);
    ASSERT_EQ(restored.machine_runtime_records.size(), 1u);
    const auto& restored_machine = restored.machine_runtime_records.front();
    ASSERT_TRUE(restored_machine.automation_work_order.has_value());
    EXPECT_EQ(restored_machine.automation_work_order->identity.provider_anchor_entity_id,
              interface_anchor);
    EXPECT_EQ(restored_machine.automation_work_order->identity.provider_job_serial, 76u);
    EXPECT_EQ(restored_machine.automation_work_order->recipe_id, "snt.furnace.iron");
    EXPECT_EQ(restored_machine.automation_work_order->expected_outputs,
              (std::vector<ResourceContentStack>{ResourceContentStack::item("ingot.iron", 1)}));
    EXPECT_EQ(restored_machine.automation_work_order->state,
              MachineAutomationWorkOrderState::kOutputReady);

    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, restored.sidecar());
    EXPECT_TRUE(GameAeMachinePatternProviderPersistence::validate_all(sidecars));
}

}  // namespace
}  // namespace snt::game
