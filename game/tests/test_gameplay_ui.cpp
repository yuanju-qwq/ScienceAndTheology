// Game-owned tests for ScienceAndTheology gameplay UI content.

#include "gameplay_ui.h"

#include "core/path_utils.h"
#include "game/client/game_content_registry.h"
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
#include <vector>

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
                    {"ui.automation.title", "Automation Controller"},
                    {"ui.automation.online", "Online  {nodes} nodes  {connections} links"},
                    {"ui.automation.offline", "Offline  {nodes} nodes  {connections} links"},
                    {"ui.automation.empty", "No flow nodes"},
                    {"ui.automation.interval", "#{id} Every {ticks} ticks"},
                    {"ui.automation.transfer", "#{id} {source} -> {destination} {resource} x{amount}"},
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
                    {"ui.automation.title", "自动化控制器"},
                    {"ui.automation.online", "在线  {nodes} 个节点  {connections} 条连接"},
                    {"ui.automation.offline", "离线  {nodes} 个节点  {connections} 条连接"},
                    {"ui.automation.empty", "没有流图节点"},
                    {"ui.automation.interval", "#{id} 每 {ticks} tick"},
                    {"ui.automation.transfer", "#{id} {source} -> {destination} {resource} x{amount}"},
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

class RecordingMachineInputSlotTransferSink final
    : public IMachineInputSlotTransferCommandSink {
public:
    snt::core::Expected<void> submit_machine_input_slot_transfer(
        MachineInputSlotTransferRequest request) override {
        requests.push_back(std::move(request));
        return {};
    }

    std::vector<MachineInputSlotTransferRequest> requests;
};

GameItemDefinition make_presented_test_item(std::string id,
                                            std::string icon_path,
                                            std::string overlay_path,
                                            uint32_t tint_rgb) {
    return {
        .id = std::move(id),
        .title_key = "item.test.presented",
        .max_stack = 64,
        .presentation = {
            .category = GameItemCategory::kMaterials,
            .icon_path = std::move(icon_path),
            .icon_overlay_path = std::move(overlay_path),
            .tint_rgb = tint_rgb,
            .uses_tint = true,
        },
    };
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

TEST(GameplayUi, MachineInputSlotDragWaitsForTypedAuthorityConfirmation) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();

    InventoryState inventory;
    inventory.columns = 3;
    inventory.max_stack_size = 64;
    inventory.slots = {
        {"iron_ore", 5},
        {},
        {},
    };
    auto sink = std::make_shared<RecordingMachineInputSlotTransferSink>();
    GameplayUiController controller{
        InventoryViewModel{inventory}, {}, {}, sink,
    };
    ASSERT_TRUE(controller.apply_inventory_authoritative_snapshot(inventory.slots, 64, 11));

    MachinePanelState machine{
        .dimension_id = "overworld",
        .root_x = 4,
        .root_y = 8,
        .root_z = -2,
        .expected_material = 10,
        .machine_id = "furnace",
        .input_slots = {{"iron_ore", 2}},
        .max_input_slots = 2,
        .max_output_slots = 1,
    };
    controller.open_machine(machine);
    ASSERT_TRUE(controller.machine_open());

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    auto root = build_gameplay_ui_root(controller, {800.0f, 600.0f}, **localization);
    ASSERT_NE(root, nullptr);
    runtime.layout(*root, {800.0f, 600.0f});
    auto* player_source = dynamic_cast<SlotView*>(root->find("inventory_slot_0"));
    auto* machine_target = dynamic_cast<SlotView*>(root->find("machine_input_slot_1"));
    ASSERT_NE(player_source, nullptr);
    ASSERT_NE(machine_target, nullptr);
    const Rect player_bounds = player_source->bounds();
    const Rect machine_bounds = machine_target->bounds();
    drag_primary(runtime, *root,
                 {player_bounds.pos.x + player_bounds.size.x * 0.5f,
                  player_bounds.pos.y + player_bounds.size.y * 0.5f},
                 {machine_bounds.pos.x + machine_bounds.size.x * 0.5f,
                  machine_bounds.pos.y + machine_bounds.size.y * 0.5f});

    ASSERT_EQ(sink->requests.size(), 1u);
    const MachineInputSlotTransferRequest& to_machine = sink->requests.front();
    EXPECT_EQ(to_machine.direction, MachineInputSlotTransferDirection::PlayerToMachineInput);
    EXPECT_EQ(to_machine.expected_inventory_revision, 11u);
    EXPECT_EQ(to_machine.player_slot, 0u);
    EXPECT_EQ(to_machine.machine_input_slot, 1u);
    EXPECT_EQ(to_machine.count, 5);
    EXPECT_EQ(to_machine.expected_player_slot, (ItemStackState{"iron_ore", 5}));
    EXPECT_TRUE(to_machine.expected_machine_input_slot.empty());
    EXPECT_TRUE(controller.machine_input_slot_transfer_pending());
    EXPECT_EQ(controller.inventory().state().slots[0], (ItemStackState{"iron_ore", 5}));
    ASSERT_NE(controller.machine_panel().state(), nullptr);
    EXPECT_EQ(controller.machine_panel().state()->input_slots.size(), 1u);

    EXPECT_TRUE(controller.apply_machine_input_slot_transfer_confirmation({
        .request_id = to_machine.request_id,
        .outcome = InventorySlotTransferOutcome::Accepted,
        .authoritative_inventory_revision = 12,
        .inventory_slots = {{}, {}, {}},
        .inventory_max_stack_size = 64,
    }));
    EXPECT_FALSE(controller.machine_input_slot_transfer_pending());
    EXPECT_TRUE(controller.inventory().state().slots[0].empty());

    machine.input_slots = {{"iron_ore", 2}, {"iron_ore", 5}};
    controller.open_machine(machine);
    root = build_gameplay_ui_root(controller, {800.0f, 600.0f}, **localization);
    ASSERT_NE(root, nullptr);
    runtime.layout(*root, {800.0f, 600.0f});
    auto* machine_source = dynamic_cast<SlotView*>(root->find("machine_input_slot_1"));
    auto* player_target = dynamic_cast<SlotView*>(root->find("inventory_slot_1"));
    ASSERT_NE(machine_source, nullptr);
    ASSERT_NE(player_target, nullptr);
    const Rect machine_source_bounds = machine_source->bounds();
    const Rect player_target_bounds = player_target->bounds();
    drag_primary(runtime, *root,
                 {machine_source_bounds.pos.x + machine_source_bounds.size.x * 0.5f,
                  machine_source_bounds.pos.y + machine_source_bounds.size.y * 0.5f},
                 {player_target_bounds.pos.x + player_target_bounds.size.x * 0.5f,
                  player_target_bounds.pos.y + player_target_bounds.size.y * 0.5f});

    ASSERT_EQ(sink->requests.size(), 2u);
    const MachineInputSlotTransferRequest& to_player = sink->requests.back();
    EXPECT_EQ(to_player.direction, MachineInputSlotTransferDirection::MachineInputToPlayer);
    EXPECT_EQ(to_player.expected_inventory_revision, 12u);
    EXPECT_EQ(to_player.player_slot, 1u);
    EXPECT_EQ(to_player.machine_input_slot, 1u);
    EXPECT_EQ(to_player.count, 5);
    EXPECT_TRUE(to_player.expected_player_slot.empty());
    EXPECT_EQ(to_player.expected_machine_input_slot, (ItemStackState{"iron_ore", 5}));
    EXPECT_TRUE(controller.machine_input_slot_transfer_pending());

    EXPECT_FALSE(controller.apply_machine_input_slot_transfer_confirmation({
        .request_id = to_player.request_id,
        .outcome = InventorySlotTransferOutcome::Rejected,
        .authoritative_inventory_revision = 12,
        .inventory_slots = {{}, {}, {}},
        .inventory_max_stack_size = 64,
        .rejection_reason = "machine changed",
    }));
    EXPECT_FALSE(controller.machine_input_slot_transfer_pending());
    EXPECT_TRUE(controller.inventory().state().slots[1].empty());
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
    EXPECT_EQ(runtime.images().image_count(), 1u);
    EXPECT_NE(runtime.images().resolve("item.missing"), nullptr);

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

TEST(GameplayUi, ContentPresentationKeepsSemanticDragKeysAndRefreshesRetainedSlots) {
    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();

    GameContentRegistry content;
    ASSERT_TRUE(content.register_script_item(
        42, make_presented_test_item("test.item", "material_sets/generic/ingot_base_32.png",
                                     "material_sets/generic/ingot_overlay_32.png", 0xc8b0a0u)));

    InventoryState inventory;
    inventory.columns = 1;
    inventory.slots = {{"test.item", 3}};
    GameplayUiController controller{
        InventoryViewModel{inventory}, {}, {}, {}, &content, &*paths,
    };
    controller.open_inventory();

    UiImageRegistry images;
    UiLayerStack layers;
    ASSERT_TRUE(layers.register_screen({
        .owner_id = "science_and_theology",
        .screen_id = "gameplay",
        .layer = UiLayer::Screen,
        .initially_visible = true,
        .factory = make_gameplay_ui_factory(controller, **localization),
    }));

    const auto& initial_frame = layers.prepare_frame({
        .viewport = {800.0f, 600.0f},
        .images = images,
    });
    ASSERT_EQ(initial_frame.size(), 1u);
    auto* root = dynamic_cast<ViewGroup*>(initial_frame.front().root);
    ASSERT_NE(root, nullptr);
    auto* slot = dynamic_cast<SlotView*>(root->find("inventory_slot_0"));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->slot_state().item_key, "test.item");
    EXPECT_EQ(slot->slot_state().image_key,
              "game.item.asset:material_sets/generic/ingot_base_32.png");
    EXPECT_EQ(slot->slot_state().overlay_image_key,
              "game.item.asset:material_sets/generic/ingot_overlay_32.png");
    EXPECT_EQ(slot->slot_state().image_tint.r, 0xc8u);
    EXPECT_EQ(slot->slot_state().image_tint.g, 0xb0u);
    EXPECT_EQ(slot->slot_state().image_tint.b, 0xa0u);
    EXPECT_NE(images.resolve(slot->slot_state().image_key), nullptr);
    EXPECT_NE(images.resolve(slot->slot_state().overlay_image_key), nullptr);

    const uint64_t initial_content_revision = content.item_content_revision();
    ASSERT_TRUE(content.set_script_item_presentation(
        42, "test.item", {
            .category = GameItemCategory::kMaterials,
            .icon_path = "material_sets/generic/dust_base_32.png",
            .tint_rgb = 0x20a0ffu,
            .uses_tint = true,
        }));
    EXPECT_GT(content.item_content_revision(), initial_content_revision);

    const auto& refreshed_frame = layers.prepare_frame({
        .viewport = {800.0f, 600.0f},
        .images = images,
    });
    ASSERT_EQ(refreshed_frame.size(), 1u);
    EXPECT_EQ(refreshed_frame.front().root, root);
    slot = dynamic_cast<SlotView*>(root->find("inventory_slot_0"));
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->slot_state().item_key, "test.item");
    EXPECT_EQ(slot->slot_state().image_key,
              "game.item.asset:material_sets/generic/dust_base_32.png");
    EXPECT_TRUE(slot->slot_state().overlay_image_key.empty());
    EXPECT_EQ(slot->slot_state().image_tint.r, 0x20u);
    EXPECT_EQ(slot->slot_state().image_tint.g, 0xa0u);
    EXPECT_EQ(slot->slot_state().image_tint.b, 0xffu);
    EXPECT_NE(images.resolve(slot->slot_state().image_key), nullptr);
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

TEST(GameplayUi, AutomationControllerPanelUsesOnlyStablePresentationValues) {
    const auto localization = make_test_localization();
    ASSERT_TRUE(localization) << localization.error().format();

    AutomationControllerPanelState state{
        .dimension_id = "overworld",
        .root_x = 3,
        .root_y = 7,
        .root_z = -2,
        .anchor_entity_id = 42,
        .controller_key = "automation.sfm_manager",
        .authoritative_revision = 8,
        .online = true,
        .nodes = {
            {.id = 1, .type = SfmFlowNodeType::kInterval, .interval_ticks = 20},
            {.id = 2,
             .type = SfmFlowNodeType::kTransfer,
             .source_endpoint = "world:chest_a",
             .destination_endpoint = "world:chest_b",
             .requested = ResourceContentStack::item("iron.ingot", 5)},
        },
        .connections = {{.source = 1, .destination = 2}},
    };
    AutomationControllerPanelViewModel model;
    ASSERT_TRUE(model.apply_authoritative_state(state));
    AutomationControllerPanelState stale = state;
    stale.authoritative_revision = 7;
    EXPECT_FALSE(model.apply_authoritative_state(std::move(stale)));

    GameplayUiController controller{
        InventoryViewModel{make_starting_inventory()},
        make_starting_crafting_recipes(),
    };
    controller.open_automation_controller(std::move(state));
    EXPECT_TRUE(controller.automation_controller_open());
    auto root = build_gameplay_ui_root(controller, {1280.0f, 720.0f}, **localization);
    ASSERT_NE(root->find("automation_controller_panel"), nullptr);
    ASSERT_NE(root->find("automation_node_1"), nullptr);
    ASSERT_NE(root->find("automation_node_2"), nullptr);

    auto paths = make_test_path_resolver();
    ASSERT_TRUE(paths) << paths.error().format();
    UiRuntime runtime(*paths);
    const UiFrameResult frame = layout_and_paint(runtime, *root, {1280.0f, 720.0f});
    EXPECT_TRUE(has_text_command(frame.commands, "自动化控制器"));
    EXPECT_TRUE(has_text_command(frame.commands, "world:chest_a"));

    controller.clear_automation_controller_authority();
    EXPECT_FALSE(controller.automation_controller_open());
}
