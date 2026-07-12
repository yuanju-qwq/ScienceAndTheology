// Game-owned tests for ScienceAndTheology gameplay UI content.

#include "gameplay_ui.h"

#include "core/path_utils.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace snt::game;
using namespace snt::ui;

snt::core::Expected<snt::core::RuntimePathResolver> make_test_path_resolver() {
    return snt::core::RuntimePathResolver::create({
        .engine_root = SNT_ENGINE_TEST_ROOT,
        .game_root = SNT_ENGINE_TEST_ROOT,
        .user_root = SNT_ENGINE_TEST_ROOT,
    });
}

bool has_text_command(const Arc2DCommandBuffer& buffer, std::string_view needle) {
    for (const auto& command : buffer.commands()) {
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->text.find(needle) != std::string::npos) return true;
        }
    }
    return false;
}

const DrawTextCommand* first_text_command(const Arc2DCommandBuffer& buffer,
                                          std::string_view needle) {
    for (const auto& command : buffer.commands()) {
        if (const auto* text = std::get_if<DrawTextCommand>(&command)) {
            if (text->text.find(needle) != std::string::npos) return text;
        }
    }
    return nullptr;
}

}  // namespace

TEST(GameplayUi, InventoryScreenOpensAndRendersThroughArc2D) {
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_inventory();

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f});
    ASSERT_NE(root->find("hotbar"), nullptr);
    ASSERT_NE(root->find("inventory_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = runtime.build_frame(*root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "背包"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());

    const DrawTextCommand* title = first_text_command(frame.commands, "背包");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
    EXPECT_TRUE(title->layout.contains_emoji);
}

TEST(GameplayUi, PerformancePanelUsesRetainedViewModel) {
    PerformanceViewModel model;
    model.publish({
        .fps = 72.5f,
        .frame_ms = 13.79f,
        .tps = 20.0f,
        .mspt = 4.25f,
        .job_workers = 8,
    });

    auto panel = build_performance_panel_view(model);
    ASSERT_NE(panel->find("performance_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = runtime.build_frame(*panel, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "性能"));
    EXPECT_TRUE(has_text_command(frame.commands, "FPS"));
    EXPECT_TRUE(has_text_command(frame.commands, "72.5"));
    EXPECT_TRUE(has_text_command(frame.commands, "Job Workers"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());
}

TEST(GameplayUi, CraftingScreenConsumesInputsAndProducesOutput) {
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_crafting();

    ASSERT_EQ(controller.inventory().count_item("plank.oak"), 8);
    CraftedItemResult result = controller.crafting().craft("craft_workbench");

    ASSERT_TRUE(result.ok) << result.reason;
    EXPECT_EQ(result.output.item_key, "workbench");
    EXPECT_EQ(controller.inventory().count_item("plank.oak"), 4);
    EXPECT_EQ(controller.inventory().count_item("workbench"), 1);

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f});
    ASSERT_NE(root->find("crafting_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = runtime.build_frame(*root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "合成"));
    EXPECT_TRUE(has_text_command(frame.commands, "可合成"));

    const DrawTextCommand* title = first_text_command(frame.commands, "合成");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
    EXPECT_TRUE(title->layout.contains_emoji);
}
