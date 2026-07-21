// ScienceAndTheology content-registry tests.

#include <string>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "game_content_registry.h"

namespace {

using snt::game::EventListener;
using snt::game::GameContentRegistry;
using snt::game::GameFluidDefinition;
using snt::game::GameItemDefinition;
using snt::game::MachineDefinition;
using snt::game::MachinePlacementDefinition;
using snt::game::RecipeDefinition;
using snt::game::RecipeInputDefinition;
using snt::game::RecipeOutputDefinition;
using snt::game::ResourceContentKey;

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

GameFluidDefinition make_fluid(std::string id,
                               bool is_gas = false,
                               std::string evaporation_target_id = {},
                               int64_t evaporation_temperature_kelvin = 0,
                               std::string condensation_target_id = {},
                               int64_t condensation_temperature_kelvin = 0) {
    return {
        .id = std::move(id),
        .title_key = "fluid.test",
        .chemical_formula = "?",
        .default_temperature_kelvin = 300,
        .is_gas = is_gas,
        .evaporation_target_id = std::move(evaporation_target_id),
        .evaporation_temperature_kelvin = evaporation_temperature_kelvin,
        .condensation_target_id = std::move(condensation_target_id),
        .condensation_temperature_kelvin = condensation_temperature_kelvin,
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
            .material_key = "snt:machine.unload",
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

TEST(GameContentRegistryTest, ResourceRuntimeKeysAreNormalizedSortedAndContiguous) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("Zinc.Ingot")));
    ASSERT_TRUE(content.register_builtin_item(make_item("charcoal")));
    ASSERT_TRUE(content.register_builtin_item(make_item("anvil", 1)));

    ASSERT_NE(content.find_item("zinc.ingot"), nullptr);
    EXPECT_EQ(content.find_item("zinc.ingot")->max_stack, 64);
    const auto anvil = content.find_resource_runtime_key(ResourceContentKey::item("anvil"));
    const auto charcoal = content.find_resource_runtime_key(ResourceContentKey::item("charcoal"));
    const auto zinc = content.find_resource_runtime_key(ResourceContentKey::item("zinc.ingot"));
    ASSERT_TRUE(anvil);
    ASSERT_TRUE(charcoal);
    ASSERT_TRUE(zinc);
    EXPECT_EQ(anvil->kind, charcoal->kind);
    EXPECT_EQ(charcoal->kind, zinc->kind);
    EXPECT_EQ(anvil->runtime_id, 1u);
    EXPECT_EQ(charcoal->runtime_id, 2u);
    EXPECT_EQ(zinc->runtime_id, 3u);
    EXPECT_EQ(content.find_resource_content_key(*anvil),
              std::optional<ResourceContentKey>{ResourceContentKey::item("anvil")});
    EXPECT_EQ(content.find_resource_content_key(*charcoal),
              std::optional<ResourceContentKey>{ResourceContentKey::item("charcoal")});
    EXPECT_EQ(content.find_resource_content_key(*zinc),
              std::optional<ResourceContentKey>{ResourceContentKey::item("zinc.ingot")});
}

TEST(GameContentRegistryTest, ResourceRuntimeIndexIncludesFluidKeysWithTheSameSnapshot) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_builtin_item(make_item("bucket")));
    ASSERT_TRUE(content.register_builtin_fluid(make_fluid("water")));

    const auto item = content.find_resource_runtime_key(ResourceContentKey::item("bucket"));
    const auto fluid = content.find_resource_runtime_key(ResourceContentKey::fluid("water"));
    ASSERT_TRUE(item);
    ASSERT_TRUE(fluid);
    EXPECT_NE(item->kind, fluid->kind);
    EXPECT_EQ(fluid->runtime_id, 1u);
    EXPECT_EQ(content.find_resource_content_key(*fluid),
              std::optional<ResourceContentKey>{ResourceContentKey::fluid("water")});
    ASSERT_NE(content.find_fluid("water"), nullptr);
    EXPECT_EQ(content.fluid_definitions().size(), 1u);
}

TEST(GameContentRegistryTest, FailedItemReloadPreservesThePreviousResourceRuntimeSnapshot) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_script_item(17, make_item("copper_ore")));
    const auto before = content.resource_runtime_index();
    ASSERT_TRUE(before.resolve_runtime(ResourceContentKey::item("copper_ore")));

    ASSERT_TRUE(content.begin_reload(17));
    ASSERT_TRUE(content.register_script_item(17, make_item("zinc_ore")));
    ASSERT_TRUE(content.register_script_recipe(17, make_recipe("broken", "missing_ore")));
    EXPECT_FALSE(content.commit_reload(17));
    ASSERT_TRUE(content.rollback_reload(17));

    const auto after = content.resource_runtime_index();
    EXPECT_EQ(after.generation(), before.generation());
    EXPECT_EQ(after.resolve_runtime(ResourceContentKey::item("copper_ore")),
              before.resolve_runtime(ResourceContentKey::item("copper_ore")));
    EXPECT_FALSE(after.resolve_runtime(ResourceContentKey::item("zinc_ore")));
    EXPECT_NE(content.find_item("copper_ore"), nullptr);
    EXPECT_EQ(content.find_item("zinc_ore"), nullptr);
}

TEST(GameContentRegistryTest, FailedFluidReloadRestoresThePriorResourceSnapshot) {
    GameContentRegistry content;
    ASSERT_TRUE(content.register_script_fluid(17, make_fluid("water")));
    const auto before = content.resource_runtime_index();
    ASSERT_TRUE(before.resolve_runtime(ResourceContentKey::fluid("water")));

    ASSERT_TRUE(content.begin_reload(17));
    ASSERT_TRUE(content.register_script_fluid(17, make_fluid("oil")));
    ASSERT_TRUE(content.register_script_recipe(17, make_recipe("broken", "missing_ore")));
    EXPECT_FALSE(content.commit_reload(17));
    ASSERT_TRUE(content.rollback_reload(17));

    const auto after = content.resource_runtime_index();
    EXPECT_EQ(after.generation(), before.generation());
    EXPECT_EQ(after.resolve_runtime(ResourceContentKey::fluid("water")),
              before.resolve_runtime(ResourceContentKey::fluid("water")));
    EXPECT_FALSE(after.resolve_runtime(ResourceContentKey::fluid("oil")));
    EXPECT_NE(content.find_fluid("water"), nullptr);
    EXPECT_EQ(content.find_fluid("oil"), nullptr);
}
