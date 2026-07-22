// AE autocrafting service coverage.

#include "game/automation/ae_autocrafting.h"
#include "game/automation/ae_storage_cell.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace snt::game
