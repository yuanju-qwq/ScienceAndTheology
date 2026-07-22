#include "game/automation/sfm_endpoint_registry.h"
#include "game/automation/sfm_flow_program.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"

#include <gtest/gtest.h>

#include <vector>

namespace snt::game {
namespace {

struct SfmFlowFixture {
    ResourceRuntimeIndex index;
    ResourceRuntimeIndex::Snapshot snapshot;
    ResourceKey iron;
    ResourceLedgerStorage source;
    ResourceLedgerStorage destination;
    SfmEndpointRegistry endpoints;

    SfmFlowFixture()
        : source(ResourceKeyContext{}), destination(ResourceKeyContext{}) {
        const std::vector<ResourceContentKey> keys{
            ResourceContentKey::item("iron.ingot"),
        };
        EXPECT_TRUE(index.rebuild(keys));
        snapshot = index.snapshot();
        iron = *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
        source = ResourceLedgerStorage(snapshot.key_context());
        destination = ResourceLedgerStorage(snapshot.key_context());
        EXPECT_EQ(source.insert(snapshot.key_context(), {.key = iron, .amount = 20},
                                ResourceTransferMode::kExecute),
                  20);
        EXPECT_TRUE(endpoints.register_endpoint({.value = "world:source"}, source));
        EXPECT_TRUE(endpoints.register_endpoint({.value = "world:destination"}, destination));
    }
};

[[nodiscard]] SfmFlowProgramRecord transfer_program() {
    return {
        .revision = 12,
        .nodes = {
            {.id = 1, .type = SfmFlowNodeType::kInterval, .interval_ticks = 5},
            {.id = 2,
             .type = SfmFlowNodeType::kTransfer,
             .transfer = {
                 .source = {.value = "world:source"},
                 .destination = {.value = "world:destination"},
                 .requested = ResourceContentStack::item("iron.ingot", 3),
             }},
        },
        .connections = {{.source = 1, .destination = 2}},
    };
}

TEST(SfmFlowProgramTest, CompilesDurableGraphAndExecutesOnlyCompactRuntimeValues) {
    SfmFlowFixture fixture;
    auto compiled = SfmFlowProgramCompiler::compile(
        transfer_program(), fixture.endpoints, fixture.snapshot);
    ASSERT_TRUE(compiled);
    EXPECT_TRUE(compiled->is_valid());
    EXPECT_EQ(compiled->source_revision(), 12u);
    EXPECT_EQ(compiled->node_count(), 2u);
    EXPECT_EQ(compiled->connection_count(), 1u);

    auto executor = SfmFlowExecutor::create(std::move(*compiled), fixture.endpoints);
    ASSERT_TRUE(executor);
    const auto first = executor->tick(100);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->dispatched_nodes, 2u);
    EXPECT_EQ(first->executed_transfers, 1u);
    EXPECT_EQ(first->transferred_units, 3);
    EXPECT_EQ(fixture.source.amount_of(fixture.snapshot.key_context(), fixture.iron), 17);
    EXPECT_EQ(fixture.destination.amount_of(fixture.snapshot.key_context(), fixture.iron), 3);

    const auto idle = executor->tick(104);
    ASSERT_TRUE(idle);
    EXPECT_EQ(idle->dispatched_nodes, 0u);
    const auto second = executor->tick(105);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->transferred_units, 3);
    EXPECT_EQ(fixture.destination.amount_of(fixture.snapshot.key_context(), fixture.iron), 6);
}

TEST(SfmFlowProgramTest, RejectsImmediateCyclesAtCompileBoundary) {
    SfmFlowFixture fixture;
    SfmFlowProgramRecord program = transfer_program();
    program.nodes.push_back({
        .id = 3,
        .type = SfmFlowNodeType::kTransfer,
        .transfer = {
            .source = {.value = "world:source"},
            .destination = {.value = "world:destination"},
            .requested = ResourceContentStack::item("iron.ingot", 1),
        },
    });
    program.connections = {
        {.source = 1, .destination = 2},
        {.source = 2, .destination = 3},
        {.source = 3, .destination = 2},
    };
    EXPECT_FALSE(SfmFlowProgramCompiler::compile(program, fixture.endpoints, fixture.snapshot));
}

TEST(SfmFlowProgramTest, RequiresRecompileAfterEndpointRemoval) {
    SfmFlowFixture fixture;
    auto compiled = SfmFlowProgramCompiler::compile(
        transfer_program(), fixture.endpoints, fixture.snapshot);
    ASSERT_TRUE(compiled);
    auto executor = SfmFlowExecutor::create(std::move(*compiled), fixture.endpoints);
    ASSERT_TRUE(executor);
    ASSERT_TRUE(executor->tick(1));
    const auto destination = fixture.endpoints.resolve_endpoint({.value = "world:destination"});
    ASSERT_TRUE(destination);
    EXPECT_TRUE(fixture.endpoints.unregister_endpoint(*destination));
    EXPECT_FALSE(executor->tick(6));
}

}  // namespace
}  // namespace snt::game
