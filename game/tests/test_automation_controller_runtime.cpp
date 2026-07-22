#include "game/simulation/automation_controller_runtime.h"

#include "game/network/game_automation_controller_replication.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/server/game_server_automation_controller_replication.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace snt::game {
namespace {

class AutomationControllerRuntimeFixture : public testing::Test {
protected:
    ResourceRuntimeIndex resource_index;
    ResourceRuntimeIndex::Snapshot resource_snapshot;
    ResourceKey iron;
    ResourceLedgerStorage source{ResourceKeyContext{}};
    ResourceLedgerStorage destination{ResourceKeyContext{}};
    std::unique_ptr<AutomationControllerRuntimeService> runtime;

    void SetUp() override {
        const std::vector<ResourceContentKey> contents{
            ResourceContentKey::item("iron.ingot"),
        };
        ASSERT_TRUE(resource_index.rebuild(contents));
        resource_snapshot = resource_index.snapshot();
        iron = *resource_snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
        source = ResourceLedgerStorage(resource_snapshot.key_context());
        destination = ResourceLedgerStorage(resource_snapshot.key_context());
        runtime = std::make_unique<AutomationControllerRuntimeService>(resource_snapshot);
        ASSERT_EQ(source.insert(resource_snapshot.key_context(), {.key = iron, .amount = 12},
                                ResourceTransferMode::kExecute),
                  12);
    }
};

[[nodiscard]] GameChunkSidecar make_controller_sidecar(EntityId anchor_id,
                                                        bool with_program = true) {
    GameChunkSidecar sidecar;
    sidecar.block_entities.push_back({
        .id = anchor_id,
        .entity_type = BlockEntityType::AUTOMATION_CONTROLLER,
        .root_x = 1,
        .root_y = 2,
        .root_z = 3,
        .owned_cell_count = 1,
    });
    AutomationControllerPersistenceRecord record{
        .anchor_entity_id = anchor_id,
        .kind = AutomationControllerKind::kSfmManager,
        .controller_key = "automation.sfm_manager",
        .revision = 1,
    };
    if (with_program) {
        record.sfm_program = {
            .revision = 9,
            .nodes = {
                {.id = 1, .type = SfmFlowNodeType::kInterval, .interval_ticks = 10},
                {
                    .id = 2,
                    .type = SfmFlowNodeType::kTransfer,
                    .transfer = {
                        .source = {.value = "controller.test.source"},
                        .destination = {.value = "controller.test.destination"},
                        .requested = ResourceContentStack::item("iron.ingot", 4),
                    },
                },
            },
            .connections = {{.source = 1, .destination = 2}},
        };
    }
    sidecar.automation_controller_records.push_back(std::move(record));
    return sidecar;
}

TEST_F(AutomationControllerRuntimeFixture, MaterializesAndExecutesCompactFlowByAnchor) {
    const auto source_handle = runtime->register_sfm_endpoint(
        {.value = "controller.test.source"}, source);
    const auto destination_handle = runtime->register_sfm_endpoint(
        {.value = "controller.test.destination"}, destination);
    ASSERT_TRUE(source_handle);
    ASSERT_TRUE(destination_handle);

    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const EntityId anchor{0x2000000000000001ull};
    ASSERT_TRUE(runtime->materialize_chunk(chunk, make_controller_sidecar(anchor)));
    EXPECT_EQ(runtime->active_controller_count(), 1u);
    const AutomationControllerRuntimePresentation* controller =
        runtime->find_controller(anchor);
    ASSERT_NE(controller, nullptr);
    EXPECT_TRUE(controller->online);

    const auto first_tick = runtime->fixed_tick(1);
    ASSERT_TRUE(first_tick);
    EXPECT_EQ(first_tick->executed_transfers, 1u);
    EXPECT_EQ(source.amount_of(resource_snapshot.key_context(), iron), 8);
    EXPECT_EQ(destination.amount_of(resource_snapshot.key_context(), iron), 4);

    ASSERT_TRUE(runtime->unregister_sfm_endpoint(*destination_handle));
    ASSERT_TRUE(runtime->fixed_tick(2));
    controller = runtime->find_controller(anchor);
    ASSERT_NE(controller, nullptr);
    EXPECT_FALSE(controller->online);

    runtime->dematerialize_chunk(chunk);
    EXPECT_EQ(runtime->active_controller_count(), 0u);
}

TEST_F(AutomationControllerRuntimeFixture, EmptyProgramIsOnlineWithoutATickExecutor) {
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    const EntityId anchor{0x2000000000000002ull};
    ASSERT_TRUE(runtime->materialize_chunk(chunk, make_controller_sidecar(anchor, false)));
    const AutomationControllerRuntimePresentation* controller =
        runtime->find_controller(anchor);
    ASSERT_NE(controller, nullptr);
    EXPECT_TRUE(controller->online);
    const auto tick = runtime->fixed_tick(1);
    ASSERT_TRUE(tick);
    EXPECT_EQ(tick->dispatched_nodes, 0u);
    EXPECT_EQ(tick->executed_transfers, 0u);
}

TEST_F(AutomationControllerRuntimeFixture, WithholdsControllerGraphsThatExceedPeerValueBudget) {
    const auto source_handle = runtime->register_sfm_endpoint(
        {.value = "controller.test.source"}, source);
    const auto destination_handle = runtime->register_sfm_endpoint(
        {.value = "controller.test.destination"}, destination);
    ASSERT_TRUE(source_handle);
    ASSERT_TRUE(destination_handle);
    const ChunkKey chunk{"snt:overworld", 0, 0, 0};
    ASSERT_TRUE(runtime->materialize_chunk(
        chunk, make_controller_sidecar(EntityId{0x2000000000000003ull})));

    auto replication = replication::GameServerAutomationControllerReplication::create(*runtime);
    ASSERT_TRUE(replication);
    const replication::GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = static_cast<uint32_t>(
            replication::kGameAutomationControllerReplicationHeaderBytes),
        .max_value_snapshots_per_tick = 1,
    };
    const replication::GameAuthenticatedPeer peer{.peer = 42};
    const replication::GameReplicationInterest interest{.chunks = {chunk}};
    auto values = (*replication)->collect_values(
        peer, interest, budget, {.tick_index = 1},
        replication::GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(values);
    ASSERT_EQ(values->size(), 1u);
    EXPECT_LE(values->front().payload.size(),
              replication::kGameAutomationControllerReplicationHeaderBytes);
    auto decoded = replication::decode_game_automation_controller_replication_snapshot(
        values->front().payload);
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(decoded->controllers.empty());
}

}  // namespace
}  // namespace snt::game
