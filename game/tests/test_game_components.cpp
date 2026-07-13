// Tests for ScienceAndTheology-owned ECS components.

#include "game_components.h"

#include "core/binary_reader.h"
#include "core/binary_writer.h"
#include "core/serializer.h"
#include "ecs/world.h"

#include <gtest/gtest.h>

namespace snt::game {

TEST(GameComponentsTest, HealthDefaultsAndHelpers) {
    Health health;
    EXPECT_FLOAT_EQ(health.current, 1.0f);
    EXPECT_FLOAT_EQ(health.maximum, 1.0f);
    EXPECT_FALSE(health.is_dead());
    EXPECT_FLOAT_EQ(health.fraction(), 1.0f);

    health.current = 0.0f;
    EXPECT_TRUE(health.is_dead());
    health.current = 0.5f;
    health.maximum = 2.0f;
    EXPECT_FLOAT_EQ(health.fraction(), 0.25f);
}

TEST(GameComponentsTest, InventoryRoundTrips) {
    Inventory source;
    source.max_slots = 24;
    source.slots = {{"snt:ore", 3}, {"snt:ingot", 7}};

    snt::core::BinaryWriter writer;
    snt::core::Serializer<Inventory>::write(writer, source);
    snt::core::BinaryReader reader(writer.buffer());

    Inventory restored;
    ASSERT_TRUE(snt::core::Serializer<Inventory>::read(reader, restored));
    EXPECT_EQ(restored.max_slots, 24);
    ASSERT_EQ(restored.slots.size(), 2u);
    EXPECT_EQ(restored.slots[0].item_key, "snt:ore");
    EXPECT_EQ(restored.slots[0].count, 3);
    EXPECT_EQ(restored.slots[1].item_key, "snt:ingot");
    EXPECT_EQ(restored.slots[1].count, 7);
}

TEST(GameComponentsTest, MarkersBelongToGameEntities) {
    snt::ecs::World world;
    const auto player = world.create_entity();
    const auto creature = world.create_entity();
    const auto machine = world.create_entity();

    world.registry().emplace<PlayerMarker>(player);
    world.registry().emplace<CreatureMarker>(creature);
    world.registry().emplace<StaticMarker>(machine);

    EXPECT_TRUE(world.registry().all_of<PlayerMarker>(player));
    EXPECT_TRUE(world.registry().all_of<CreatureMarker>(creature));
    EXPECT_TRUE(world.registry().all_of<StaticMarker>(machine));
    EXPECT_FALSE(world.registry().all_of<CreatureMarker>(player));
}

}  // namespace snt::game
