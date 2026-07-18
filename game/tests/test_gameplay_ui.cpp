// Game-owned tests for ScienceAndTheology gameplay UI content.

#include "gameplay_ui.h"

#include "core/path_utils.h"
#include "game/localization/localization.h"
#include "ui/retained_mui_controls.h"
#include "ui/retained_mui_drag.h"
#include "ui/retained_mui_layout.h"
#include "ui/retained_mui_runtime.h"

#include <gtest/gtest.h>

#include <algorithm>
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
        .game_root = SNT_GAME_TEST_ROOT,
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

void drag_primary(UiRuntime& runtime, View& root, Vec2 source, Vec2 target) {
    runtime.begin_input_frame({
        .pointer_position = source,
        .pointer_held = {true, false, false},
        .pointer_pressed = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));

    runtime.begin_input_frame({
        .pointer_position = target,
        .pointer_held = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));

    runtime.begin_input_frame({
        .pointer_position = target,
        .pointer_released = {true, false, false},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);
}

void drag_secondary(UiRuntime& runtime, View& root, Vec2 source, Vec2 target) {
    runtime.begin_input_frame({
        .pointer_position = source,
        .pointer_held = {false, false, true},
        .pointer_pressed = {false, false, true},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));

    runtime.begin_input_frame({
        .pointer_position = target,
        .pointer_held = {false, false, true},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));

    runtime.begin_input_frame({
        .pointer_position = target,
        .pointer_released = {false, false, true},
    });
    ASSERT_TRUE(runtime.dispatch_pointer_input(root));
    runtime.synchronize_interaction_state(root);
}

}  // namespace

TEST(GameplayUi, LocalSlotAuthoritySplitsMergesSwapsAndRejectsStaleSnapshots) {
    InventoryState state;
    state.columns = 4;
    state.max_stack_size = 64;
    state.slots = {
        {"plank.oak", 8},
        {},
        {"plank.oak", 60},
        {"material.coal", 2},
    };
    LocalInventorySlotTransferAuthority authority(state);

    ASSERT_TRUE(authority.submit_slot_transfer({
        .request_id = 1,
        .expected_revision = 0,
        .source_slot = 0,
        .target_slot = 1,
        .count = 3,
        .expected_source = state.slots[0],
        .expected_target = state.slots[1],
    }));
    auto confirmations = authority.drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_EQ(confirmations.front().outcome, InventorySlotTransferOutcome::Accepted);
    EXPECT_EQ(confirmations.front().authoritative_revision, 1u);
    EXPECT_EQ(confirmations.front().slots[0], (ItemStackState{"plank.oak", 5}));
    EXPECT_EQ(confirmations.front().slots[1], (ItemStackState{"plank.oak", 3}));

    state.slots = confirmations.front().slots;
    ASSERT_TRUE(authority.submit_slot_transfer({
        .request_id = 2,
        .expected_revision = 1,
        .source_slot = 0,
        .target_slot = 2,
        .count = 4,
        .expected_source = state.slots[0],
        .expected_target = state.slots[2],
    }));
    confirmations = authority.drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_EQ(confirmations.front().outcome, InventorySlotTransferOutcome::Accepted);
    EXPECT_EQ(confirmations.front().slots[0], (ItemStackState{"plank.oak", 1}));
    EXPECT_EQ(confirmations.front().slots[2], (ItemStackState{"plank.oak", 64}));

    state.slots = confirmations.front().slots;
    ASSERT_TRUE(authority.submit_slot_transfer({
        .request_id = 3,
        .expected_revision = 2,
        .source_slot = 1,
        .target_slot = 3,
        .count = 3,
        .expected_source = state.slots[1],
        .expected_target = state.slots[3],
    }));
    confirmations = authority.drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_EQ(confirmations.front().outcome, InventorySlotTransferOutcome::Accepted);
    EXPECT_EQ(confirmations.front().slots[1], (ItemStackState{"material.coal", 2}));
    EXPECT_EQ(confirmations.front().slots[3], (ItemStackState{"plank.oak", 3}));

    ASSERT_TRUE(authority.submit_slot_transfer({
        .request_id = 4,
        .expected_revision = 0,
        .source_slot = 1,
        .target_slot = 3,
        .count = 2,
        .expected_source = {"material.coal", 2},
        .expected_target = {"plank.oak", 3},
    }));
    confirmations = authority.drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_EQ(confirmations.front().outcome, InventorySlotTransferOutcome::Rejected);
    EXPECT_EQ(confirmations.front().authoritative_revision, 3u);
    EXPECT_EQ(confirmations.front().slots[1], (ItemStackState{"material.coal", 2}));
    EXPECT_EQ(confirmations.front().slots[3], (ItemStackState{"plank.oak", 3}));
}

TEST(GameplayUi, SlotDragChangesPresentationOnlyAfterAuthorityConfirmation) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();

    InventoryState inventory;
    inventory.columns = 3;
    inventory.slots = {
        {"plank.oak", 8},
        {"material.coal", 2},
        {},
    };
    auto authority = std::make_shared<LocalInventorySlotTransferAuthority>(inventory);
    GameplayUiController controller{
        InventoryViewModel{inventory}, {}, authority,
    };
    controller.open_inventory();
    auto root = build_gameplay_ui_root(controller, {800.0f, 600.0f}, **localization);
    ASSERT_NE(root, nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(*root, {800.0f, 600.0f});
    auto* source = dynamic_cast<SlotView*>(root->find("inventory_slot_0"));
    auto* target = dynamic_cast<SlotView*>(root->find("inventory_slot_1"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);

    const Rect source_bounds = source->bounds();
    const Rect target_bounds = target->bounds();
    drag_primary(runtime, *root,
                 {source_bounds.pos.x + source_bounds.size.x * 0.5f,
                  source_bounds.pos.y + source_bounds.size.y * 0.5f},
                 {target_bounds.pos.x + target_bounds.size.x * 0.5f,
                  target_bounds.pos.y + target_bounds.size.y * 0.5f});

    EXPECT_TRUE(controller.inventory_slot_transfer_pending());
    EXPECT_EQ(controller.inventory().state().slots[0], (ItemStackState{"plank.oak", 8}));
    EXPECT_EQ(controller.inventory().state().slots[1], (ItemStackState{"material.coal", 2}));

    auto confirmations = authority->drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_TRUE(controller.apply_inventory_slot_transfer_confirmation(
        std::move(confirmations.front())));
    EXPECT_FALSE(controller.inventory_slot_transfer_pending());
    EXPECT_EQ(controller.inventory_authority_revision(), 1u);
    EXPECT_EQ(controller.inventory().state().slots[0], (ItemStackState{"material.coal", 2}));
    EXPECT_EQ(controller.inventory().state().slots[1], (ItemStackState{"plank.oak", 8}));
}

TEST(GameplayUi, SecondarySlotDragRequestsHalfStackSplit) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();

    InventoryState inventory;
    inventory.columns = 3;
    inventory.slots = {
        {"plank.oak", 7},
        {},
        {},
    };
    auto authority = std::make_shared<LocalInventorySlotTransferAuthority>(inventory);
    GameplayUiController controller{
        InventoryViewModel{inventory}, {}, authority,
    };
    controller.open_inventory();
    auto root = build_gameplay_ui_root(controller, {800.0f, 600.0f}, **localization);
    ASSERT_NE(root, nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    runtime.layout(*root, {800.0f, 600.0f});
    auto* source = dynamic_cast<SlotView*>(root->find("inventory_slot_0"));
    auto* target = dynamic_cast<SlotView*>(root->find("inventory_slot_1"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    const Rect source_bounds = source->bounds();
    const Rect target_bounds = target->bounds();
    drag_secondary(runtime, *root,
                   {source_bounds.pos.x + source_bounds.size.x * 0.5f,
                    source_bounds.pos.y + source_bounds.size.y * 0.5f},
                   {target_bounds.pos.x + target_bounds.size.x * 0.5f,
                    target_bounds.pos.y + target_bounds.size.y * 0.5f});

    auto confirmations = authority->drain_slot_transfer_confirmations();
    ASSERT_EQ(confirmations.size(), 1u);
    EXPECT_TRUE(controller.apply_inventory_slot_transfer_confirmation(
        std::move(confirmations.front())));
    EXPECT_EQ(controller.inventory().state().slots[0], (ItemStackState{"plank.oak", 3}));
    EXPECT_EQ(controller.inventory().state().slots[1], (ItemStackState{"plank.oak", 4}));
}

TEST(GameplayUi, ReplacesPresentationFromAuthoritativeNetworkSnapshot) {
    InventoryState initial;
    initial.columns = 3;
    initial.slots = {
        {"plank.oak", 8},
        {},
        {},
    };
    GameplayUiController controller{InventoryViewModel{initial}, {}};

    ASSERT_TRUE(controller.apply_inventory_authoritative_snapshot(
        {{"material.coal", 3}, {}, {"hammer", 1, "durability=97"}}, 64, 6));
    EXPECT_EQ(controller.inventory_authority_revision(), 6u);
    EXPECT_EQ(controller.inventory().state().slots[0], (ItemStackState{"material.coal", 3}));
    EXPECT_EQ(controller.inventory().state().slots[2],
              (ItemStackState{"hammer", 1, "durability=97"}));

    controller.clear_inventory_authority();
    EXPECT_EQ(controller.inventory_authority_revision(), 0u);
}

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

TEST(GameplayUi, PackagedItemImagesRegisterAndRenderInInventorySlots) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    auto registration = register_gameplay_ui_images(runtime.images(), *paths);
    ASSERT_TRUE(registration) << registration.error().format();
    EXPECT_EQ(runtime.images().image_count(), 5u);
    EXPECT_NE(runtime.images().resolve("item.missing"), nullptr);
    EXPECT_NE(runtime.images().resolve("plank.oak"), nullptr);

    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_inventory();
    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f}, **localization);

    const UiFrameResult frame = layout_and_paint(runtime, *root, {1280.0f, 720.0f});
    ASSERT_TRUE(frame.draw_data.image_atlas);
    EXPECT_GT(frame.draw_data.image_atlas->revision, 0u);
    EXPECT_TRUE(std::any_of(
        frame.draw_data.vertices.begin(), frame.draw_data.vertices.end(),
        [](const UiVertex& vertex) { return vertex.texture_mode == UiTextureMode::Image; }));
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

TEST(GameplayUi, PerformanceHudFactoryRetainsRootAndUpdatesExistingWidgets) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    PerformanceViewModel model;
    UiImageRegistry images;
    UiLayerStack layers;
    ASSERT_TRUE(layers.register_screen({
        .owner_id = "science_and_theology",
        .screen_id = "performance",
        .layer = UiLayer::Hud,
        .initially_visible = true,
        .factory = make_performance_ui_factory(model, **localization),
    }));

    const auto& first_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(first_frame.size(), 1u);
    View* const mounted_root = first_frame.front().root;
    ASSERT_NE(mounted_root, nullptr);

    model.publish({
        .fps = 72.5f,
        .frame_ms = 13.79f,
        .tps = 20.0f,
        .mspt = 4.25f,
        .job_workers = 8,
    });
    const auto& updated_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(updated_frame.size(), 1u);
    EXPECT_EQ(updated_frame.front().root, mounted_root);

    auto* root_group = dynamic_cast<ViewGroup*>(mounted_root);
    ASSERT_NE(root_group, nullptr);
    auto* fps = dynamic_cast<TextView*>(root_group->find("performance_fps"));
    ASSERT_NE(fps, nullptr);
    EXPECT_NE(fps->text().find("72.5"), std::string::npos);

    ASSERT_TRUE(layers.set_visible("science_and_theology", "performance", false));
    EXPECT_TRUE(layers.is_mounted("science_and_theology", "performance"));
    ASSERT_TRUE(layers.set_visible("science_and_theology", "performance", true));
    const auto& reopened_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(reopened_frame.size(), 1u);
    EXPECT_EQ(reopened_frame.front().root, mounted_root);
}

TEST(GameplayUi, GameplayFactoryKeepsCraftingScrollUntilModelChanges) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();
    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_crafting();

    UiImageRegistry images;
    UiLayerStack layers;
    ASSERT_TRUE(layers.register_screen({
        .owner_id = "science_and_theology",
        .screen_id = "gameplay",
        .layer = UiLayer::Screen,
        .initially_visible = true,
        .factory = make_gameplay_ui_factory(controller, **localization),
        .dispatch_action = [&controller](std::string_view action_id) {
            dispatch_gameplay_ui_action(controller, action_id);
        },
    }));

    const auto& first_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(first_frame.size(), 1u);
    auto* root = dynamic_cast<ViewGroup*>(first_frame.front().root);
    ASSERT_NE(root, nullptr);
    auto* first_scroll = dynamic_cast<ScrollView*>(root->find("crafting_recipe_scroll"));
    ASSERT_NE(first_scroll, nullptr);

    const auto& unchanged_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(unchanged_frame.size(), 1u);
    auto* unchanged_scroll = dynamic_cast<ScrollView*>(root->find("crafting_recipe_scroll"));
    EXPECT_EQ(unchanged_scroll, first_scroll);

    ASSERT_TRUE(controller.inventory().remove_item("plank.oak", 5));
    const auto& changed_frame = layers.prepare_frame({
        .viewport = {1280.0f, 720.0f},
        .images = images,
    });
    ASSERT_EQ(changed_frame.size(), 1u);
    auto* changed_scroll = dynamic_cast<ScrollView*>(root->find("crafting_recipe_scroll"));
    ASSERT_NE(changed_scroll, nullptr);
    EXPECT_NE(changed_scroll, first_scroll);

    auto* recipe_label = dynamic_cast<TextView*>(root->find("recipe_label_craft_workbench"));
    ASSERT_NE(recipe_label, nullptr);
    EXPECT_NE(recipe_label->text().find("材料不足"), std::string::npos);
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
