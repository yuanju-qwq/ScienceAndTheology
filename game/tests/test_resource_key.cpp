#include "game/resources/resource_key.h"
#include "game/resources/resource_runtime_index.h"

#include <unordered_set>

#include <gtest/gtest.h>

namespace snt::game {
namespace {

TEST(ResourceKeyTest, KeepsTypeAndVariantInIdentity) {
    const ResourceKey iron_ingot = ResourceKey::item("material.iron_ingot");
    const ResourceKey hot_iron_ingot = ResourceKey::item(
        "material.iron_ingot", "temperature=1200");
    const ResourceKey fluid_iron = ResourceKey::fluid("material.iron_ingot");

    EXPECT_TRUE(iron_ingot.is_valid());
    EXPECT_TRUE(iron_ingot.is_item());
    EXPECT_FALSE(iron_ingot.is_fluid());
    EXPECT_NE(iron_ingot, hot_iron_ingot);
    EXPECT_NE(iron_ingot, fluid_iron);
    EXPECT_EQ(hot_iron_ingot.without_variant(), iron_ingot);

    std::unordered_set<ResourceKey, ResourceKey::Hash> keys;
    keys.insert(iron_ingot);
    keys.insert(hot_iron_ingot);
    keys.insert(fluid_iron);
    EXPECT_EQ(keys.size(), 3u);
}

TEST(ResourceStackTest, SeparatesIdentityFromAmount) {
    const ResourceStack items = ResourceStack::item("iron.ingot", 64);
    const ResourceStack fluid = ResourceStack::fluid("water", 8'000);
    const ResourceStack invalid_key{{"", "iron.ingot", {}}, 1};

    EXPECT_TRUE(items.is_valid());
    EXPECT_TRUE(items.is_item());
    EXPECT_TRUE(fluid.is_valid());
    EXPECT_TRUE(fluid.is_fluid());
    EXPECT_FALSE(items.has_same_key(fluid));
    EXPECT_FALSE(ResourceStack::item("iron.ingot", 0).is_valid());
    EXPECT_FALSE(invalid_key.is_valid());
}

TEST(RuntimeResourceKeyTest, UsesOnlyFixedWidthIdentityComponents) {
    const RuntimeResourceKey first{
        .type_id = 1,
        .resource_id = 42,
        .variant_id = snt::core::kInvalidRuntimeKeyId,
    };
    const RuntimeResourceKey same = first;
    const RuntimeResourceKey variant{
        .type_id = 1,
        .resource_id = 42,
        .variant_id = 7,
    };

    EXPECT_TRUE(first.is_valid());
    EXPECT_EQ(first, same);
    EXPECT_NE(first, variant);

    std::unordered_set<RuntimeResourceKey, RuntimeResourceKey::Hash> keys;
    keys.insert(first);
    keys.insert(same);
    keys.insert(variant);
    EXPECT_EQ(keys.size(), 2u);
}

TEST(ResourceRuntimeIndexTest, CapturesStableFixedWidthKeysPerSnapshot) {
    ResourceRuntimeIndex index;
    const std::vector<ResourceKey> first_keys{
        ResourceKey::item("iron.ingot"),
        ResourceKey::item("iron.ingot", "grade=hot"),
        ResourceKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(first_keys));
    const ResourceRuntimeIndex::Snapshot first = index.snapshot();

    const auto iron = first.resolve_runtime(ResourceKey::item("iron.ingot"));
    const auto hot_iron = first.resolve_runtime(
        ResourceKey::item("iron.ingot", "grade=hot"));
    const auto water = first.resolve_runtime(ResourceKey::fluid("water"));
    ASSERT_TRUE(iron);
    ASSERT_TRUE(hot_iron);
    ASSERT_TRUE(water);
    EXPECT_NE(iron->type_id, water->type_id);
    EXPECT_EQ(iron->resource_id, hot_iron->resource_id);
    EXPECT_NE(iron->variant_id, hot_iron->variant_id);
    EXPECT_EQ(first.resolve_semantic(*hot_iron),
              std::optional<ResourceKey>{ResourceKey::item("iron.ingot", "grade=hot")});

    const std::vector<ResourceKey> reloaded_keys{
        ResourceKey::item("copper.ingot"),
        ResourceKey::fluid("water"),
    };
    ASSERT_TRUE(index.rebuild(reloaded_keys));
    const ResourceRuntimeIndex::Snapshot second = index.snapshot();

    EXPECT_LT(first.generation(), second.generation());
    EXPECT_TRUE(first.resolve_runtime(ResourceKey::item("iron.ingot")));
    EXPECT_FALSE(second.resolve_runtime(ResourceKey::item("iron.ingot")));
    EXPECT_TRUE(second.resolve_runtime(ResourceKey::item("copper.ingot")));
}

}  // namespace
}  // namespace snt::game
