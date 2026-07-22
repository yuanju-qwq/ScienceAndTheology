// Durable AE drive cell ownership coverage.

#include "game/automation/ae_network_types.h"
#include "game/client/game_content_registry.h"
#include "game/simulation/ae_drive_storage_runtime.h"
#include "game/simulation/ae_network_node_placement_registry.h"
#include "game/simulation/ae_network_persistence.h"
#include "game/simulation/ae_network_runtime.h"
#include "game/simulation/automation_controller_persistence.h"

#include <gtest/gtest.h>

namespace snt::game {
namespace {

TEST(AeDriveStorageRuntimeServiceTest,
     SidecarOwnsStableContentsAcrossAggregateReloadAndMaterializationBoundaries) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item({
        .id = "ae_drive_1k",
        .title_key = "ae.block.drive_1k",
        .max_stack = 64,
    }));
    ASSERT_TRUE(content.register_builtin_item({
        .id = "iron.ingot",
        .title_key = "item.iron_ingot",
        .max_stack = 64,
    }));
    ASSERT_TRUE(content.register_builtin_ae_network_node_placement({
        .item_id = "ae_drive_1k",
        .node_key = "automation.ae_drive.1k",
        .type = AeNetworkNodeType::kDrive,
        .material_key = "snt:runtime.automation.ae_drive_1k",
        .drive_storage_cell = AeDriveStorageCellDefinition{
            .byte_capacity = 1024,
            .max_distinct_resources = 63,
            .bytes_per_distinct_resource = 8,
            .units_per_byte = 1,
        },
    }));

    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    GameChunkSidecarRegistry sidecars;
    sidecars.set(chunk, {});
    const auto controller = GameAutomationControllerPersistence::create_controller(
        sidecars, chunk, 0, 0, 0,
        {
            .kind = AutomationControllerKind::kAeController,
            .controller_key = std::string(kAeControllerKey),
            .ae_node = {.connection_mask = CONN_POS_X},
        });
    ASSERT_TRUE(controller);
    const auto drive = GameAeNetworkPersistence::create_node(
        sidecars, chunk, 1, 0, 0,
        {
            .node_key = "automation.ae_drive.1k",
            .type = AeNetworkNodeType::kDrive,
            .connection_mask = CONN_NEG_X,
        });
    ASSERT_TRUE(drive);
    ASSERT_TRUE(GameAeNetworkPersistence::validate_all(sidecars));

    GameChunkSidecar* sidecar = sidecars.get(chunk);
    ASSERT_NE(sidecar, nullptr);
    AeNetworkRuntimeService first_network;
    ASSERT_TRUE(first_network.materialize_chunk(chunk, *sidecar));
    AeDriveStorageRuntimeService first_drives{first_network, content};
    ASSERT_TRUE(content.add_resource_runtime_snapshot_participant(first_drives));
    ASSERT_TRUE(first_drives.materialize_chunk(chunk, *sidecar));
    ASSERT_EQ(first_drives.active_drive_count(), 1u);
    AeStorageCell* const cell = first_drives.find_drive_cell(drive->anchor_entity_id);
    ASSERT_NE(cell, nullptr);

    const ResourceRuntimeIndex::Snapshot first_snapshot = content.resource_runtime_index();
    const ResourceKey first_iron =
        *first_snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ASSERT_EQ(cell->insert(first_snapshot.key_context(), {.key = first_iron, .amount = 5},
                           ResourceTransferMode::kExecute),
              5);
    EXPECT_EQ(first_network.amount_at_node(
                  controller->anchor_entity_id, first_snapshot.key_context(), first_iron),
              5);

    ASSERT_TRUE(first_drives.flush_chunk(chunk, *sidecar));
    ASSERT_EQ(sidecar->ae_drive_storage_records.size(), 1u);
    EXPECT_EQ(sidecar->ae_drive_storage_records.front().stored_resources,
              (std::vector<ResourceContentStack>{
                  ResourceContentStack::item("iron.ingot", 5),
              }));
    EXPECT_EQ(sidecar->ae_drive_storage_records.front().revision, 2u);

    // This key sorts before iron, forcing a different compact runtime id.
    // The service detaches/rebuilds the aggregate only at this snapshot
    // boundary; normal component amount reads remain direct hash lookups.
    ASSERT_TRUE(content.register_builtin_item({
        .id = "copper.ingot",
        .title_key = "item.copper_ingot",
        .max_stack = 64,
    }));
    const ResourceRuntimeIndex::Snapshot second_snapshot = content.resource_runtime_index();
    const ResourceKey second_iron =
        *second_snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    EXPECT_EQ(first_network.amount_at_node(
                  controller->anchor_entity_id, second_snapshot.key_context(), second_iron),
              5);
    EXPECT_EQ(cell->amount_of(second_snapshot.key_context(), second_iron), 5);

    ASSERT_TRUE(first_drives.dematerialize_chunk(chunk, *sidecar));
    ASSERT_TRUE(first_network.dematerialize_chunk(chunk));
    EXPECT_EQ(first_drives.active_drive_count(), 0u);

    AeNetworkRuntimeService restored_network;
    ASSERT_TRUE(restored_network.materialize_chunk(chunk, *sidecar));
    AeDriveStorageRuntimeService restored_drives{restored_network, content};
    ASSERT_TRUE(restored_drives.materialize_chunk(chunk, *sidecar));
    const AeStorageCell* const restored_cell =
        restored_drives.find_drive_cell(drive->anchor_entity_id);
    ASSERT_NE(restored_cell, nullptr);
    EXPECT_EQ(restored_cell->amount_of(second_snapshot.key_context(), second_iron), 5);
    EXPECT_EQ(restored_network.amount_at_node(
                  controller->anchor_entity_id, second_snapshot.key_context(), second_iron),
              5);
}

}  // namespace
}  // namespace snt::game
