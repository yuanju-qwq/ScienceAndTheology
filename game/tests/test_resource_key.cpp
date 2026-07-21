#include "game/resources/resource_key.h"
#include "game/resources/resource_ledger_storage.h"
#include "game/resources/resource_runtime_index.h"
#include "game/resources/resource_state.h"

#include <algorithm>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace snt::game {
namespace {

class ContextBoundStorage final : public IResourceStorage {
public:
    explicit ContextBoundStorage(ResourceKeyContext context) : context_(std::move(context)) {}

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override { return context_; }

    [[nodiscard]] int64_t amount_of(const ResourceKeyContext& context,
                                    const ResourceKey& key) const override {
        if (!context_.matches(context)) return 0;
        const auto found = amounts_.find(key);
        return found == amounts_.end() ? 0 : found->second;
    }

    [[nodiscard]] int64_t insert(const ResourceKeyContext& context,
                                 const ResourceStack& stack,
                                 ResourceTransferMode mode) override {
        if (!context_.matches(context) || !stack.is_valid()) return 0;
        if (mode == ResourceTransferMode::kExecute) amounts_[stack.key] += stack.amount;
        return stack.amount;
    }

    [[nodiscard]] int64_t extract(const ResourceKeyContext& context,
                                  const ResourceKey& key,
                                  int64_t amount,
                                  ResourceTransferMode mode) override {
        if (!context_.matches(context) || !key.is_valid() || amount <= 0) return 0;
        const int64_t extracted = std::min(amount_of(context, key), amount);
        if (mode == ResourceTransferMode::kExecute && extracted != 0) {
            amounts_[key] -= extracted;
            if (amounts_[key] == 0) amounts_.erase(key);
        }
        return extracted;
    }

    [[nodiscard]] std::vector<ResourceKey> stored_keys(
        const ResourceKeyContext& context) const override {
        if (!context_.matches(context)) return {};
        std::vector<ResourceKey> keys;
        keys.reserve(amounts_.size());
        for (const auto& [key, amount] : amounts_) {
            if (amount > 0) keys.push_back(key);
        }
        return keys;
    }

private:
    ResourceKeyContext context_;
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts_;
};

[[nodiscard]] snt::core::Expected<void> rebuild_resource_index(
    ResourceRuntimeIndex& index,
    std::initializer_list<ResourceContentKey> keys) {
    const std::vector<ResourceContentKey> values(keys);
    return index.rebuild(values);
}

TEST(ResourceContentKeyTest, KeepsTypeAndVariantInContentIdentity) {
    const ResourceContentKey iron_ingot = ResourceContentKey::item("material.iron_ingot");
    const ResourceContentKey refined_iron_ingot = ResourceContentKey::item(
        "material.iron_ingot", "grade=refined");
    const ResourceContentKey fluid_iron = ResourceContentKey::fluid("material.iron_ingot");

    EXPECT_TRUE(iron_ingot.is_valid());
    EXPECT_TRUE(iron_ingot.is_item());
    EXPECT_FALSE(iron_ingot.is_fluid());
    EXPECT_NE(iron_ingot, refined_iron_ingot);
    EXPECT_NE(iron_ingot, fluid_iron);
    EXPECT_EQ(refined_iron_ingot.without_variant(), iron_ingot);

    std::unordered_set<ResourceContentKey, ResourceContentKey::Hash> keys;
    keys.insert(iron_ingot);
    keys.insert(refined_iron_ingot);
    keys.insert(fluid_iron);
    EXPECT_EQ(keys.size(), 3u);
}

TEST(ResourceContentStackTest, IsLimitedToContentAndPersistenceBoundaries) {
    const ResourceContentStack items = ResourceContentStack::item("iron.ingot", 64);
    const ResourceContentStack fluid = ResourceContentStack::fluid("water", 8'000);
    const ResourceContentStack invalid_key{{"", "iron.ingot", {}}, 1};

    EXPECT_TRUE(items.is_valid());
    EXPECT_TRUE(items.is_item());
    EXPECT_TRUE(fluid.is_valid());
    EXPECT_TRUE(fluid.is_fluid());
    EXPECT_FALSE(items.has_same_key(fluid));
    EXPECT_FALSE(ResourceContentStack::item("iron.ingot", 0).is_valid());
    EXPECT_FALSE(invalid_key.is_valid());
}

TEST(ResourceKeyTest, UsesOnlyCompactRuntimeIdentityComponents) {
    const ResourceKey first{
        .kind = 1,
        .runtime_id = 42,
        .variant = snt::core::kInvalidRuntimeKeyId,
    };
    const ResourceKey same = first;
    const ResourceKey variant{
        .kind = 1,
        .runtime_id = 42,
        .variant = 7,
    };

    EXPECT_TRUE(first.is_valid());
    EXPECT_EQ(first, same);
    EXPECT_NE(first, variant);
    EXPECT_EQ(first.without_variant(), first);
    EXPECT_EQ(variant.without_variant(), first);

    std::unordered_set<ResourceKey, ResourceKey::Hash> keys;
    keys.insert(first);
    keys.insert(same);
    keys.insert(variant);
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_LE(sizeof(ResourceKey), 16u);
    EXPECT_LE(sizeof(ResourceStack), 24u);
}

TEST(ResourceRuntimeIndexTest, CapturesCompactKeysPerSnapshot) {
    ResourceRuntimeIndex index;
    const std::vector<ResourceContentKey> first_keys{
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::item("iron.ingot", "grade=hot"),
        ResourceContentKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(first_keys));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();

    const auto iron = first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const auto hot_iron = first.resolve_runtime(
        ResourceContentKey::item("iron.ingot", "grade=hot"));
    const auto water = first.resolve_runtime(ResourceContentKey::fluid("water"));
    ASSERT_TRUE(iron);
    ASSERT_TRUE(hot_iron);
    ASSERT_TRUE(water);
    EXPECT_NE(iron->kind, water->kind);
    EXPECT_EQ(iron->runtime_id, hot_iron->runtime_id);
    EXPECT_NE(iron->variant, hot_iron->variant);
    EXPECT_EQ(first.resolve_content(*hot_iron),
              std::optional<ResourceContentKey>{
                  ResourceContentKey::item("iron.ingot", "grade=hot")});

    const std::vector<ResourceContentKey> reloaded_keys{
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(reloaded_keys));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();

    EXPECT_LT(first.generation(), second.generation());
    EXPECT_TRUE(first.resolve_runtime(ResourceContentKey::item("iron.ingot")));
    EXPECT_FALSE(second.resolve_runtime(ResourceContentKey::item("iron.ingot")));
    EXPECT_TRUE(second.resolve_runtime(ResourceContentKey::item("copper.ingot")));
}

TEST(ResourceStackCodecTest, ResolvesContentAtBoundariesAndRebindsAcrossReload) {
    ResourceRuntimeIndex index;
    const std::vector<ResourceContentKey> first_keys{
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(first_keys));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceContentStack content_stack = ResourceContentStack::item("iron.ingot", 12);

    const auto runtime_stack = resolve_resource_stack(content_stack, first);
    ASSERT_TRUE(runtime_stack);
    EXPECT_TRUE(runtime_stack->is_valid());
    EXPECT_EQ(resolve_content_stack(*runtime_stack, first), content_stack);

    const std::vector<ResourceContentKey> second_keys{
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(second_keys));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const auto rebound = rebind_resource_stack(*runtime_stack, first, second);
    ASSERT_TRUE(rebound);
    EXPECT_NE(first.key_context(), second.key_context());
    EXPECT_EQ(resolve_content_stack(*rebound, second), content_stack);
    EXPECT_EQ(resolve_resource_stack(ResourceContentStack{}, second),
              std::optional<ResourceStack>{ResourceStack{}});
}

TEST(ResourceStateTest, KeepsPhysicalAndInstanceStateOutOfResourceIdentity) {
    const ResourceKey water{
        .kind = 2,
        .runtime_id = 11,
        .variant = snt::core::kInvalidRuntimeKeyId,
    };
    const FluidState cold{
        .temperature_kelvin = 275,
        .pressure_pascal = 101'325,
    };
    const FluidState hot{
        .temperature_kelvin = 450,
        .pressure_pascal = 150'000,
        .flow_millibuckets_per_tick = 120,
    };
    const ItemInstance durable_tool{
        .durability = 91,
        .schema_id = "tool",
        .payload = {std::byte{1}},
    };

    EXPECT_EQ(water, water);
    EXPECT_TRUE(cold.is_valid());
    EXPECT_TRUE(hot.is_valid());
    EXPECT_TRUE(durable_tool.is_valid());
    EXPECT_FALSE(durable_tool.empty());
}

TEST(ResourceStorageTest, RejectsKeysFromAnotherRuntimeSnapshot) {
    ResourceRuntimeIndex index;
    const std::vector<ResourceContentKey> keys{
        ResourceContentKey::item("iron.ingot"),
    };
    ASSERT_TRUE(index.rebuild(keys));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_key = *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    ContextBoundStorage storage(first.key_context());

    EXPECT_EQ(storage.insert(first.key_context(), {first_key, 12},
                             ResourceTransferMode::kExecute),
              12);
    EXPECT_EQ(storage.amount_of(first.key_context(), first_key), 12);

    ASSERT_TRUE(index.rebuild(keys));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_key = *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    EXPECT_NE(first.key_context(), second.key_context());
    EXPECT_EQ(storage.insert(second.key_context(), {second_key, 5},
                             ResourceTransferMode::kExecute),
              0);
    EXPECT_EQ(storage.amount_of(second.key_context(), second_key), 0);
    EXPECT_EQ(storage.amount_of(first.key_context(), first_key), 12);
}

TEST(ResourceLedgerStorageTest, StoresAndExtractsOnlyWithinItsBoundSnapshot) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey iron = *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey water = *first.resolve_runtime(ResourceContentKey::fluid("water"));
    ResourceLedgerStorage ledger(first.key_context());

    EXPECT_EQ(ledger.insert(first.key_context(), {iron, 12}, ResourceTransferMode::kSimulate),
              12);
    EXPECT_EQ(ledger.amount_of(first.key_context(), iron), 0);
    EXPECT_EQ(ledger.insert(first.key_context(), {iron, 12}, ResourceTransferMode::kExecute),
              12);
    EXPECT_EQ(ledger.insert(first.key_context(), {water, 1'000}, ResourceTransferMode::kExecute),
              1'000);
    EXPECT_EQ(ledger.extract(first.key_context(), iron, 5, ResourceTransferMode::kSimulate), 5);
    EXPECT_EQ(ledger.amount_of(first.key_context(), iron), 12);
    EXPECT_EQ(ledger.extract(first.key_context(), iron, 5, ResourceTransferMode::kExecute), 5);
    EXPECT_EQ(ledger.amount_of(first.key_context(), iron), 7);

    const std::vector<ResourceKey> keys = ledger.stored_keys(first.key_context());
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_NE(std::find(keys.begin(), keys.end(), iron), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), water), keys.end());

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::fluid("water"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));

    EXPECT_EQ(ledger.insert(second.key_context(), {second_iron, 1},
                            ResourceTransferMode::kExecute),
              0);
    EXPECT_EQ(ledger.amount_of(second.key_context(), second_iron), 0);
    EXPECT_EQ(ledger.amount_of(first.key_context(), iron), 7);
}

TEST(ResourceLedgerStorageTest, RebindsAllAmountsThroughContentKeys) {
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
    ResourceLedgerStorage ledger(first.key_context());
    ASSERT_EQ(ledger.insert(first.key_context(), {first_iron, 12},
                            ResourceTransferMode::kExecute),
              12);
    ASSERT_EQ(ledger.insert(first.key_context(), {first_water, 750},
                            ResourceTransferMode::kExecute),
              750);

    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("copper.ingot"),
        ResourceContentKey::fluid("water"),
        ResourceContentKey::item("iron.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();
    const ResourceKey second_iron =
        *second.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey second_water =
        *second.resolve_runtime(ResourceContentKey::fluid("water"));

    ASSERT_TRUE(ledger.rebind(first, second));
    EXPECT_EQ(ledger.key_context(), second.key_context());
    EXPECT_EQ(ledger.amount_of(first.key_context(), first_iron), 0);
    EXPECT_EQ(ledger.amount_of(second.key_context(), second_iron), 12);
    EXPECT_EQ(ledger.amount_of(second.key_context(), second_water), 750);
}

TEST(ResourceLedgerStorageTest, KeepsTheOldLedgerWhenReloadCannotResolveEveryKey) {
    ResourceRuntimeIndex index;
    ASSERT_TRUE(rebuild_resource_index(index, {
        ResourceContentKey::item("iron.ingot"),
        ResourceContentKey::item("copper.ingot"),
    }));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();
    const ResourceKey first_iron =
        *first.resolve_runtime(ResourceContentKey::item("iron.ingot"));
    const ResourceKey first_copper =
        *first.resolve_runtime(ResourceContentKey::item("copper.ingot"));
    ResourceLedgerStorage ledger(first.key_context());
    ASSERT_EQ(ledger.insert(first.key_context(), {first_iron, 4}, ResourceTransferMode::kExecute),
              4);
    ASSERT_EQ(ledger.insert(first.key_context(), {first_copper, 7}, ResourceTransferMode::kExecute),
              7);

    ASSERT_TRUE(rebuild_resource_index(index, {ResourceContentKey::item("copper.ingot")}));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();

    EXPECT_FALSE(ledger.rebind(first, second));
    EXPECT_EQ(ledger.key_context(), first.key_context());
    EXPECT_EQ(ledger.amount_of(first.key_context(), first_iron), 4);
    EXPECT_EQ(ledger.amount_of(first.key_context(), first_copper), 7);
    EXPECT_TRUE(ledger.stored_keys(second.key_context()).empty());
}

TEST(ResourceLedgerStorageTest, RejectsAnInvalidContextWithoutMutation) {
    ResourceLedgerStorage ledger(ResourceKeyContext{});
    const ResourceKey key{.kind = 1, .runtime_id = 1};

    EXPECT_EQ(ledger.insert(ResourceKeyContext{}, {key, 1}, ResourceTransferMode::kExecute), 0);
    EXPECT_EQ(ledger.amount_of(ResourceKeyContext{}, key), 0);
    EXPECT_TRUE(ledger.stored_keys(ResourceKeyContext{}).empty());
}

}  // namespace
}  // namespace snt::game
