#include "game/automation/ae_storage_cell.h"
#include "game/automation/sfm_endpoint_registry.h"
#include "game/automation/sfm_resource_transfer.h"

#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"

#include <initializer_list>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Expected<void> rebuild_resource_index(
    ResourceRuntimeIndex& index,
    std::initializer_list<ResourceContentKey> keys) {
    const std::vector<ResourceContentKey> values(keys);
    return index.rebuild(values);
}

class ExecuteRejectingStorage final : public IResourceStorage {
public:
    explicit ExecuteRejectingStorage(ResourceKeyContext context) : context_(std::move(context)) {}

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override { return context_; }

    [[nodiscard]] int64_t amount_of(const ResourceKeyContext&,
                                    const ResourceKey&) const override {
        return 0;
    }

    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override {
        if (!context_.matches(context) || !stack.is_valid()) return 0;
        return mode == ResourceTransferMode::kSimulate ? stack.amount : 0;
    }

    [[nodiscard]] int64_t extract(const ResourceKeyContext&,
                                  const ResourceStack&,
                                  ResourceTransferMode) override {
        return 0;
    }

    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext&) const override {
        return {};
    }

private:
    ResourceKeyContext context_;
};

TEST(SfmResourceTransferTest, MovesOneCompactStackBetweenGenericStorageEndpoints) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const ResourceKey iron =
        *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey water =
        *snapshot.resolve_runtime(ResourceContentKey::fluid("water"));

    ResourceLedgerStorage source(snapshot.key_context());
    ASSERT_EQ(source.insert(snapshot.key_context(), {iron, 25}, ResourceTransferMode::kExecute),
              25);
    ASSERT_EQ(source.insert(snapshot.key_context(), {water, 500}, ResourceTransferMode::kExecute),
              500);

    auto created_destination = AeStorageCell::create({
        .byte_capacity = 3,
        .max_distinct_resources = 1,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, snapshot);
    ASSERT_TRUE(created_destination);
    AeStorageCell destination = std::move(*created_destination);

    const ResourceStack request{.key = iron, .amount = 20};
    const auto simulated = SfmResourceTransfer::transfer(
        source, destination, snapshot.key_context(), request, ResourceTransferMode::kSimulate);
    ASSERT_TRUE(simulated);
    EXPECT_EQ(simulated->transferable, request);
    EXPECT_TRUE(simulated->transferred.is_absent());
    EXPECT_EQ(source.amount_of(snapshot.key_context(), iron), 25);
    EXPECT_EQ(destination.amount_of(snapshot.key_context(), iron), 0);

    const auto executed = SfmResourceTransfer::transfer(
        source, destination, snapshot.key_context(), request, ResourceTransferMode::kExecute);
    ASSERT_TRUE(executed);
    EXPECT_EQ(executed->transferred, request);
    EXPECT_EQ(source.amount_of(snapshot.key_context(), iron), 5);
    EXPECT_EQ(destination.amount_of(snapshot.key_context(), iron), 20);

    const auto rejected_fluid = SfmResourceTransfer::transfer(
        source, destination, snapshot.key_context(), {water, 100}, ResourceTransferMode::kExecute);
    ASSERT_TRUE(rejected_fluid);
    EXPECT_TRUE(rejected_fluid->transferable.is_absent());
    EXPECT_EQ(source.amount_of(snapshot.key_context(), water), 500);
}

TEST(SfmResourceTransferTest, RejectsEndpointsFromAnotherResourceSnapshot) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::item("iron.ingot")}));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ResourceLedgerStorage source(first.key_context());
    ResourceLedgerStorage destination(first.key_context());
    ASSERT_EQ(source.insert(first.key_context(), {first_iron, 5}, ResourceTransferMode::kExecute),
              5);

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("aaa.copper.ingot"),
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    EXPECT_FALSE(SfmResourceTransfer::transfer(
        source, destination, second.key_context(), {second_iron, 5}, ResourceTransferMode::kExecute));
    EXPECT_EQ(source.amount_of(first.key_context(), first_iron), 5);
    EXPECT_EQ(destination.amount_of(first.key_context(), first_iron), 0);
}

TEST(SfmResourceTransferTest, CompensatesTheSourceWhenAnEndpointRejectsAfterSimulation) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::item("iron.ingot")}));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const ResourceKey iron =
        *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ResourceLedgerStorage source(snapshot.key_context());
    ASSERT_EQ(source.insert(snapshot.key_context(), {iron, 5}, ResourceTransferMode::kExecute),
              5);
    ExecuteRejectingStorage destination(snapshot.key_context());
    const ResourceStack request{.key = iron, .amount = 5};

    const auto result = SfmResourceTransfer::transfer(
        source, destination, snapshot.key_context(), request, ResourceTransferMode::kExecute);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->transferable, request);
    EXPECT_TRUE(result->transferred.is_absent());
    EXPECT_EQ(source.amount_of(snapshot.key_context(), iron), 5);
}

TEST(SfmEndpointRegistryTest, CompilesStableRulesIntoCompactFixedTickTransfers) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const ResourceKey iron =
        *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    ResourceLedgerStorage source(snapshot.key_context());
    ResourceLedgerStorage destination(snapshot.key_context());
    ASSERT_EQ(source.insert(snapshot.key_context(), {iron, 12}, ResourceTransferMode::kExecute),
              12);

    SfmEndpointRegistry endpoints;
    const auto source_handle = endpoints.register_endpoint(
        {.value = "machine.alpha.output"}, source);
    const auto destination_handle = endpoints.register_endpoint(
        {.value = "machine.beta.input"}, destination);
    ASSERT_TRUE(source_handle);
    ASSERT_TRUE(destination_handle);

    const SfmResourceTransferRule rule{
        .source = {.value = "machine.alpha.output"},
        .destination = {.value = "machine.beta.input"},
        .requested = ResourceContentStack::item("iron.ingot", 7),
    };
    const auto compiled = endpoints.compile_transfer(rule, snapshot);
    ASSERT_TRUE(compiled);
    EXPECT_EQ(compiled->source, *source_handle);
    EXPECT_EQ(compiled->destination, *destination_handle);
    EXPECT_EQ(compiled->requested, (ResourceStack{.key = iron, .amount = 7}));

    const auto executed = endpoints.execute_transfer(
        snapshot.key_context(), *compiled, ResourceTransferMode::kExecute);
    ASSERT_TRUE(executed);
    EXPECT_EQ(executed->transferred, (ResourceStack{.key = iron, .amount = 7}));
    EXPECT_EQ(source.amount_of(snapshot.key_context(), iron), 5);
    EXPECT_EQ(destination.amount_of(snapshot.key_context(), iron), 7);
}

TEST(SfmEndpointRegistryTest, RejectsAStaleCompiledHandleAfterEndpointReuse) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::item("iron.ingot")}));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const ResourceKey iron =
        *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    ResourceLedgerStorage original_source(snapshot.key_context());
    ResourceLedgerStorage destination(snapshot.key_context());
    ASSERT_EQ(original_source.insert(snapshot.key_context(), {iron, 5},
                                     ResourceTransferMode::kExecute),
              5);
    SfmEndpointRegistry endpoints;
    const auto old_source = endpoints.register_endpoint(
        {.value = "machine.alpha.output"}, original_source);
    const auto destination_handle = endpoints.register_endpoint(
        {.value = "machine.beta.input"}, destination);
    ASSERT_TRUE(old_source);
    ASSERT_TRUE(destination_handle);

    const SfmResourceTransferRule rule{
        .source = {.value = "machine.alpha.output"},
        .destination = {.value = "machine.beta.input"},
        .requested = ResourceContentStack::item("iron.ingot", 5),
    };
    const auto old_compiled = endpoints.compile_transfer(rule, snapshot);
    ASSERT_TRUE(old_compiled);
    ASSERT_TRUE(endpoints.unregister_endpoint(*old_source));

    ResourceLedgerStorage replacement_source(snapshot.key_context());
    ASSERT_EQ(replacement_source.insert(snapshot.key_context(), {iron, 5},
                                        ResourceTransferMode::kExecute),
              5);
    const auto replacement_handle = endpoints.register_endpoint(
        {.value = "machine.alpha.output"}, replacement_source);
    ASSERT_TRUE(replacement_handle);
    EXPECT_NE(*replacement_handle, *old_source);

    EXPECT_FALSE(endpoints.execute_transfer(
        snapshot.key_context(), *old_compiled, ResourceTransferMode::kExecute));
    EXPECT_EQ(original_source.amount_of(snapshot.key_context(), iron), 5);
    EXPECT_EQ(replacement_source.amount_of(snapshot.key_context(), iron), 5);
    EXPECT_EQ(destination.amount_of(snapshot.key_context(), iron), 0);

    const auto rebound = endpoints.compile_transfer(rule, snapshot);
    ASSERT_TRUE(rebound);
    const auto executed = endpoints.execute_transfer(
        snapshot.key_context(), *rebound, ResourceTransferMode::kExecute);
    ASSERT_TRUE(executed);
    EXPECT_EQ(executed->transferred.amount, 5);
    EXPECT_EQ(replacement_source.amount_of(snapshot.key_context(), iron), 0);
    EXPECT_EQ(destination.amount_of(snapshot.key_context(), iron), 5);
}

TEST(SfmEndpointRegistryTest, RequiresRecompilationAfterAResourceSnapshotReload) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::item("iron.ingot")}));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    ResourceLedgerStorage source(first.key_context());
    ResourceLedgerStorage destination(first.key_context());
    ASSERT_EQ(source.insert(first.key_context(), {first_iron, 5}, ResourceTransferMode::kExecute),
              5);
    SfmEndpointRegistry endpoints;
    ASSERT_TRUE(endpoints.register_endpoint({.value = "machine.alpha.output"}, source));
    ASSERT_TRUE(endpoints.register_endpoint({.value = "machine.beta.input"}, destination));
    const SfmResourceTransferRule rule{
        .source = {.value = "machine.alpha.output"},
        .destination = {.value = "machine.beta.input"},
        .requested = ResourceContentStack::item("iron.ingot", 5),
    };
    const auto first_compiled = endpoints.compile_transfer(rule, first);
    ASSERT_TRUE(first_compiled);

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("aaa.copper.ingot"),
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    EXPECT_NE(first_iron, second_iron);
    ASSERT_TRUE(source.rebind(first, second));
    ASSERT_TRUE(destination.rebind(first, second));

    EXPECT_FALSE(endpoints.execute_transfer(
        second.key_context(), *first_compiled, ResourceTransferMode::kExecute));
    EXPECT_EQ(source.amount_of(second.key_context(), second_iron), 5);
    EXPECT_EQ(destination.amount_of(second.key_context(), second_iron), 0);

    const auto second_compiled = endpoints.compile_transfer(rule, second);
    ASSERT_TRUE(second_compiled);
    const auto executed = endpoints.execute_transfer(
        second.key_context(), *second_compiled, ResourceTransferMode::kExecute);
    ASSERT_TRUE(executed);
    EXPECT_EQ(executed->transferred, (ResourceStack{.key = second_iron, .amount = 5}));
}

}  // namespace
}  // namespace snt::game
