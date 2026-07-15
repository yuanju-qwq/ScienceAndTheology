// Game-owned AngelScript binding and transactional content reload tests.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game_content_registry.h"
#include "script/file_watcher.h"
#include "script/script_manager.h"

namespace fs = std::filesystem;

namespace {

using snt::game::GameContentRegistry;
using snt::script::FileChange;
using snt::script::FileChangeKind;
using snt::script::ScriptManager;

class TempScriptDirectory {
public:
    explicit TempScriptDirectory(const std::string& name)
        : root_(fs::temp_directory_path() / ("snt_p7_" + name)) {
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
    }

    ~TempScriptDirectory() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    void write(const std::string& name, const std::string& source) const {
        const fs::path path = root_ / name;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << source;
    }

    void remove(const std::string& name) const {
        std::error_code ec;
        fs::remove(root_ / name, ec);
    }

    const fs::path& root() const { return root_; }

private:
    fs::path root_;
};

std::string gameplay_source(const std::string& input_item,
                            const std::string& callback_id,
                            bool define_callback) {
    std::string source =
        "void snt_register() {\n"
        "    snt_register_recipe(\"p7.test.recipe\", \"furnace\", \"" + input_item +
        "\", 1, \"test_ingot\", 1, 20, 0, \"p7-test\");\n"
        "    snt_on(\"p7.test.tick\", \"" + callback_id + "\");\n"
        "}\n";
    if (define_callback) {
        source += "void " + callback_id + "() {}\n";
    }
    return source;
}

std::string read_packaged_p7_bootstrap_script() {
    const fs::path path = fs::path(SNT_ENGINE_TEST_ROOT).parent_path() /
                          "game/scripts/p7_bootstrap.as";
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return {};
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

}  // namespace

TEST(P7FileWatcherTest, ReportsFilteredChangesInStablePathOrder) {
    TempScriptDirectory directory("watcher");
    directory.write("first.as", "void snt_register() {}");

    auto watcher = snt::script::create_polling_file_watcher();
    ASSERT_TRUE(watcher->start(directory.root(), {".as"}));
    EXPECT_TRUE(watcher->drain_changes().empty());

    directory.write("first.as", "void snt_register() { snt_log(\"changed\"); }");
    directory.write("second.as", "void snt_register() {}");
    directory.write("ignored.txt", "not a script");

    const std::vector<FileChange> changes = watcher->drain_changes();
    ASSERT_EQ(changes.size(), 2U);
    EXPECT_EQ(changes[0].path.filename(), "first.as");
    EXPECT_EQ(changes[0].kind, FileChangeKind::Modified);
    EXPECT_EQ(changes[1].path.filename(), "second.as");
    EXPECT_EQ(changes[1].kind, FileChangeKind::Created);

    directory.remove("first.as");
    const std::vector<FileChange> removed = watcher->drain_changes();
    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed[0].path.filename(), "first.as");
    EXPECT_EQ(removed[0].kind, FileChangeKind::Removed);
}

TEST(P7ScriptApiTest, ScriptRegistersCopiedGameplayDefinition) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.load_source(
        "p7_api",
        "void snt_register() {"
        "  snt_register_machine(\"p7.test.machine\", \"Test Machine\", 2, 500, true);"
        "  snt_set_machine_activation_requirements(\"p7.test.machine\", true, false, true, \"hammer\");"
        "  snt_register_quest(\"p7.test.quest\", \"Test Quest\", \"Description\");"
        "  snt_add_quest_objective(\"p7.test.quest\", \"craft.test\", \"craft_item\", \"test_ingot\", 2);"
        "}"));

    const auto* machine = content.find_machine("p7.test.machine");
    ASSERT_NE(machine, nullptr);
    EXPECT_EQ(machine->display_name, "Test Machine");
    EXPECT_EQ(machine->tier, 2);
    EXPECT_EQ(machine->power_capacity, 500);
    EXPECT_TRUE(machine->requires_manual_activation);
    EXPECT_TRUE(machine->activation_requirements.requires_cover);
    EXPECT_FALSE(machine->activation_requirements.requires_ignition);
    EXPECT_TRUE(machine->activation_requirements.requires_valid_structure);
    EXPECT_EQ(machine->activation_requirements.required_tool_tag, "hammer");
    const auto* quest = content.find_quest("p7.test.quest");
    ASSERT_NE(quest, nullptr);
    ASSERT_EQ(quest->objectives.size(), 1u);
    EXPECT_EQ(quest->objectives.front().id, "craft.test");
    EXPECT_EQ(quest->objectives.front().kind, snt::game::QuestObjectiveKind::kCraftItem);
    EXPECT_EQ(quest->objectives.front().target_id, "test_ingot");
    EXPECT_EQ(quest->objectives.front().required_count, 2);
    EXPECT_TRUE(scripts.reload_all());
    scripts.shutdown();
}

TEST(P7ScriptApiTest, RejectsLegacyMachineAndRecipeSignatures) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    EXPECT_FALSE(scripts.load_source(
        "p7_legacy_api",
        "void snt_register() {"
        "  snt_register_machine(\"legacy.machine\", \"Legacy\", 1, 0);"
        "  snt_register_recipe(\"legacy.recipe\", \"legacy.machine\", \"ore\", "
        "\"ingot\", 1, 20, 0, \"legacy\");"
        "}"));
    EXPECT_EQ(content.find_machine("legacy.machine"), nullptr);
    EXPECT_EQ(content.find_recipe("legacy.recipe"), nullptr);

    scripts.shutdown();
}

TEST(P7PackagedContentTest, RegistersPrimitiveMachineRecipesFromTheRuntimeScript) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    const std::string source = read_packaged_p7_bootstrap_script();
    ASSERT_FALSE(source.empty());
    ASSERT_TRUE(scripts.load_source("p7_bootstrap", source));

    const auto assert_machine = [&content](std::string_view id,
                                           std::string_view display_name,
                                           bool requires_manual_activation) {
        const auto* machine = content.find_machine(id);
        ASSERT_NE(machine, nullptr);
        EXPECT_EQ(machine->display_name, display_name);
        EXPECT_EQ(machine->tier, 1);
        EXPECT_EQ(machine->power_capacity, 0);
        EXPECT_EQ(machine->requires_manual_activation, requires_manual_activation);
    };
    assert_machine("furnace", "Furnace", false);
    assert_machine("pit_kiln", "Pit Kiln", true);
    assert_machine("charcoal_pit", "Charcoal Pit", true);
    assert_machine("bloomery", "Bloomery", true);
    assert_machine("anvil", "Anvil", true);

    const auto assert_requirements = [&content](std::string_view id,
                                                bool requires_cover,
                                                bool requires_ignition,
                                                bool requires_valid_structure,
                                                std::string_view required_tool_tag) {
        const auto* machine = content.find_machine(id);
        ASSERT_NE(machine, nullptr);
        EXPECT_EQ(machine->activation_requirements.requires_cover, requires_cover);
        EXPECT_EQ(machine->activation_requirements.requires_ignition, requires_ignition);
        EXPECT_EQ(machine->activation_requirements.requires_valid_structure,
                  requires_valid_structure);
        EXPECT_EQ(machine->activation_requirements.required_tool_tag, required_tool_tag);
    };
    assert_requirements("furnace", false, false, false, "");
    assert_requirements("pit_kiln", true, true, false, "");
    assert_requirements("charcoal_pit", true, true, false, "");
    assert_requirements("bloomery", false, true, true, "");
    assert_requirements("anvil", false, false, false, "hammer");

    const auto assert_recipe = [&content](std::string_view id,
                                          std::string_view machine_id,
                                          const std::vector<std::pair<std::string_view, int32_t>>& inputs,
                                          std::string_view output,
                                          int32_t output_count,
                                          int32_t duration_ticks,
                                          std::string_view tag) {
        const auto* recipe = content.find_recipe(id);
        ASSERT_NE(recipe, nullptr);
        EXPECT_EQ(recipe->machine_id, machine_id);
        ASSERT_EQ(recipe->inputs.size(), inputs.size());
        for (size_t index = 0; index < inputs.size(); ++index) {
            EXPECT_EQ(recipe->inputs[index].item_id, inputs[index].first);
            EXPECT_EQ(recipe->inputs[index].count, inputs[index].second);
        }
        ASSERT_EQ(recipe->outputs.size(), 1u);
        EXPECT_EQ(recipe->outputs.front().item_id, output);
        EXPECT_EQ(recipe->outputs.front().count, output_count);
        EXPECT_EQ(recipe->duration_ticks, duration_ticks);
        EXPECT_EQ(recipe->energy_per_tick, 0);
        EXPECT_EQ(recipe->tag, tag);
    };
    assert_recipe("snt.furnace.iron", "furnace", {{"iron_ore", 1}},
                  "iron_ingot", 1, 200, "smelting");
    assert_recipe("snt.pit_kiln.fire_unfired_bowl", "pit_kiln", {{"unfired_bowl", 1}},
                  "fired_bowl", 1, 8000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_jug", "pit_kiln", {{"unfired_jug", 1}},
                  "fired_jug", 1, 8000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_crucible", "pit_kiln", {{"unfired_crucible", 1}},
                  "fired_crucible", 1, 12000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_brick", "pit_kiln", {{"unfired_brick", 1}},
                  "refractory_brick", 1, 6000, "primitive_thermal");
    assert_recipe("snt.charcoal_pit.burn_wood", "charcoal_pit", {{"wood_dust", 16}},
                  "charcoal", 8, 24000, "primitive_thermal");
    assert_recipe("snt.bloomery.iron", "bloomery", {{"iron_crushed", 5}, {"charcoal", 5}},
                  "iron_bloom", 1, 12000, "primitive_thermal");
    assert_recipe("snt.anvil.forge_wrought_iron", "anvil", {{"iron_bloom", 1}},
                  "wrought_iron_ingot", 1, 1, "primitive_forging");

    scripts.shutdown();
}

TEST(P7ScriptReloadTest, WatcherCommitsSuccessAndRollsBackFailuresWithoutStaleCallbacks) {
    TempScriptDirectory directory("transaction");
    directory.write("gameplay.as", gameplay_source("ore_a", "on_tick", true));

    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.watch_directory(directory.root()));

    ASSERT_NE(content.find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("p7.test.recipe")->inputs.front().item_id, "ore_a");

    directory.write("gameplay.as", gameplay_source("ore_b", "on_tick", true));
    scripts.update(0.016f);
    ASSERT_NE(content.find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("p7.test.recipe")->inputs.front().item_id, "ore_b");

    directory.write("gameplay.as", "void snt_register() {");
    scripts.update(0.016f);
    ASSERT_NE(content.find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("p7.test.recipe")->inputs.front().item_id, "ore_b");

    directory.write("gameplay.as", gameplay_source("ore_bad", "missing_callback", false));
    scripts.update(0.016f);
    ASSERT_NE(content.find_recipe("p7.test.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("p7.test.recipe")->inputs.front().item_id, "ore_b");
    const auto listeners = content.event_listeners("p7.test.tick");
    ASSERT_EQ(listeners.size(), 1U);
    EXPECT_EQ(listeners[0].callback_id, "on_tick");

    scripts.shutdown();
}
