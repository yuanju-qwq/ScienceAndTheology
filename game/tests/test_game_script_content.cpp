// Game-owned AngelScript binding and transactional content reload tests.

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game/chemistry/element_catalog.h"
#include "game_content_registry.h"
#include "game/simulation/game_content_reload_service.h"
#include "game/simulation/worldgen_script_content.h"
#include "game/worldgen/world_gen_config.h"
#include "script/file_watcher.h"
#include "script/script_manager.h"

namespace fs = std::filesystem;

namespace {

using snt::game::GameContentRegistry;
using snt::game::GameContentReloadService;
using snt::game::GameContentReloadTarget;
using snt::game::chemistry::ElementCatalog;
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
        "    snt_register_item(\"" + input_item + "\", \"item.test_input\", 64);\n"
        "    snt_register_item(\"test_ingot\", \"item.test_ingot\", 64);\n"
        "    snt_register_recipe(\"p7.test.recipe\", \"furnace\", \"" + input_item +
        "\", 1, \"test_ingot\", 1, 20, 0, \"p7-test\");\n"
        "    snt_on(\"p7.test.tick\", \"" + callback_id + "\");\n"
        "}\n";
    if (define_callback) {
        source += "void " + callback_id + "() {}\n";
    }
    return source;
}

std::string read_packaged_game_script(std::string_view file_name) {
    const fs::path path = fs::path(SNT_ENGINE_TEST_ROOT).parent_path() /
                          "game/scripts" / file_name;
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
        "  snt_register_item(\"p7.test.machine_block\", \"item.test_machine_block\", 1);"
        "  snt_register_machine(\"p7.test.machine\", \"Test Machine\", 2, 500, true);"
        "  snt_register_machine_placement(\"p7.test.machine_block\", \"p7.test.machine\", \"snt:test.machine\");"
        "  snt_set_machine_activation_requirements(\"p7.test.machine\", true, false, true, \"hammer\");"
        "  snt_register_quest_chapter(\"p7.test.chapter\", \"Test Chapter\", \"Chapter description\", \"chapter.test\", 3);"
        "  snt_register_quest(\"p7.test.quest\", \"p7.test.chapter\", \"Test Quest\", \"Description\", 96.0f, 48.0f, \"quest.test\", false, false);"
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
    const auto* placement = content.find_machine_placement_by_item("p7.test.machine_block");
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->machine_id, "p7.test.machine");
    EXPECT_EQ(placement->material_key, "snt:test.machine");
    const auto* chapter = content.find_quest_chapter("p7.test.chapter");
    ASSERT_NE(chapter, nullptr);
    EXPECT_EQ(chapter->title, "Test Chapter");
    EXPECT_EQ(chapter->sort_order, 3);
    const auto* quest = content.find_quest("p7.test.quest");
    ASSERT_NE(quest, nullptr);
    EXPECT_EQ(quest->chapter_id, "p7.test.chapter");
    EXPECT_FLOAT_EQ(quest->node_position.x, 96.0f);
    EXPECT_FLOAT_EQ(quest->node_position.y, 48.0f);
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
        "  snt_register_quest(\"legacy.quest\", \"Legacy\", \"Description\");"
        "}"));
    EXPECT_EQ(content.find_machine("legacy.machine"), nullptr);
    EXPECT_EQ(content.find_recipe("legacy.recipe"), nullptr);
    EXPECT_EQ(content.find_quest("legacy.quest"), nullptr);

    scripts.shutdown();
}

TEST(P7ScriptApiTest, RejectsNonCanonicalMaterialElementSymbols) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    EXPECT_FALSE(scripts.load_source(
        "invalid_material_element",
        "void snt_register() {\n"
        "  snt_register_material(\"invalid_element\", \"material.invalid_element\", "
        "0, 0, 0, 0, 0, 1.0f, \"Naq\");\n"
        "  snt_add_material_element(\"invalid_element\", \"Naq\", 1);\n"
        "}\n"));
    EXPECT_EQ(content.find_material("invalid_element"), nullptr);

    scripts.shutdown();
}

TEST(P7ScriptApiTest, RegistersImmutablePeriodicElementMaterialsBeforeScripts) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    EXPECT_EQ(content.material_definitions().size(), 118u);
    const auto expect_element_material = [&content](std::string_view id,
                                                    std::string_view formula,
                                                    uint8_t atom_count) {
        const auto* material = content.find_material(id);
        ASSERT_NE(material, nullptr) << id;
        EXPECT_EQ(material->chemical_formula, formula);
        ASSERT_EQ(material->composition.size(), 1u);
        EXPECT_EQ(material->composition.front().count, atom_count);
    };
    expect_element_material("hydrogen", "H2", 2);
    expect_element_material("carbon", "C", 1);
    expect_element_material("chromium", "Cr", 1);
    expect_element_material("caesium", "Cs", 1);
    expect_element_material("iron", "Fe", 1);
    expect_element_material("oganesson", "Og", 1);
    EXPECT_EQ(content.find_material("chrome"), nullptr);
    EXPECT_EQ(content.find_material("cesium"), nullptr);

    scripts.shutdown();
}

TEST(P7ScriptApiTest, RejectsScriptOverrideOfImmutablePeriodicElementMaterial) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    EXPECT_FALSE(scripts.load_source(
        "periodic_element_override",
        "void snt_register() {\n"
        "  snt_register_material(\"iron\", \"material.override\", "
        "0, 0, 0, 0, 0, 1.0f, \"Fe\");\n"
        "}\n"));
    EXPECT_EQ(content.material_definitions().size(), 118u);
    const auto* iron = content.find_material("iron");
    ASSERT_NE(iron, nullptr);
    EXPECT_EQ(iron->title_key, "material.iron");

    scripts.shutdown();
}

TEST(P7ScriptApiTest, RollsBackPlacementThatReferencesAMissingMachine) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.load_source(
        "p7_machine_placement_validation",
        "void snt_register() {"
        "  snt_register_item(\"p7.valid.block\", \"item.valid_block\", 1);"
        "  snt_register_machine(\"p7.valid.machine\", \"Valid\", 1, 0, false);"
        "  snt_register_machine_placement(\"p7.valid.block\", \"p7.valid.machine\", \"snt:valid.machine\");"
        "}"));

    EXPECT_FALSE(scripts.load_source(
        "p7_machine_placement_validation",
        "void snt_register() {"
        "  snt_register_machine_placement(\"p7.valid.block\", \"p7.missing.machine\", \"snt:valid.machine\");"
        "}"));

    EXPECT_NE(content.find_machine("p7.valid.machine"), nullptr);
    const auto* placement = content.find_machine_placement_by_item("p7.valid.block");
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->machine_id, "p7.valid.machine");
    EXPECT_TRUE(content.validate_machine_placement_references());
    scripts.shutdown();
}

TEST(P7PackagedContentTest, RegistersPrimitiveMachineRecipesFromRuntimeCatalogs) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    const std::string material_source = read_packaged_game_script("00_material_catalog.as");
    const std::string item_source = read_packaged_game_script("10_item_catalog.as");
    const std::string machine_source = read_packaged_game_script("20_machine_catalog.as");
    const std::string automation_source = read_packaged_game_script("25_automation_catalog.as");
    const std::string recipe_source = read_packaged_game_script("30_recipe_catalog.as");
    const std::string quest_source = read_packaged_game_script("40_quest_catalog.as");
    ASSERT_FALSE(material_source.empty());
    ASSERT_FALSE(item_source.empty());
    ASSERT_FALSE(machine_source.empty());
    ASSERT_FALSE(automation_source.empty());
    ASSERT_FALSE(recipe_source.empty());
    ASSERT_FALSE(quest_source.empty());
    ASSERT_TRUE(scripts.load_source("00_material_catalog", material_source));
    ASSERT_TRUE(scripts.load_source("10_item_catalog", item_source));
    ASSERT_TRUE(scripts.load_source("20_machine_catalog", machine_source));
    ASSERT_TRUE(scripts.load_source("25_automation_catalog", automation_source));
    ASSERT_TRUE(scripts.load_source("30_recipe_catalog", recipe_source));
    ASSERT_TRUE(scripts.load_source("40_quest_catalog", quest_source));

    const auto assert_item = [&content](std::string_view id, int32_t max_stack) {
        const auto* item = content.find_item(id);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->max_stack, max_stack);
        ASSERT_TRUE(content.find_resource_runtime_key(
            snt::game::ResourceContentKey::item(std::string(id))));
    };
    assert_item("furnace", 1);
    assert_item("pit_kiln", 1);
    assert_item("charcoal_pit", 1);
    assert_item("bloomery", 1);
    assert_item("anvil", 1);
    assert_item("iron_ore", 64);
    assert_item("ingot.iron", 64);
    assert_item("unfired_bowl", 16);
    assert_item("fired_bowl", 16);
    assert_item("unfired_jug", 16);
    assert_item("fired_jug", 16);
    assert_item("unfired_crucible", 16);
    assert_item("fired_crucible", 16);
    assert_item("unfired_brick", 64);
    assert_item("refractory_brick", 64);
    assert_item("dust.wood", 64);
    assert_item("charcoal", 64);
    assert_item("crushed.iron", 64);
    assert_item("iron_bloom", 16);
    assert_item("ingot.wrought_iron", 64);
    assert_item("flint_and_steel", 1);
    assert_item("hammer", 1);
    assert_item("snt:glow_deer_antler", 64);
    assert_item("crop.wheat", 64);
    assert_item("meat.raw.glow_deer", 64);
    assert_item("ae_cable", 64);
    assert_item("ae_channel_provider", 64);
    assert_item("ae_drive_1k", 64);
    assert_item("ae_storage_bus", 64);
    assert_item("ae_interface", 64);
    assert_item("ae_terminal", 64);

    const auto* ae_drive = content.find_ae_network_node_placement_by_item("ae_drive_1k");
    ASSERT_NE(ae_drive, nullptr);
    EXPECT_EQ(ae_drive->node_key, "automation.ae_drive.1k");
    EXPECT_EQ(ae_drive->type, snt::game::AeNetworkNodeType::kDrive);
    ASSERT_TRUE(ae_drive->drive_storage_cell.has_value());
    EXPECT_EQ(ae_drive->drive_storage_cell->byte_capacity, 1024);
    EXPECT_EQ(ae_drive->drive_storage_cell->max_distinct_resources, 63u);
    EXPECT_EQ(content.find_ae_network_node_placement_by_node_key("automation.ae_drive.1k"),
              ae_drive);
    ASSERT_TRUE(content.validate_ae_network_node_placement_references());

    ASSERT_EQ(content.fluid_definitions().size(), 29u);
    const auto* water = content.find_fluid("water");
    ASSERT_NE(water, nullptr);
    EXPECT_FALSE(water->is_gas);
    EXPECT_EQ(water->default_temperature_kelvin, 300);
    EXPECT_EQ(water->evaporation_target_id, "steam");
    EXPECT_EQ(water->evaporation_temperature_kelvin, 373);
    const auto* steam = content.find_fluid("steam");
    ASSERT_NE(steam, nullptr);
    EXPECT_TRUE(steam->is_gas);
    EXPECT_EQ(steam->condensation_target_id, "water");
    EXPECT_EQ(steam->condensation_temperature_kelvin, 373);
    EXPECT_TRUE(content.find_resource_runtime_key(
        snt::game::ResourceContentKey::fluid("water")).has_value());

    const auto* iron_pickaxe = content.find_item("iron_pickaxe");
    ASSERT_NE(iron_pickaxe, nullptr);
    ASSERT_TRUE(iron_pickaxe->tool.has_value());
    EXPECT_EQ(iron_pickaxe->tool->type, snt::game::GameToolType::kPickaxe);
    EXPECT_EQ(iron_pickaxe->tool->mining_level, 2);
    EXPECT_EQ(iron_pickaxe->tool->durability, 250);
    EXPECT_EQ(iron_pickaxe->presentation.icon_path, "tools/iron_pickaxe_icon_32.png");

    const auto* hammer = content.find_item("hammer");
    ASSERT_NE(hammer, nullptr);
    ASSERT_TRUE(hammer->tool.has_value());
    EXPECT_EQ(hammer->tool->type, snt::game::GameToolType::kNone);
    EXPECT_EQ(hammer->tool->durability, 200);
    EXPECT_EQ(content.tool_tags_for_item("hammer"), (std::vector<std::string>{"hammer"}));

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

    const auto assert_placement = [&content](std::string_view item_id,
                                             std::string_view machine_id,
                                             std::string_view material_key) {
        const auto* placement = content.find_machine_placement_by_item(item_id);
        ASSERT_NE(placement, nullptr);
        EXPECT_EQ(placement->machine_id, machine_id);
        EXPECT_EQ(placement->material_key, material_key);
    };
    assert_placement("furnace", "furnace", "snt:runtime.machine.furnace");
    assert_placement("pit_kiln", "pit_kiln", "snt:runtime.machine.pit_kiln");
    assert_placement("charcoal_pit", "charcoal_pit", "snt:runtime.machine.charcoal_pit");
    assert_placement("bloomery", "bloomery", "snt:runtime.machine.bloomery");
    assert_placement("anvil", "anvil", "snt:runtime.machine.anvil");

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
                  "ingot.iron", 1, 200, "smelting");
    assert_recipe("snt.pit_kiln.fire_unfired_bowl", "pit_kiln", {{"unfired_bowl", 1}},
                  "fired_bowl", 1, 8000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_jug", "pit_kiln", {{"unfired_jug", 1}},
                  "fired_jug", 1, 8000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_crucible", "pit_kiln", {{"unfired_crucible", 1}},
                  "fired_crucible", 1, 12000, "primitive_thermal");
    assert_recipe("snt.pit_kiln.fire_unfired_brick", "pit_kiln", {{"unfired_brick", 1}},
                  "refractory_brick", 1, 6000, "primitive_thermal");
    assert_recipe("snt.charcoal_pit.burn_wood", "charcoal_pit", {{"dust.wood", 16}},
                  "charcoal", 8, 24000, "primitive_thermal");
    assert_recipe("snt.bloomery.iron", "bloomery", {{"crushed.iron", 5}, {"charcoal", 5}},
                  "iron_bloom", 1, 12000, "primitive_thermal");
    assert_recipe("snt.anvil.forge_wrought_iron", "anvil", {{"iron_bloom", 1}},
                  "ingot.wrought_iron", 1, 1, "primitive_forging");

    scripts.shutdown();
}

TEST(P7ContentReloadTest, ReloadsCategoryClosureAtomicallyAndPreservesUnselectedContent) {
    TempScriptDirectory directory("category_reload");
    directory.write("00_material_catalog.as", "void snt_register() {}");
    directory.write(
        "10_item_catalog.as",
        "void snt_register() {\n"
        "  snt_register_item(\"reload.input\", \"item.reload_input_a\", 64);\n"
        "  snt_register_item(\"reload.output\", \"item.reload_output\", 64);\n"
        "}\n");
    directory.write(
        "20_machine_catalog.as",
        "void snt_register() {\n"
        "  snt_register_machine(\"reload.machine\", \"Machine A\", 1, 0, false);\n"
        "}\n");
    directory.write("25_automation_catalog.as", "void snt_register() {}\n");
    directory.write(
        "30_recipe_catalog.as",
        "void snt_register() {\n"
        "  snt_register_recipe(\"reload.recipe\", \"reload.machine\", \"reload.input\", 1,\n"
        "                      \"reload.output\", 1, 20, 0, \"recipe-a\");\n"
        "}\n");
    directory.write(
        "40_quest_catalog.as",
        "void snt_register() {\n"
        "  snt_register_quest_chapter(\"reload.chapter\", \"Reload Chapter\", \"Description\", \"\", 0);\n"
        "  snt_register_quest(\"reload.quest\", \"reload.chapter\", \"Quest A\", \"Description\",\n"
        "                     0.0f, 0.0f, \"\", false, false);\n"
        "}\n");
    directory.write(
        "50_worldgen_catalog.as",
        "void snt_register() {}\n"
        "void snt_register_worldgen() {}\n");

    GameContentReloadService reload_service;
    ASSERT_TRUE(reload_service.configure(directory.root()));

    const auto material_plan = reload_service.plan(GameContentReloadTarget::kMaterials);
    ASSERT_TRUE(material_plan);
    EXPECT_EQ(material_plan->expanded_targets,
              (std::vector<GameContentReloadTarget>{
                  GameContentReloadTarget::kMaterials,
                  GameContentReloadTarget::kItems,
                  GameContentReloadTarget::kMachines,
                  GameContentReloadTarget::kAutomation,
                  GameContentReloadTarget::kRecipes,
                  GameContentReloadTarget::kQuests,
              }));
    EXPECT_EQ(material_plan->files,
              (std::vector<fs::path>{
                  directory.root() / "00_material_catalog.as",
                  directory.root() / "10_item_catalog.as",
                  directory.root() / "20_machine_catalog.as",
                  directory.root() / "25_automation_catalog.as",
                  directory.root() / "30_recipe_catalog.as",
                  directory.root() / "40_quest_catalog.as",
              }));
    const auto worldgen_plan = reload_service.plan(GameContentReloadTarget::kWorldGeneration);
    ASSERT_TRUE(worldgen_plan);
    EXPECT_EQ(worldgen_plan->expanded_targets,
              (std::vector<GameContentReloadTarget>{
                  GameContentReloadTarget::kWorldGeneration,
              }));
    EXPECT_EQ(worldgen_plan->files,
              (std::vector<fs::path>{
                  directory.root() / "50_worldgen_catalog.as",
              }));

    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.load_directory(directory.root().string()));

    directory.write(
        "10_item_catalog.as",
        "void snt_register() {\n"
        "  snt_register_item(\"reload.input\", \"item.reload_input_b\", 64);\n"
        "  snt_register_item(\"reload.output\", \"item.reload_output\", 64);\n"
        "}\n");
    directory.write(
        "20_machine_catalog.as",
        "void snt_register() {\n"
        "  snt_register_machine(\"reload.machine\", \"Machine B\", 1, 0, false);\n"
        "}\n");
    directory.write(
        "30_recipe_catalog.as",
        "void snt_register() {\n"
        "  snt_register_recipe(\"reload.recipe\", \"reload.machine\", \"reload.input\", 1,\n"
        "                      \"reload.output\", 1, 20, 0, \"recipe-b\");\n"
        "}\n");
    directory.write(
        "40_quest_catalog.as",
        "void snt_register() {\n"
        "  snt_register_quest_chapter(\"reload.chapter\", \"Reload Chapter\", \"Description\", \"\", 0);\n"
        "  snt_register_quest(\"reload.quest\", \"reload.chapter\", \"Quest B\", \"Description\",\n"
        "                     0.0f, 0.0f, \"\", false, false);\n"
        "}\n");

    const auto machine_reload = reload_service.reload(scripts, GameContentReloadTarget::kMachines);
    ASSERT_TRUE(machine_reload);
    EXPECT_EQ(machine_reload->plan.expanded_targets,
              (std::vector<GameContentReloadTarget>{
                  GameContentReloadTarget::kMachines,
                  GameContentReloadTarget::kRecipes,
              }));
    ASSERT_NE(content.find_machine("reload.machine"), nullptr);
    EXPECT_EQ(content.find_machine("reload.machine")->display_name, "Machine B");
    ASSERT_NE(content.find_recipe("reload.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("reload.recipe")->tag, "recipe-b");
    ASSERT_NE(content.find_item("reload.input"), nullptr);
    EXPECT_EQ(content.find_item("reload.input")->title_key, "item.reload_input_a");
    ASSERT_NE(content.find_quest("reload.quest"), nullptr);
    EXPECT_EQ(content.find_quest("reload.quest")->title, "Quest A");

    const auto worldgen_reload = reload_service.reload(
        scripts, GameContentReloadTarget::kWorldGeneration);
    ASSERT_TRUE(worldgen_reload);
    EXPECT_EQ(worldgen_reload->plan.files,
              (std::vector<fs::path>{
                  directory.root() / "50_worldgen_catalog.as",
              }));

    const auto all_reload = reload_service.reload(scripts, GameContentReloadTarget::kAll);
    ASSERT_TRUE(all_reload);
    EXPECT_EQ(all_reload->plan.expanded_targets.size(), 7u);
    ASSERT_NE(content.find_item("reload.input"), nullptr);
    EXPECT_EQ(content.find_item("reload.input")->title_key, "item.reload_input_b");
    ASSERT_NE(content.find_quest("reload.quest"), nullptr);
    EXPECT_EQ(content.find_quest("reload.quest")->title, "Quest B");

    directory.write(
        "20_machine_catalog.as",
        "void snt_register() {\n"
        "  snt_register_machine(\"reload.machine\", \"Machine C\", 1, 0, false);\n"
        "}\n");
    directory.write(
        "30_recipe_catalog.as",
        "void snt_register() {\n"
        "  snt_register_recipe(\"reload.recipe\", \"reload.machine\", \"reload.input\", 1,\n"
        "                      \"reload.missing_output\", 1, 20, 0, \"recipe-invalid\");\n"
        "}\n");
    EXPECT_FALSE(reload_service.reload(scripts, GameContentReloadTarget::kMachines));
    ASSERT_NE(content.find_machine("reload.machine"), nullptr);
    EXPECT_EQ(content.find_machine("reload.machine")->display_name, "Machine B");
    ASSERT_NE(content.find_recipe("reload.recipe"), nullptr);
    EXPECT_EQ(content.find_recipe("reload.recipe")->tag, "recipe-b");

    scripts.shutdown();
}

TEST(WorldgenScriptReloadTest, PublishesChangesOnlyToTheNextWorldSnapshot) {
    const auto worldgen_source = [](float hardness) {
        return "void snt_register() {}\n"
               "void snt_register_worldgen() {\n"
               "  snt_register_worldgen_material(\"snt:air\", \"terrain.air\", 0, 0.0f, \"\", 0, 0.3f, 5, \"\");\n"
               "  snt_register_worldgen_material(\"snt:stone\", \"terrain.stone\", 10, " +
               std::to_string(hardness) +
               "f, \"pickaxe\", 0, 0.3f, 5, \"\");\n"
               "  snt_register_worldgen_base_terrain_rule(\n"
               "      \"overworld\", \"solid\", \"snt:stone\", \"snt:stone\", \"snt:stone\", \"snt:air\",\n"
               "      0.02f, 4, 0.05f, 3, -0.25f, 0.3f, 0.55f, 0.04f, 4, 0.35f, 0.25f);\n"
               "}\n";
    };

    TempScriptDirectory directory("worldgen_reload");
    directory.write("50_worldgen_catalog.as", worldgen_source(1.0f));

    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());
    ASSERT_TRUE(scripts.load_directory(directory.root().string()));

    auto first_snapshot = snt::game::build_worldgen_config_from_script(scripts);
    ASSERT_TRUE(first_snapshot) << first_snapshot.error().format();
    ASSERT_NE((*first_snapshot)->find_material("snt:stone"), nullptr);
    EXPECT_FLOAT_EQ((*first_snapshot)->find_material("snt:stone")->hardness, 1.0f);

    GameContentReloadService reload_service;
    ASSERT_TRUE(reload_service.configure(directory.root()));
    directory.write("50_worldgen_catalog.as", worldgen_source(2.0f));
    ASSERT_TRUE(reload_service.reload(scripts, GameContentReloadTarget::kWorldGeneration));

    auto second_snapshot = snt::game::build_worldgen_config_from_script(scripts);
    ASSERT_TRUE(second_snapshot) << second_snapshot.error().format();
    ASSERT_NE((*second_snapshot)->find_material("snt:stone"), nullptr);
    EXPECT_FLOAT_EQ((*second_snapshot)->find_material("snt:stone")->hardness, 2.0f);
    EXPECT_FLOAT_EQ((*first_snapshot)->find_material("snt:stone")->hardness, 1.0f);

    scripts.shutdown();
}

TEST(P7PackagedContentTest, RegistersMigratedMaterialPhysicsAndGeneratedForms) {
    GameContentRegistry content;
    ScriptManager scripts;
    ASSERT_TRUE(scripts.set_content_host(content));
    ASSERT_TRUE(scripts.init());

    const std::string source = read_packaged_game_script("00_material_catalog.as");
    ASSERT_FALSE(source.empty());
    ASSERT_TRUE(scripts.load_source("material_catalog", source));

    EXPECT_EQ(content.material_definitions().size(), 304u);

    std::set<std::string> registered_element_symbols;
    for (const auto& material : content.material_definitions()) {
        for (const auto& element : material.composition) {
            const auto* definition = ElementCatalog::find(element.element);
            ASSERT_NE(definition, nullptr);
            registered_element_symbols.emplace(definition->symbol);
        }
    }
    EXPECT_EQ(registered_element_symbols.size(), 118u);

    const std::array<std::pair<std::string_view, std::string_view>, 45>
        completed_elements = {{
            {"technetium", "Tc"}, {"cerium", "Ce"}, {"praseodymium", "Pr"},
            {"neodymium", "Nd"}, {"promethium", "Pm"}, {"samarium", "Sm"},
            {"europium", "Eu"}, {"gadolinium", "Gd"}, {"terbium", "Tb"},
            {"dysprosium", "Dy"}, {"holmium", "Ho"}, {"erbium", "Er"},
            {"thulium", "Tm"}, {"ytterbium", "Yb"}, {"lutetium", "Lu"},
            {"astatine", "At"}, {"francium", "Fr"}, {"radium", "Ra"},
            {"actinium", "Ac"}, {"protactinium", "Pa"}, {"neptunium", "Np"},
            {"americium", "Am"}, {"curium", "Cm"}, {"berkelium", "Bk"},
            {"californium", "Cf"}, {"einsteinium", "Es"}, {"fermium", "Fm"},
            {"mendelevium", "Md"}, {"nobelium", "No"}, {"lawrencium", "Lr"},
            {"rutherfordium", "Rf"}, {"dubnium", "Db"}, {"seaborgium", "Sg"},
            {"bohrium", "Bh"}, {"hassium", "Hs"}, {"meitnerium", "Mt"},
            {"darmstadtium", "Ds"}, {"roentgenium", "Rg"}, {"copernicium", "Cn"},
            {"nihonium", "Nh"}, {"flerovium", "Fl"}, {"moscovium", "Mc"},
            {"livermorium", "Lv"}, {"tennessine", "Ts"}, {"oganesson", "Og"},
        }};
    for (const auto& [id, symbol] : completed_elements) {
        const auto* element = content.find_material(id);
        ASSERT_NE(element, nullptr) << id;
        EXPECT_EQ(element->chemical_formula, symbol);
        ASSERT_EQ(element->composition.size(), 1u) << id;
        const auto* definition = ElementCatalog::find(element->composition.front().element);
        ASSERT_NE(definition, nullptr) << id;
        EXPECT_EQ(definition->symbol, symbol) << id;
        EXPECT_EQ(element->composition.front().count, 1u) << id;
    }

    const auto* iron = content.find_material("iron");
    ASSERT_NE(iron, nullptr);
    EXPECT_EQ(iron->title_key, "material.iron");
    EXPECT_EQ(iron->color_rgb, 0xc8b0a0u);
    EXPECT_EQ(iron->melting_point_kelvin, 1811);
    EXPECT_EQ(iron->boiling_point_kelvin, 3134);
    EXPECT_FLOAT_EQ(iron->mass, 55.845f);
    EXPECT_EQ(iron->chemical_formula, "Fe");
    ASSERT_EQ(iron->composition.size(), 1u);
    const auto* iron_element = ElementCatalog::find(iron->composition.front().element);
    ASSERT_NE(iron_element, nullptr);
    EXPECT_EQ(iron_element->symbol, "Fe");
    EXPECT_EQ(iron->composition.front().count, 1u);

    const auto* iron_ingot = content.find_item("ingot.iron");
    ASSERT_NE(iron_ingot, nullptr);
    ASSERT_TRUE(iron_ingot->material_form.has_value());
    EXPECT_EQ(iron_ingot->material_form->material_id, "iron");
    EXPECT_EQ(iron_ingot->material_form->material_units, 144);
    EXPECT_EQ(iron_ingot->presentation.icon_path,
              "material_sets/generic/ingot_base_32.png");
    EXPECT_EQ(iron_ingot->presentation.icon_overlay_path,
              "material_sets/generic/ingot_overlay_32.png");
    EXPECT_TRUE(iron_ingot->presentation.uses_tint);

    const auto* wood_log = content.find_item("dust.wood");
    ASSERT_NE(wood_log, nullptr);
    EXPECT_EQ(wood_log->title_key, "item.wood_log");
    EXPECT_EQ(wood_log->presentation.icon_path, "materials/wood_log_icon_32.png");
    EXPECT_FALSE(wood_log->presentation.uses_tint);
    EXPECT_TRUE(content.find_resource_runtime_key(
        snt::game::ResourceContentKey::item("dust.wood")).has_value());

    size_t compound_count = 0;
    for (const auto& item : content.item_definitions()) {
        if (item.title_key.starts_with("item.compound.")) ++compound_count;
    }
    EXPECT_EQ(compound_count, 65u);

    const auto* hematite = content.find_item("crushed.hematite");
    ASSERT_NE(hematite, nullptr);
    EXPECT_EQ(hematite->title_key, "item.compound.crushed_hematite");
    EXPECT_EQ(hematite->presentation.category, snt::game::GameItemCategory::kMaterials);
    EXPECT_EQ(hematite->presentation.icon_path,
              "material_sets/generic/crushed_base_32.png");
    EXPECT_TRUE(hematite->presentation.uses_tint);

    const auto* ash = content.find_item("dust.ash");
    ASSERT_NE(ash, nullptr);
    EXPECT_EQ(ash->presentation.icon_path, "material_sets/generic/dust_base_32.png");

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
