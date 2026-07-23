#include "game/simulation/ae_network_persistence.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/simulation/automation_controller_persistence.h"
#include "game/automation/ae_storage_cell.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"
#include "game/world/save/chunk_serializer.h"

#include <gtest/gtest.h>

#include <vector>

namespace snt::game {
namespace {

constexpr EntityId kAeControllerAnchor{0x2000000000000011ull};
constexpr EntityId kAeCableAnchor{0x1000000000000011ull};
constexpr EntityId kAeDriveAnchor{0x1000000000000012ull};

[[nodiscard]] ResourceRuntimeIndex::Snapshot make_resource_snapshot(
    std::vector<ResourceContentKey> keys = {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::item("copper.ingot"),
    }) {
    ResourceRuntimeIndex index;
    EXPECT_TRUE(index.rebuild(keys));
    return index.snapshot();
}

GameChunkSidecar make_connected_sidecar() {
    GameChunkSidecar sidecar;
    sidecar.block_entities = {
        {
            .id = kAeControllerAnchor,
            .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
            .root_x = 0,
            .root_y = 0,
            .root_z = 0,
            .owned_cell_count = 1,
        },
        {
            .id = kAeCableAnchor,
            .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
            .root_x = 1,
            .root_y = 0,
            .root_z = 0,
            .owned_cell_count = 1,
        },
    };
    sidecar.automation_controller_records.push_back({
        .anchor_entity_id = kAeControllerAnchor,
        .kind = AutomationControllerKind::kAeController,
        .controller_key = std::string(kAeControllerKey),
        .revision = 1,
    });
    sidecar.ae_network_node_records = {
        {
            .anchor_entity_id = kAeControllerAnchor,
            .node_key = std::string(kAeControllerKey),
            .type = AeNetworkNodeType::kController,
            .connection_mask = CONN_POS_X,
            .revision = 1,
        },
        {
            .anchor_entity_id = kAeCableAnchor,
            .node_key = "automation.ae_cable",
            .type = AeNetworkNodeType::kCable,
            .connection_mask = CONN_NEG_X,
            .revision = 1,
        },
    };
    return sidecar;
}

GameChunkSidecar make_storage_sidecar(
    AeNetworkNodeType storage_type = AeNetworkNodeType::kDrive) {
    GameChunkSidecar sidecar = make_connected_sidecar();
    sidecar.block_entities.push_back({
        .id = kAeDriveAnchor,
        .entity_type = BlockEntityType::AUTOMATION_NETWORK_NODE,
        .root_x = 2,
        .root_y = 0,
        .root_z = 0,
        .owned_cell_count = 1,
    });
    sidecar.ae_network_node_records[1].connection_mask = CONN_NEG_X | CONN_POS_X;
    sidecar.ae_network_node_records.push_back({
        .anchor_entity_id = kAeDriveAnchor,
        .node_key = storage_type == AeNetworkNodeType::kDrive
            ? "automation.ae_drive.1k"
            : "automation.ae_storage_bus",
        .type = storage_type,
        .connection_mask = CONN_NEG_X,
        .revision = 1,
    });
    return sidecar;
}

TEST(AeNetworkRuntimeTest, MaterializesReciprocalAdjacentNodesWithHashLookups) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(chunk, make_connected_sidecar()));
    EXPECT_EQ(runtime.active_node_count(), 2u);
    EXPECT_GT(runtime.topology_revision(), 0u);

    const AeNetworkRuntimeNodePresentation* controller = runtime.find_node(kAeControllerAnchor);
    const AeNetworkRuntimeNodePresentation* cable = runtime.find_node(kAeCableAnchor);
    ASSERT_NE(controller, nullptr);
    ASSERT_NE(cable, nullptr);
    EXPECT_TRUE(controller->online);
    EXPECT_TRUE(cable->online);
    EXPECT_NE(controller->component_id, 0u);
    EXPECT_EQ(controller->component_id, cable->component_id);
    EXPECT_EQ(runtime.find_node_at("snt:overworld", 1, 0, 0), cable);

    ASSERT_TRUE(runtime.dematerialize_chunk(chunk));
    EXPECT_EQ(runtime.active_node_count(), 0u);
    EXPECT_EQ(runtime.find_node(kAeControllerAnchor), nullptr);
}

TEST(AeNetworkRuntimeTest, FailsClosedForTwoControllersInOnePhysicalComponent) {
    GameChunkSidecar sidecar = make_connected_sidecar();
    const EntityId second_controller{0x2000000000000012ull};
    sidecar.block_entities.push_back({
        .id = second_controller,
        .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
        .root_x = 2,
        .root_y = 0,
        .root_z = 0,
        .owned_cell_count = 1,
    });
    sidecar.automation_controller_records.push_back({
        .anchor_entity_id = second_controller,
        .kind = AutomationControllerKind::kAeController,
        .controller_key = std::string(kAeControllerKey),
        .revision = 1,
    });
    sidecar.ae_network_node_records.front().connection_mask = CONN_POS_X;
    sidecar.ae_network_node_records[1].connection_mask = CONN_NEG_X | CONN_POS_X;
    sidecar.ae_network_node_records.push_back({
        .anchor_entity_id = second_controller,
        .node_key = std::string(kAeControllerKey),
        .type = AeNetworkNodeType::kController,
        .connection_mask = CONN_NEG_X,
        .revision = 1,
    });

    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk({"snt:overworld", 0, 0, 0}, sidecar));
    const AeNetworkRuntimeNodePresentation* first = runtime.find_node(kAeControllerAnchor);
    const AeNetworkRuntimeNodePresentation* second = runtime.find_node(second_controller);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(first->online);
    EXPECT_FALSE(second->online);
}

TEST(AeNetworkRuntimeStorageTest,
     AggregatesDriveCellsByPhysicalComponentAndRebuildsOnlyAtBoundaries) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const ResourceRuntimeIndex::Snapshot snapshot = make_resource_snapshot();
    const ResourceKey iron = *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    auto created = AeStorageCell::create({
        .byte_capacity = 64,
        .max_distinct_resources = 4,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 1,
    }, snapshot);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(snapshot.key_context(), {.key = iron, .amount = 10},
                          ResourceTransferMode::kExecute),
              10);

    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(chunk, make_storage_sidecar()));
    EXPECT_FALSE(runtime.attach_storage(kAeCableAnchor, cell));
    const auto attachment = runtime.attach_storage(kAeDriveAnchor, cell);
    ASSERT_TRUE(attachment);
    const auto component = runtime.storage_component_of(*attachment);
    ASSERT_TRUE(component);
    EXPECT_EQ(runtime.storage_count_in_component(*component), 1u);
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 10);
    EXPECT_EQ(runtime.amount_in_component(*component, snapshot.key_context(), iron), 10);

    ASSERT_EQ(cell.insert(snapshot.key_context(), {.key = iron, .amount = 6},
                          ResourceTransferMode::kExecute),
              6);
    EXPECT_EQ(runtime.amount_at_node(kAeControllerAnchor, snapshot.key_context(), iron), 16);

    ASSERT_TRUE(runtime.dematerialize_chunk(chunk));
    EXPECT_FALSE(runtime.storage_component_of(*attachment));
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 0);
    ASSERT_EQ(cell.insert(snapshot.key_context(), {.key = iron, .amount = 4},
                          ResourceTransferMode::kExecute),
              4);

    ASSERT_TRUE(runtime.materialize_chunk(chunk, make_storage_sidecar()));
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 20);
    EXPECT_TRUE(runtime.detach_storage(*attachment));
    EXPECT_FALSE(cell.has_resource_aggregate_observer());
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 0);
}

TEST(AeNetworkRuntimeStorageTest,
     AggregatesStorageBusLedgerDeltasWithoutRescanningExternalStorage) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const ResourceRuntimeIndex::Snapshot snapshot = make_resource_snapshot();
    const ResourceKey iron = *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ResourceLedgerStorage ledger{snapshot.key_context()};
    ASSERT_EQ(ledger.insert(snapshot.key_context(), {.key = iron, .amount = 12},
                            ResourceTransferMode::kExecute),
              12);

    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(
        chunk, make_storage_sidecar(AeNetworkNodeType::kStorageBus)));
    const auto attachment = runtime.attach_storage(kAeDriveAnchor, ledger);
    ASSERT_TRUE(attachment);
    EXPECT_EQ(runtime.amount_at_node(kAeControllerAnchor, snapshot.key_context(), iron), 12);

    ASSERT_EQ(ledger.extract(snapshot.key_context(), {.key = iron, .amount = 5},
                             ResourceTransferMode::kExecute),
              5);
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 7);
    EXPECT_TRUE(runtime.detach_storage(*attachment));
    EXPECT_FALSE(ledger.has_resource_aggregate_observer());
}

TEST(AeNetworkComponentStorageTest,
     ReadsTheComponentAggregateAndRoutesTransfersToIndexedPhysicalOwners) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const ResourceRuntimeIndex::Snapshot snapshot = make_resource_snapshot();
    const ResourceKey iron = *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey copper = *snapshot.resolve_runtime(ResourceContentKey::item("copper.ingot"));
    ResourceLedgerStorage ledger{snapshot.key_context()};
    ASSERT_EQ(ledger.insert(snapshot.key_context(), {.key = iron, .amount = 7},
                            ResourceTransferMode::kExecute),
              7);

    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(chunk, make_storage_sidecar()));
    ASSERT_TRUE(runtime.attach_storage(kAeDriveAnchor, ledger));
    AeNetworkComponentStorage component_storage{runtime, kAeControllerAnchor};
    EXPECT_TRUE(component_storage.key_context().matches(snapshot.key_context()));
    EXPECT_EQ(component_storage.amount_of(snapshot.key_context(), iron), 7);
    EXPECT_EQ(component_storage.extract(snapshot.key_context(), {.key = iron, .amount = 3},
                                        ResourceTransferMode::kSimulate),
              3);
    EXPECT_EQ(component_storage.extract(snapshot.key_context(), {.key = iron, .amount = 3},
                                        ResourceTransferMode::kExecute),
              3);
    EXPECT_EQ(ledger.amount_of(snapshot.key_context(), iron), 4);
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, snapshot.key_context(), iron), 4);

    // The direct endpoint mutation updates both the aggregate and its
    // ResourceKey-to-owner route. Once empty, extraction finds no owner;
    // inserting a new key allocates an endpoint only at that capacity boundary.
    ASSERT_EQ(ledger.extract(snapshot.key_context(), {.key = iron, .amount = 4},
                             ResourceTransferMode::kExecute),
              4);
    EXPECT_EQ(component_storage.extract(snapshot.key_context(), {.key = iron, .amount = 1},
                                        ResourceTransferMode::kSimulate),
              0);
    EXPECT_EQ(component_storage.insert(snapshot.key_context(), {.key = copper, .amount = 5},
                                       ResourceTransferMode::kExecute),
              5);
    EXPECT_EQ(runtime.amount_at_node(kAeControllerAnchor, snapshot.key_context(), copper), 5);
    EXPECT_EQ(component_storage.extract(snapshot.key_context(), {.key = copper, .amount = 2},
                                        ResourceTransferMode::kExecute),
              2);
    EXPECT_EQ(runtime.amount_at_node(kAeControllerAnchor, snapshot.key_context(), copper), 3);
    EXPECT_EQ(component_storage.stored_keys(snapshot.key_context()),
              (std::vector<ResourceKey>{copper}));
}

TEST(AeNetworkRuntimeStorageTest, RebindsAggregatesOnlyAfterCellsPublishNewSnapshot) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const ResourceRuntimeIndex::Snapshot first = make_resource_snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    auto created = AeStorageCell::create({
        .byte_capacity = 64,
        .max_distinct_resources = 4,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 1,
    }, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {.key = first_iron, .amount = 7},
                          ResourceTransferMode::kExecute),
              7);

    AeNetworkRuntimeService runtime;
    ASSERT_TRUE(runtime.materialize_chunk(chunk, make_storage_sidecar()));
    ASSERT_TRUE(runtime.attach_storage(kAeDriveAnchor, cell));
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, first.key_context(), first_iron), 7);

    runtime.detach_storage_aggregates_for_resource_reload();
    EXPECT_FALSE(cell.has_resource_aggregate_observer());
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, first.key_context(), first_iron), 0);

    const ResourceRuntimeIndex::Snapshot second = make_resource_snapshot({
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::item("iron.ingot"),
    });
    ASSERT_TRUE(cell.rebind(first, second));
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ASSERT_TRUE(runtime.rebuild_storage_aggregates());
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, first.key_context(), first_iron), 0);
    EXPECT_EQ(runtime.amount_at_node(kAeDriveAnchor, second.key_context(), second_iron), 7);
}

TEST(AeNetworkPersistenceTest, CreatesControllerNodeAtomicallyAndRoundTripsV26Sidecar) {
    const ChunkKey chunk_key{"snt:overworld", 0, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, {});
    auto controller = GameAutomationControllerPersistence::create_controller(
        sidecars, chunk_key, 0, 0, 0,
        {
            .kind = AutomationControllerKind::kAeController,
            .controller_key = std::string(kAeControllerKey),
        });
    ASSERT_TRUE(controller);
    ASSERT_TRUE(GameAutomationControllerPersistence::validate_all(sidecars));
    ASSERT_TRUE(GameAeNetworkPersistence::validate_all(sidecars));

    const GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->ae_network_node_records.size(), 1u);
    EXPECT_EQ(sidecar->ae_network_node_records.front().anchor_entity_id,
              controller->anchor_entity_id);
    EXPECT_EQ(sidecar->ae_network_node_records.front().type,
              AeNetworkNodeType::kController);

    GameChunk source;
    source.chunk_x = 0;
    source.chunk_y = 0;
    source.chunk_z = 0;
    source.terrain.resize(1, 1, 1);
    source.sidecar() = *sidecar;
    const GameChunkSerializer serializer;
    const std::vector<uint8_t> bytes = serializer.serialize("snt:overworld", source);
    GameChunk restored;
    std::string dimension_id;
    ASSERT_TRUE(serializer.deserialize(bytes, dimension_id, restored));
    EXPECT_EQ(dimension_id, "snt:overworld");
    ASSERT_EQ(restored.ae_network_node_records.size(), 1u);
    EXPECT_EQ(restored.ae_network_node_records.front().type,
              AeNetworkNodeType::kController);
    EXPECT_EQ(restored.ae_network_node_records.front().connection_mask, CONN_ALL);

    ASSERT_TRUE(GameAutomationControllerPersistence::remove_controller(
        sidecars, controller->anchor_entity_id));
    sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    EXPECT_TRUE(sidecar->automation_controller_records.empty());
    EXPECT_TRUE(sidecar->ae_network_node_records.empty());
}

TEST(AeNetworkPersistenceTest, CreatesAndRemovesNonControllerTypedNode) {
    const ChunkKey chunk_key{"snt:overworld", 0, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk_key, {});
    auto node = GameAeNetworkPersistence::create_node(
        sidecars, chunk_key, 1, 0, 0,
        {
            .node_key = "automation.ae_cable",
            .type = AeNetworkNodeType::kCable,
            .connection_mask = CONN_NEG_X | CONN_POS_X,
        });
    ASSERT_TRUE(node);
    ASSERT_TRUE(GameAeNetworkPersistence::validate_all(sidecars));
    const GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    ASSERT_EQ(sidecar->block_entities.size(), 1u);
    EXPECT_EQ(sidecar->block_entities.front().entity_type,
              BlockEntityType::AUTOMATION_NETWORK_NODE);

    ASSERT_TRUE(GameAeNetworkPersistence::remove_node(sidecars, node->anchor_entity_id));
    sidecar = sidecars.get(chunk_key);
    ASSERT_NE(sidecar, nullptr);
    EXPECT_TRUE(sidecar->block_entities.empty());
    EXPECT_TRUE(sidecar->ae_network_node_records.empty());
}

}  // namespace
}  // namespace snt::game
