// Machine placement content registry coverage.

#include "game/simulation/machine_placement_registry.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace {

using snt::game::MachinePlacementDefinition;
using snt::game::MachinePlacementRegistry;

MachinePlacementDefinition placement(std::string item_id, std::string machine_id,
                                     std::string material_key) {
    return {
        .item_id = std::move(item_id),
        .machine_id = std::move(machine_id),
        .material_key = std::move(material_key),
    };
}

}  // namespace

TEST(MachinePlacementRegistryTest, RestoresBuiltinPlacementAfterScriptUnload) {
    MachinePlacementRegistry registry;
    ASSERT_TRUE(registry.register_builtin(placement("furnace", "furnace", "snt:machine.furnace")));
    ASSERT_TRUE(registry.register_script(41, placement("furnace", "mod.furnace", "snt:machine.mod_furnace")));
    // The built-in fallback becomes live again on unload, so its material
    // remains reserved while a script shadows the same item.
    EXPECT_FALSE(registry.register_script(42, placement("anvil", "anvil", "snt:machine.furnace")));

    const auto* override = registry.find_by_item("furnace");
    ASSERT_NE(override, nullptr);
    EXPECT_EQ(override->machine_id, "mod.furnace");
    EXPECT_EQ(override->material_key, "snt:machine.mod_furnace");

    ASSERT_TRUE(registry.unload_script(41));
    const auto* fallback = registry.find_by_item("furnace");
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(fallback->machine_id, "furnace");
    EXPECT_EQ(fallback->material_key, "snt:machine.furnace");
    EXPECT_EQ(registry.find_by_material_key("snt:machine.furnace"), fallback);
}

TEST(MachinePlacementRegistryTest, RollbackRestoresScriptPlacementAndRejectsMaterialCollision) {
    MachinePlacementRegistry registry;
    ASSERT_TRUE(registry.register_builtin(placement("furnace", "furnace", "snt:machine.furnace")));
    ASSERT_TRUE(registry.register_script(52, placement("bloomery", "bloomery", "snt:machine.bloomery")));
    EXPECT_FALSE(registry.register_script(53, placement("anvil", "anvil", "snt:machine.bloomery")));

    ASSERT_TRUE(registry.begin_reload(52));
    EXPECT_EQ(registry.find_by_item("bloomery"), nullptr);
    ASSERT_TRUE(registry.register_script(52, placement("bloomery", "mod.bloomery", "snt:machine.mod_bloomery")));
    ASSERT_TRUE(registry.rollback_reload(52));

    const auto* restored = registry.find_by_item("bloomery");
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->machine_id, "bloomery");
    EXPECT_EQ(restored->material_key, "snt:machine.bloomery");
}
