// ScienceAndTheology content-registry tests.

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "game_content_registry.h"

namespace {

using snt::game::EventListener;
using snt::game::GameContentRegistry;
using snt::game::GameItemDefinition;
using snt::game::MachineDefinition;
using snt::game::MachinePlacementDefinition;
using snt::game::RecipeDefinition;
using snt::game::RecipeInputDefinition;
using snt::game::RecipeOutputDefinition;

RecipeDefinition make_recipe(std::string id, std::string input) {
    RecipeDefinition recipe;
    recipe.id = std::move(id);
    recipe.machine_id = "furnace";
    recipe.inputs = {RecipeInputDefinition{std::move(input), 1}};
    recipe.outputs = {RecipeOutputDefinition{"iron_ingot", 1}};
    recipe.duration_ticks = 100;
    return recipe;
}

MachineDefinition make_machine(std::string id) {
    return {
        .id = std::move(id),
        .display_name = "Test Machine",
    };
}

GameItemDefinition make_item(std::string id, int32_t max_stack = 64) {
    return {
        .id = std::move(id),
        .title_key = "item.test",
        .max_stack = max_stack,
    };
}

}  // namespace

TEST(GameContentRegistryTest, ScriptOverrideIsRemovedAndBuiltinFallbackRestored) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("iron_ore")));
    ASSERT_TRUE(content.register_builtin_item(make_item("rich_iron_ore")));
    ASSERT_TRUE(content.register_builtin_item(make_item("iron_ingot")));
    ASSERT_TRUE(content.register_builtin_recipe(make_recipe("iron", "iron_ore")));
    ASSERT_TRUE(content.register_script_recipe(42, make_recipe("iron", "rich_iron_ore")));

    ASSERT_NE(content.find_recipe("iron"), nullptr);
    EXPECT_EQ(content.find_recipe("iron")->inputs.front().item_id, "rich_iron_ore");

    ASSERT_TRUE(content.unload_script(42));
    ASSERT_NE(content.find_recipe("iron"), nullptr);
    EXPECT_EQ(content.find_recipe("iron")->inputs.front().item_id, "iron_ore");
}

TEST(GameContentRegistryTest, RollbackRestoresPreviousScriptContentAndState) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_recipe(make_recipe("copper", "copper_ore")));
    ASSERT_TRUE(content.register_script_recipe(7, make_recipe("copper", "dense_copper_ore")));
    ASSERT_TRUE(content.set_state(7, "furnace.temperature", "840"));
    ASSERT_TRUE(content.add_event_listener(EventListener{7, "machine.tick", "tick_copper"}));

    ASSERT_TRUE(content.begin_reload(7));
    ASSERT_NE(content.find_recipe("copper"), nullptr);
    EXPECT_EQ(content.find_recipe("copper")->inputs.front().item_id, "copper_ore");
    EXPECT_TRUE(content.event_listeners("machine.tick").empty());

    ASSERT_TRUE(content.register_script_recipe(7, make_recipe("copper", "broken_copper_ore")));
    ASSERT_TRUE(content.add_event_listener(EventListener{7, "machine.tick", "broken_tick"}));
    ASSERT_TRUE(content.rollback_reload(7));

    ASSERT_NE(content.find_recipe("copper"), nullptr);
    EXPECT_EQ(content.find_recipe("copper")->inputs.front().item_id, "dense_copper_ore");
    ASSERT_EQ(content.event_listeners("machine.tick").size(), 1U);
    EXPECT_EQ(content.event_listeners("machine.tick")[0].callback_id, "tick_copper");
    EXPECT_EQ(content.get_state(7, "furnace.temperature"), "840");
}

TEST(GameContentRegistryTest, CommitKeepsReplacementAndRejectsDefinitionCollision) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("copper")));
    ASSERT_TRUE(content.register_builtin_item(make_item("copper_dust")));
    ASSERT_TRUE(content.register_builtin_item(make_item("iron_ingot")));
    ASSERT_TRUE(content.register_script_recipe(10, make_recipe("bronze", "copper")));
    EXPECT_FALSE(content.register_script_recipe(11, make_recipe("bronze", "tin")));

    ASSERT_TRUE(content.begin_reload(10));
    ASSERT_TRUE(content.register_script_recipe(10, make_recipe("bronze", "copper_dust")));
    ASSERT_TRUE(content.commit_reload(10));

    ASSERT_NE(content.find_recipe("bronze"), nullptr);
    EXPECT_EQ(content.find_recipe("bronze")->inputs.front().item_id, "copper_dust");
}

TEST(GameContentRegistryTest, RejectsUnloadThatWouldLeaveMachinePlacementDangling) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_script_machine(10, make_machine("p7.unload.machine")));
    ASSERT_TRUE(content.register_script_machine_placement(
        11,
        MachinePlacementDefinition{
            .item_id = "p7.unload.machine_block",
            .machine_id = "p7.unload.machine",
            .material_id = 42,
        }));
    ASSERT_TRUE(content.validate_machine_placement_references());

    EXPECT_FALSE(content.unload_script(10));
    EXPECT_NE(content.find_machine("p7.unload.machine"), nullptr);
    EXPECT_NE(content.find_machine_placement_by_item("p7.unload.machine_block"), nullptr);
}

TEST(GameContentRegistryTest, EventListenersAreStableAndStateIsIsolatedByScript) {
    GameContentRegistry content;
    ASSERT_TRUE(content.add_event_listener(EventListener{9, "block.interact", "zeta"}));
    ASSERT_TRUE(content.add_event_listener(EventListener{3, "block.interact", "alpha"}));
    EXPECT_FALSE(content.add_event_listener(EventListener{3, "block.interact", "alpha"}));

    const auto listeners = content.event_listeners("block.interact");
    ASSERT_EQ(listeners.size(), 2U);
    EXPECT_EQ(listeners[0].script_id, 3U);
    EXPECT_EQ(listeners[1].script_id, 9U);

    ASSERT_TRUE(content.set_state(3, "mode", "normal"));
    ASSERT_TRUE(content.set_state(9, "mode", "expert"));
    EXPECT_EQ(content.get_state(3, "mode"), "normal");
    EXPECT_EQ(content.get_state(9, "mode"), "expert");
}

TEST(GameContentRegistryTest, ItemRuntimeIdsAreNormalizedSortedAndContiguous) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("Zinc.Ingot")));
    ASSERT_TRUE(content.register_builtin_item(make_item("charcoal")));
    ASSERT_TRUE(content.register_builtin_item(make_item("anvil", 1)));

    ASSERT_NE(content.find_item("zinc.ingot"), nullptr);
    EXPECT_EQ(content.find_item("zinc.ingot")->max_stack, 64);
    EXPECT_EQ(content.find_item_runtime_id("anvil"), 1u);
    EXPECT_EQ(content.find_item_runtime_id("charcoal"), 2u);
    EXPECT_EQ(content.find_item_runtime_id("zinc.ingot"), 3u);
    EXPECT_EQ(content.find_item_key(1u), "anvil");
    EXPECT_EQ(content.find_item_key(2u), "charcoal");
    EXPECT_EQ(content.find_item_key(3u), "zinc.ingot");
}

TEST(GameContentRegistryTest, FailedItemReloadPreservesThePreviousRuntimeSnapshot) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_script_item(17, make_item("copper_ore")));
    const auto before = content.item_runtime_index();
    ASSERT_TRUE(before.find_id("copper_ore"));

    ASSERT_TRUE(content.begin_reload(17));
    ASSERT_TRUE(content.register_script_item(17, make_item("zinc_ore")));
    ASSERT_TRUE(content.register_script_recipe(17, make_recipe("broken", "missing_ore")));
    EXPECT_FALSE(content.commit_reload(17));
    ASSERT_TRUE(content.rollback_reload(17));

    const auto after = content.item_runtime_index();
    EXPECT_EQ(after.generation(), before.generation());
    EXPECT_EQ(after.find_id("copper_ore"), before.find_id("copper_ore"));
    EXPECT_FALSE(after.find_id("zinc_ore"));
    EXPECT_NE(content.find_item("copper_ore"), nullptr);
    EXPECT_EQ(content.find_item("zinc_ore"), nullptr);
}
