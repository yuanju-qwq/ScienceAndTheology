#include "game/automation/ae_storage_cell.h"

#include "game/client/game_content_registry.h"
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

TEST(AeStorageCellTest, UsesCompactKeysForFilteredConstantStateCapacityOperations) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const ResourceKey iron =
        *snapshot.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey copper =
        *snapshot.resolve_runtime(ResourceContentKey::item("copper.ingot"));
    const ResourceKey water =
        *snapshot.resolve_runtime(ResourceContentKey::fluid("water"));

    auto created = AeStorageCell::create({
        .byte_capacity = 6,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, snapshot);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);

    EXPECT_EQ(cell.insert(snapshot.key_context(), {water, 1}, ResourceTransferMode::kExecute), 0);
    EXPECT_EQ(cell.insert(snapshot.key_context(), {iron, 1}, ResourceTransferMode::kExecute), 1);
    EXPECT_EQ(cell.used_bytes(), 2);
    EXPECT_EQ(cell.insert(snapshot.key_context(), {iron, 19}, ResourceTransferMode::kExecute), 19);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), iron), 20);
    EXPECT_EQ(cell.used_bytes(), 3);

    EXPECT_EQ(cell.insert(snapshot.key_context(), {copper, 30}, ResourceTransferMode::kExecute),
              20);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), copper), 20);
    EXPECT_EQ(cell.used_bytes(), 6);
    EXPECT_EQ(cell.insert(snapshot.key_context(), {iron, 1}, ResourceTransferMode::kExecute), 0);

    EXPECT_EQ(cell.extract(snapshot.key_context(), {copper, 5}, ResourceTransferMode::kSimulate),
              5);
    EXPECT_EQ(cell.amount_of(snapshot.key_context(), copper), 20);
    EXPECT_EQ(cell.extract(snapshot.key_context(), {iron, 20}, ResourceTransferMode::kExecute),
              20);
    EXPECT_EQ(cell.used_bytes(), 3);
    EXPECT_EQ(cell.distinct_resource_count(), 1u);
}

TEST(AeStorageCellTest, RebindsItsCompactKeysWithoutChangingCapacityState) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    auto created = AeStorageCell::create({
        .byte_capacity = 12,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 2,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {first_iron, 17}, ResourceTransferMode::kExecute),
              17);
    const int64_t used_before_rebind = cell.used_bytes();

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey{"alloy", "steel"},
        ResourceContentKey::fluid("water"),
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    ASSERT_TRUE(cell.rebind(first, second));
    EXPECT_FALSE(cell.key_context().matches(first.key_context()));
    EXPECT_EQ(cell.amount_of(first.key_context(), first_iron), 0);
    EXPECT_EQ(cell.amount_of(second.key_context(), second_iron), 17);
    EXPECT_EQ(cell.used_bytes(), used_before_rebind);
}

TEST(AeStorageCellTest, PreparesAContentReloadWithoutPublishingItEarly) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    auto created = AeStorageCell::create({
        .byte_capacity = 12,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 2,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {first_iron, 17}, ResourceTransferMode::kExecute),
              17);

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey{"alloy", "steel"},
        ResourceContentKey::fluid("water"),
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    ASSERT_TRUE(cell.prepare_resource_runtime_snapshot(second));
    EXPECT_TRUE(cell.key_context().matches(first.key_context()));
    EXPECT_EQ(cell.amount_of(first.key_context(), first_iron), 17);
    EXPECT_EQ(cell.amount_of(second.key_context(), second_iron), 0);

    cell.commit_resource_runtime_snapshot();
    EXPECT_TRUE(cell.key_context().matches(second.key_context()));
    EXPECT_EQ(cell.amount_of(first.key_context(), first_iron), 0);
    EXPECT_EQ(cell.amount_of(second.key_context(), second_iron), 17);
}

TEST(AeStorageCellTest, ParticipatesInGameContentSnapshotPublication) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item({
        .id = "iron.ingot",
        .title_key = "item.iron.ingot",
        .max_stack = 64,
    }));
    const ResourceRuntimeIndex::Snapshot first = content.resource_runtime_index();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    auto created = AeStorageCell::create({
        .byte_capacity = 12,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 2,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {first_iron, 17}, ResourceTransferMode::kExecute),
              17);
    ASSERT_TRUE(content.add_resource_runtime_snapshot_participant(cell));

    ASSERT_TRUE(content.register_builtin_item({
        .id = "aaa.copper.ingot",
        .title_key = "item.aaa.copper.ingot",
        .max_stack = 64,
    }));
    const ResourceRuntimeIndex::Snapshot second = content.resource_runtime_index();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    EXPECT_TRUE(cell.key_context().matches(second.key_context()));
    EXPECT_EQ(cell.amount_of(first.key_context(), first_iron), 0);
    EXPECT_EQ(cell.amount_of(second.key_context(), second_iron), 17);
    content.remove_resource_runtime_snapshot_participant(cell);
}

TEST(AeStorageCellTest, KeepsTheOldSnapshotAndAmountsWhenReloadCannotResolveAStoredKey) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    auto created = AeStorageCell::create({
        .byte_capacity = 12,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 2,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    }, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {first_iron, 17}, ResourceTransferMode::kExecute),
              17);

    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::fluid("water")}));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();

    EXPECT_FALSE(cell.rebind(first, second));
    EXPECT_TRUE(cell.key_context().matches(first.key_context()));
    EXPECT_EQ(cell.amount_of(first.key_context(), first_iron), 17);
    EXPECT_EQ(cell.used_bytes(), 4);
}

TEST(AeStorageCellTest, PersistsStableContentStacksAndRestoresIntoAnotherSnapshot) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey first_water =
        *first.resolve_runtime(ResourceContentKey::fluid("water"));

    const AeStorageCellConfig config{
        .byte_capacity = 16,
        .max_distinct_resources = 3,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 10,
    };
    auto created = AeStorageCell::create(config, first);
    ASSERT_TRUE(created);
    AeStorageCell cell = std::move(*created);
    ASSERT_EQ(cell.insert(first.key_context(), {first_iron, 17}, ResourceTransferMode::kExecute),
              17);
    ASSERT_EQ(cell.insert(first.key_context(), {first_water, 25}, ResourceTransferMode::kExecute),
              25);

    const auto persisted = cell.capture_persistence_record();
    ASSERT_TRUE(persisted);
    ASSERT_EQ(persisted->stored_resources.size(), 2u);
    EXPECT_EQ(persisted->stored_resources[0], ResourceContentStack::fluid("water", 25));
    EXPECT_EQ(persisted->stored_resources[1], ResourceContentStack::item("iron.ingot", 17));

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey{"alloy", "steel"},
        ResourceContentKey::item("aaa.copper.ingot"),
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey second_water =
        *second.resolve_runtime(ResourceContentKey::fluid("water"));
    EXPECT_NE(first_iron, second_iron);

    const auto restored = AeStorageCell::restore_persistence_record(config, *persisted, second);
    ASSERT_TRUE(restored);
    EXPECT_TRUE(restored->key_context().matches(second.key_context()));
    EXPECT_EQ(restored->amount_of(second.key_context(), second_iron), 17);
    EXPECT_EQ(restored->amount_of(second.key_context(), second_water), 25);
    EXPECT_EQ(restored->used_bytes(), 7);

    const auto captured_again = restored->capture_persistence_record();
    ASSERT_TRUE(captured_again);
    EXPECT_EQ(captured_again->stored_resources, persisted->stored_resources);
}

TEST(AeStorageCellTest, RejectsMalformedUnresolvedAndOverCapacityPersistenceRecords) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot snapshot = index.snapshot();
    const AeStorageCellConfig config{
        .byte_capacity = 3,
        .max_distinct_resources = 2,
        .bytes_per_distinct_resource = 1,
        .units_per_byte = 10,
        .accepted_resource_types = {std::string(kResourceTypeItem)},
    };

    const AeStorageCellPersistenceRecord duplicate{
        .stored_resources = {
            ResourceContentStack::item("iron.ingot", 1),
            ResourceContentStack::item("iron.ingot", 2),
        },
    };
    EXPECT_FALSE(AeStorageCell::restore_persistence_record(config, duplicate, snapshot));

    const AeStorageCellPersistenceRecord malformed{
        .stored_resources = {ResourceContentStack::item("iron.ingot", 0)},
    };
    EXPECT_FALSE(AeStorageCell::restore_persistence_record(config, malformed, snapshot));

    const AeStorageCellPersistenceRecord unresolved{
        .stored_resources = {ResourceContentStack::item("copper.ingot", 1)},
    };
    EXPECT_FALSE(AeStorageCell::restore_persistence_record(config, unresolved, snapshot));

    const AeStorageCellPersistenceRecord over_capacity{
        .stored_resources = {ResourceContentStack::item("iron.ingot", 21)},
    };
    EXPECT_FALSE(AeStorageCell::restore_persistence_record(config, over_capacity, snapshot));
}

}  // namespace
}  // namespace snt::game
