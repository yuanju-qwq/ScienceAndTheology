// Game-owned tests for ScienceAndTheology gameplay UI content.

#include "gameplay_ui.h"

#include "core/path_utils.h"
#include "game/localization/localization.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace {

using namespace snt::game;
using namespace snt::ui;
using snt::game::localization::ILocalizationCatalogSource;
using snt::game::localization::LocalizationCatalog;
using snt::game::localization::LocalizationLoadConfig;
using snt::game::localization::LocalizationService;

class TestCatalogSource final : public ILocalizationCatalogSource {
public:
    explicit TestCatalogSource(std::unordered_map<std::string, LocalizationCatalog> catalogs)
        : catalogs_(std::move(catalogs)) {}

    snt::core::Expected<LocalizationCatalog> load_catalog(std::string_view locale) const override {
        const auto catalog = catalogs_.find(std::string(locale));
        if (catalog == catalogs_.end()) {
            return snt::core::Error{snt::core::ErrorCode::kFileNotFound,
                                    "Test localization catalog is missing"};
        }
        return catalog->second;
    }

private:
    std::unordered_map<std::string, LocalizationCatalog> catalogs_;
};

snt::core::Expected<std::shared_ptr<LocalizationService>> make_test_localization() {
    auto source = std::make_shared<TestCatalogSource>(
        std::unordered_map<std::string, LocalizationCatalog>{
            {"en", {
                .locale = "en",
                .messages = {
                    {"ui.inventory.title", "Inventory"},
                    {"ui.crafting.title", "Crafting"},
                    {"ui.crafting.recipe", "{item} x{count}  {availability}"},
                    {"ui.crafting.ready", "Craftable"},
                    {"ui.crafting.insufficient", "Missing materials"},
                    {"ui.crafting.craft", "Craft"},
                    {"ui.performance.title", "Performance"},
                    {"ui.performance.fps", "FPS  {fps}   Frame {frame_ms} ms"},
                    {"ui.performance.tps", "TPS  {tps}   MSPT {mspt}"},
                    {"ui.performance.workers", "Job Workers  {workers}"},
                },
            }},
            {"zh-Hans", {
                .locale = "zh-Hans",
                .messages = {
                    {"ui.inventory.title", "背包"},
                    {"ui.crafting.title", "合成"},
                    {"ui.crafting.recipe", "{item} x{count}  {availability}"},
                    {"ui.crafting.ready", "可合成"},
                    {"ui.crafting.insufficient", "材料不足"},
                    {"ui.crafting.craft", "合成"},
                    {"ui.performance.title", "性能"},
                    {"ui.performance.fps", "FPS  {fps}   帧时间 {frame_ms} ms"},
                    {"ui.performance.tps", "TPS  {tps}   每刻耗时 {mspt}"},
                    {"ui.performance.workers", "工作线程  {workers}"},
                    {"plank.oak", "橡木板"},
                    {"material.coal", "煤炭"},
                    {"stick", "木棍"},
                    {"workbench", "工作台"},
                    {"torch", "火把"},
                },
            }},
        });
    return LocalizationService::load(std::move(source),
                                     LocalizationLoadConfig{.locale = "zh-Hans", .fallback_locale = "en"});
}

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

UiFrameResult layout_and_paint(UiRuntime& runtime, View& root, Vec2 viewport) {
    runtime.layout(root, viewport);
    runtime.begin_input_frame({.pointer_enabled = false});
    runtime.synchronize_interaction_state(root);
    return runtime.paint(root);
}

void click_primary(UiRuntime& runtime, View& root, Vec2 point) {
    runtime.begin_input_frame({
        .pointer_position = point,
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);

    runtime.begin_input_frame({
        .pointer_position = point,
        .pointer_released = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);
}

}  // namespace

TEST(GameplayUi, InventoryScreenOpensAndRendersThroughArc2D) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_inventory();

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f}, **localization);
    ASSERT_NE(root->find("hotbar"), nullptr);
    ASSERT_NE(root->find("inventory_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = layout_and_paint(runtime, *root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "背包"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());

    const DrawTextCommand* title = first_text_command(frame.commands, "背包");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
}

TEST(GameplayUi, PerformancePanelUsesRetainedViewModel) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    PerformanceViewModel model;
    model.publish({
        .fps = 72.5f,
        .frame_ms = 13.79f,
        .tps = 20.0f,
        .mspt = 4.25f,
        .job_workers = 8,
    });

    auto panel = build_performance_panel_view(model, **localization);
    ASSERT_NE(panel->find("performance_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = layout_and_paint(runtime, *panel, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "性能"));
    EXPECT_TRUE(has_text_command(frame.commands, "FPS"));
    EXPECT_TRUE(has_text_command(frame.commands, "72.5"));
    EXPECT_TRUE(has_text_command(frame.commands, "工作线程"));
    EXPECT_FALSE(frame.draw_data.vertices.empty());
}

TEST(GameplayUi, CraftingScreenConsumesInputsAndProducesOutput) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
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

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f}, **localization);
    ASSERT_NE(root->find("crafting_panel"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    UiFrameResult frame = layout_and_paint(runtime, *root, {1280.0f, 720.0f});

    EXPECT_TRUE(has_text_command(frame.commands, "合成"));
    EXPECT_TRUE(has_text_command(frame.commands, "可合成"));

    const DrawTextCommand* title = first_text_command(frame.commands, "合成");
    ASSERT_NE(title, nullptr);
    EXPECT_TRUE(title->layout.contains_cjk);
}

TEST(GameplayUi, CraftButtonReceivesMouseActivationThroughMuiRuntime) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_crafting();

    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f}, **localization);
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(*root, {1280.0f, 720.0f});

    auto* craft = dynamic_cast<Button*>(root->find("recipe_button_craft_workbench"));
    ASSERT_NE(craft, nullptr);
    const Rect bounds = craft->bounds();
    click_primary(runtime, *root, {
        bounds.pos.x + bounds.size.x * 0.5f,
        bounds.pos.y + bounds.size.y * 0.5f,
    });

    EXPECT_EQ(controller.inventory().count_item("plank.oak"), 4);
    EXPECT_EQ(controller.inventory().count_item("workbench"), 1);
}
