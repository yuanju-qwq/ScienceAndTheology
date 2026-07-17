// ScienceAndTheology gameplay UI implementation.

#define SNT_LOG_CHANNEL "game_ui"
#include "gameplay_ui.h"

#include "core/log.h"
#include "core/path_utils.h"
#include "game/localization/localization.h"
#include "ui/ui_packed_scene.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace snt::game {
using namespace snt::ui;

namespace {

constexpr float kSlotSize = 36.0f;
constexpr float kSlotGap = 2.0f;

LayoutParams fixed(float width, float height) {
    LayoutParams lp;
    lp.width = width;
    lp.height = height;
    return lp;
}

std::string format_float(const char* fmt, float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, value);
    return buf;
}

UiWidgetTemplate text_widget(std::string id, std::string text, float size = 16.0f) {
    UiWidgetTemplate widget;
    widget.type = UiWidgetType::Text;
    widget.id = std::move(id);
    widget.text = std::move(text);
    widget.text_style.size_px = size;
    widget.text_style.color = {230, 236, 245, 255};
    return widget;
}

UiWidgetTemplate slot_widget(std::string id, const ItemStackState& stack, bool selected,
                             bool interactive = true) {
    UiWidgetTemplate widget;
    widget.type = UiWidgetType::Slot;
    widget.id = std::move(id);
    widget.layout.params = fixed(kSlotSize, kSlotSize);
    widget.enabled = interactive;
    widget.slot = {
        .item_key = stack.item_key,
        .count = stack.count,
        .selected = selected,
    };
    return widget;
}

std::optional<uint32_t> inventory_slot_index(std::string_view id) {
    constexpr std::string_view kPrefixes[] = {
        "inventory_slot_",
        "hotbar_slot_",
    };
    for (const std::string_view prefix : kPrefixes) {
        if (!id.starts_with(prefix)) continue;
        const char* const first = id.data() + prefix.size();
        const char* const last = id.data() + id.size();
        uint32_t index = 0;
        const auto [next, error] = std::from_chars(first, last, index);
        if (error == std::errc{} && next == last) return index;
        return std::nullopt;
    }
    return std::nullopt;
}

void bind_inventory_slot_drag_handlers(View& view, GameplayUiController& controller) {
    if (auto* slot = dynamic_cast<SlotView*>(&view)) {
        if (inventory_slot_index(slot->id())) {
            const std::string source_id = slot->id();
            slot->set_drag_handler([&controller, source_id](const UiDragEvent& event) {
                // Both source and target receive Drop. Only the source emits
                // the transaction, so one retained drag yields one command.
                if (event.source_id != source_id) return;
                controller.handle_inventory_slot_drag(event);
            });
        }
    }
    if (auto* group = dynamic_cast<ViewGroup*>(&view)) {
        for (const std::unique_ptr<View>& child : group->children()) {
            bind_inventory_slot_drag_handlers(*child, controller);
        }
    }
}

UiWidgetTemplate& append_child(UiWidgetTemplate& parent, UiWidgetTemplate child) {
    parent.children.push_back(std::move(child));
    return parent.children.back();
}

std::unique_ptr<ViewGroup> instantiate_group(UiWidgetTemplate root,
                                             UiWidgetActionDispatcher dispatch_action = {}) {
    UiWidgetTree tree{.root = std::move(root)};
    auto instantiated = instantiate_ui_widget_tree(
        tree, {.dispatch_action = std::move(dispatch_action)});
    if (!instantiated) {
        SNT_LOG_ERROR("Gameplay WidgetTree instantiation failed: %s",
                      instantiated.error().format().c_str());
        return {};
    }

    std::unique_ptr<View> view = std::move(*instantiated);
    auto* group = dynamic_cast<ViewGroup*>(view.get());
    if (!group) {
        SNT_LOG_ERROR("Gameplay WidgetTree root is not a ViewGroup");
        return {};
    }
    view.release();
    return std::unique_ptr<ViewGroup>(group);
}

UiWidgetTemplate hotbar_template(const HotbarViewModel& model) {
    UiWidgetTemplate root;
    root.type = UiWidgetType::Flex;
    root.id = "hotbar";
    root.layout.orientation = Orientation::Horizontal;
    root.layout.spacing = kSlotGap;
    root.layout.padding = {4, 4, 4, 4};
    root.background = Color{12, 15, 20, 205};
    root.background_radius = 4.0f;

    const auto& inventory = model.inventory();
    const int32_t limit = std::min<int32_t>(9, static_cast<int32_t>(inventory.slots.size()));
    for (int32_t index = 0; index < limit; ++index) {
        append_child(root, slot_widget("hotbar_slot_" + std::to_string(index),
                                       inventory.slots[index],
                                       index == model.selected_index()));
    }
    return root;
}

UiWidgetTemplate inventory_template(const InventoryViewModel& model,
                                    const localization::LocalizationService& localization) {
    UiWidgetTemplate panel;
    panel.type = UiWidgetType::Flex;
    panel.id = "inventory_panel";
    panel.layout.orientation = Orientation::Vertical;
    panel.layout.spacing = 8.0f;
    panel.layout.padding = {10, 10, 10, 10};
    panel.background = Color{13, 15, 21, 235};
    panel.background_radius = 6.0f;
    panel.layout.params = fixed(430.0f, 250.0f);

    UiWidgetTemplate title = text_widget("inventory_title", localization.translate("ui.inventory.title"), 18.0f);
    title.layout.params = fixed(300.0f, 28.0f);
    append_child(panel, std::move(title));

    UiWidgetTemplate grid;
    grid.type = UiWidgetType::Grid;
    grid.id = "inventory_grid";
    grid.layout.columns = std::max(1, model.state().columns);
    grid.layout.column_spacing = kSlotGap;
    grid.layout.row_spacing = kSlotGap;
    for (int32_t index = 0; index < static_cast<int32_t>(model.state().slots.size()); ++index) {
        append_child(grid, slot_widget("inventory_slot_" + std::to_string(index),
                                       model.state().slots[index],
                                       index == model.state().selected_hotbar));
    }
    append_child(panel, std::move(grid));
    return panel;
}

UiWidgetTemplate crafting_template(const CraftingViewModel& model,
                                   const localization::LocalizationService& localization) {
    UiWidgetTemplate panel;
    panel.type = UiWidgetType::Flex;
    panel.id = "crafting_panel";
    panel.layout.orientation = Orientation::Vertical;
    panel.layout.spacing = 8.0f;
    panel.layout.padding = {10, 10, 10, 10};
    panel.background = Color{14, 16, 22, 238};
    panel.background_radius = 6.0f;
    panel.layout.params = fixed(520.0f, 310.0f);

    UiWidgetTemplate title = text_widget("crafting_title", localization.translate("ui.crafting.title"), 18.0f);
    title.layout.params = fixed(320.0f, 28.0f);
    append_child(panel, std::move(title));

    UiWidgetTemplate scroll;
    scroll.type = UiWidgetType::Scroll;
    scroll.id = "crafting_recipe_scroll";
    scroll.layout.params = fixed(500.0f, 244.0f);
    UiWidgetTemplate list;
    list.type = UiWidgetType::Flex;
    list.id = "crafting_recipe_list";
    list.layout.orientation = Orientation::Vertical;
    list.layout.spacing = 6.0f;

    for (const auto& recipe : model.recipes()) {
        UiWidgetTemplate row;
        row.type = UiWidgetType::Flex;
        row.id = "recipe_" + recipe.id;
        row.layout.orientation = Orientation::Horizontal;
        row.layout.spacing = 8.0f;
        row.layout.params = fixed(480.0f, 48.0f);
        append_child(row, slot_widget("recipe_output_" + recipe.id, recipe.output, false, false));

        const std::string output_name = localization.translate(recipe.output.item_key);
        const std::string output_count = std::to_string(recipe.output.count);
        const std::string availability = localization.translate(
            model.can_craft(recipe) ? "ui.crafting.ready" : "ui.crafting.insufficient");
        UiWidgetTemplate name = text_widget(
            "recipe_label_" + recipe.id,
            localization.format("ui.crafting.recipe",
                                {{"item", output_name}, {"count", output_count}, {"availability", availability}}),
            14.0f);
        name.layout.params = fixed(280.0f, 36.0f);
        append_child(row, std::move(name));

        UiWidgetTemplate craft;
        craft.type = UiWidgetType::Button;
        craft.id = "recipe_button_" + recipe.id;
        craft.text = localization.translate("ui.crafting.craft");
        craft.action_id = "craft:" + recipe.id;
        craft.layout.params = fixed(72.0f, 32.0f);
        append_child(row, std::move(craft));
        append_child(list, std::move(row));
    }
    append_child(scroll, std::move(list));
    append_child(panel, std::move(scroll));
    return panel;
}

void dispatch_crafting_action(CraftingViewModel& model, std::string_view action_id) {
    constexpr std::string_view kCraftActionPrefix = "craft:";
    if (!action_id.starts_with(kCraftActionPrefix)) {
        SNT_LOG_WARN("Craft UI received an unsupported WidgetTree action '%.*s'",
                     static_cast<int>(action_id.size()), action_id.data());
        return;
    }
    const std::string recipe_id(action_id.substr(kCraftActionPrefix.size()));
    const CraftedItemResult result = model.craft(recipe_id);
    if (!result.ok) {
        SNT_LOG_WARN("Craft UI rejected recipe '%s': %s",
                     recipe_id.c_str(), result.reason.c_str());
        return;
    }
    SNT_LOG_INFO("Craft UI completed recipe '%s' -> %s x%d",
                 recipe_id.c_str(), result.output.item_key.c_str(), result.output.count);
}

UiWidgetActionDispatcher crafting_actions(CraftingViewModel& model) {
    return [&model](std::string_view action_id) {
        dispatch_crafting_action(model, action_id);
    };
}

UiWidgetTemplate performance_panel_template(const PerformanceViewModel& model,
                                            const localization::LocalizationService& localization) {
    UiWidgetTemplate panel;
    panel.type = UiWidgetType::Flex;
    panel.id = "performance_panel";
    panel.layout.orientation = Orientation::Vertical;
    panel.layout.spacing = 3.0f;
    panel.layout.padding = {8, 8, 8, 8};
    panel.background = Color{10, 12, 16, 210};
    panel.background_radius = 5.0f;
    panel.layout.params = fixed(230.0f, 116.0f);
    panel.layout.params.margin.left = 12.0f;
    panel.layout.params.margin.top = 12.0f;

    const auto& stats = model.stats();
    UiWidgetTemplate title = text_widget("performance_title", localization.translate("ui.performance.title"), 14.0f);
    title.layout.params = fixed(210.0f, 20.0f);
    append_child(panel, std::move(title));

    const std::string fps_value = format_float("%.1f", stats.fps);
    const std::string frame_ms_value = format_float("%.2f", stats.frame_ms);
    UiWidgetTemplate fps = text_widget(
        "performance_fps",
        localization.format("ui.performance.fps", {{"fps", fps_value}, {"frame_ms", frame_ms_value}}),
        12.0f);
    fps.layout.params = fixed(210.0f, 18.0f);
    append_child(panel, std::move(fps));

    const std::string tps_value = format_float("%.1f", stats.tps);
    const std::string mspt_value = format_float("%.2f", stats.mspt);
    UiWidgetTemplate tps = text_widget(
        "performance_tps",
        localization.format("ui.performance.tps", {{"tps", tps_value}, {"mspt", mspt_value}}),
        12.0f);
    tps.layout.params = fixed(210.0f, 18.0f);
    append_child(panel, std::move(tps));

    UiWidgetTemplate jobs = text_widget(
        "performance_jobs",
        localization.format("ui.performance.workers", {{"workers", std::to_string(stats.job_workers)}}),
        12.0f);
    jobs.layout.params = fixed(210.0f, 18.0f);
    append_child(panel, std::move(jobs));

    UiWidgetTemplate bar_background;
    bar_background.type = UiWidgetType::View;
    bar_background.id = "performance_fps_bar_bg";
    bar_background.layout.params = fixed(210.0f, 5.0f);
    bar_background.background = Color{38, 44, 55, 230};
    bar_background.background_radius = 2.0f;
    append_child(panel, std::move(bar_background));

    const float fps_fraction = std::clamp(stats.fps / 120.0f, 0.0f, 1.0f);
    UiWidgetTemplate bar;
    bar.type = UiWidgetType::View;
    bar.id = "performance_fps_bar";
    bar.layout.params = fixed(std::max(2.0f, 210.0f * fps_fraction), 5.0f);
    bar.background = Color{86, 170, 120, 245};
    bar.background_radius = 2.0f;
    append_child(panel, std::move(bar));
    return panel;
}

UiWidgetTree performance_ui_widget_tree(const PerformanceViewModel& model,
                                        const localization::LocalizationService& localization) {
    UiWidgetTree tree;
    tree.root.type = UiWidgetType::Frame;
    tree.root.id = "performance_ui_root";
    append_child(tree.root, performance_panel_template(model, localization));
    return tree;
}

}  // namespace

PerformanceViewModel::PerformanceViewModel() {
    publish({});
    bindings_.set("performance.visible", visible_);
}

void PerformanceViewModel::publish(PerformanceStats stats) {
    const bool unchanged = stats_.fps == stats.fps &&
        stats_.frame_ms == stats.frame_ms &&
        stats_.tps == stats.tps &&
        stats_.mspt == stats.mspt &&
        stats_.job_workers == stats.job_workers;
    if (unchanged) return;
    stats_ = stats;
    ++revision_;
    bindings_.set("performance.fps", static_cast<double>(stats_.fps));
    bindings_.set("performance.frame_ms", static_cast<double>(stats_.frame_ms));
    bindings_.set("performance.tps", static_cast<double>(stats_.tps));
    bindings_.set("performance.mspt", static_cast<double>(stats_.mspt));
    bindings_.set("performance.job_workers", static_cast<int64_t>(stats_.job_workers));
}

void PerformanceViewModel::set_visible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    ++revision_;
    bindings_.set("performance.visible", visible_);
}

InventoryViewModel::InventoryViewModel(InventoryState state)
    : state_(std::move(state)) {
    publish();
}

int32_t InventoryViewModel::count_item(std::string_view item_key) const {
    int32_t total = 0;
    for (const auto& slot : state_.slots) {
        if (slot.item_key == item_key) {
            total += slot.count;
        }
    }
    return total;
}

bool InventoryViewModel::remove_item(std::string_view item_key, int32_t count) {
    if (count <= 0) return true;
    if (count_item(item_key) < count) return false;

    int32_t remaining = count;
    for (auto& slot : state_.slots) {
        if (slot.item_key != item_key || slot.count <= 0) continue;
        const int32_t take = std::min(slot.count, remaining);
        slot.count -= take;
        remaining -= take;
        if (slot.count <= 0) {
            slot = {};
        }
        if (remaining <= 0) break;
    }
    publish();
    return true;
}

bool InventoryViewModel::add_item(ItemStackState stack) {
    if (stack.empty()) return true;
    if (state_.max_stack_size <= 0 || (!stack.instance_data.empty() && stack.count != 1)) {
        SNT_LOG_WARN("Inventory rejected an invalid stack for '%s'", stack.item_key.c_str());
        return false;
    }

    InventoryState candidate = state_;
    if (stack.instance_data.empty()) {
        for (auto& slot : candidate.slots) {
            if (slot.item_key != stack.item_key || !slot.instance_data.empty() ||
                slot.count >= candidate.max_stack_size) {
                continue;
            }
            const int32_t capacity = candidate.max_stack_size - slot.count;
            const int32_t merged = std::min(capacity, stack.count);
            slot.count += merged;
            stack.count -= merged;
        }
    }
    for (auto& slot : candidate.slots) {
        if (stack.count == 0) break;
        if (slot.empty()) {
            const int32_t placed = std::min(candidate.max_stack_size, stack.count);
            slot = stack;
            slot.count = placed;
            stack.count -= placed;
        }
    }
    if (stack.count != 0) {
        SNT_LOG_WARN("Inventory is full; could not add item '%s'", stack.item_key.c_str());
        return false;
    }
    state_ = std::move(candidate);
    publish();
    return true;
}

bool InventoryViewModel::apply_authoritative_slots(std::vector<ItemStackState> slots,
                                                    int32_t max_stack_size) {
    if (max_stack_size <= 0) {
        SNT_LOG_ERROR("Inventory authority supplied an invalid stack limit %d", max_stack_size);
        return false;
    }
    for (const ItemStackState& stack : slots) {
        const bool empty = stack.item_key.empty() && stack.count == 0 &&
            stack.instance_data.empty();
        if (empty) continue;
        if (stack.item_key.empty() || stack.count <= 0 || stack.count > max_stack_size ||
            (!stack.instance_data.empty() && stack.count != 1)) {
            SNT_LOG_ERROR("Inventory authority supplied an invalid slot snapshot");
            return false;
        }
    }

    state_.slots = std::move(slots);
    state_.max_stack_size = max_stack_size;
    if (state_.slots.empty()) {
        state_.selected_hotbar = 0;
    } else {
        state_.selected_hotbar = std::clamp(
            state_.selected_hotbar, 0,
            std::min(8, static_cast<int32_t>(state_.slots.size()) - 1));
    }
    publish();
    return true;
}

void InventoryViewModel::publish() {
    ++revision_;
    bindings_.set("inventory.slot_count", static_cast<int64_t>(state_.slots.size()));
    bindings_.set("inventory.selected_hotbar", static_cast<int64_t>(state_.selected_hotbar));
    bindings_.set("inventory.max_stack_size", static_cast<int64_t>(state_.max_stack_size));
}

HotbarViewModel::HotbarViewModel(InventoryViewModel& inventory)
    : inventory_(inventory) {}

int32_t HotbarViewModel::selected_index() const {
    return inventory_.state().selected_hotbar;
}

void HotbarViewModel::select(int32_t index) {
    auto& inv = inventory_.mutable_state();
    if (inv.slots.empty()) return;
    inv.selected_hotbar = std::clamp(index, 0, std::min(8, static_cast<int32_t>(inv.slots.size()) - 1));
    inventory_.publish();
    bindings_.set("hotbar.selected", static_cast<int64_t>(inv.selected_hotbar));
}

const InventoryState& HotbarViewModel::inventory() const {
    return inventory_.state();
}

CraftingViewModel::CraftingViewModel(InventoryViewModel& inventory,
                                     std::vector<CraftingRecipeState> recipes)
    : inventory_(inventory) {
    set_recipes(std::move(recipes));
}

void CraftingViewModel::set_recipes(std::vector<CraftingRecipeState> recipes) {
    recipes_ = std::move(recipes);
    ++revision_;
    bindings_.set("crafting.recipe_count", static_cast<int64_t>(recipes_.size()));
}

bool CraftingViewModel::can_craft(const CraftingRecipeState& recipe) const {
    if (recipe.output.empty()) return false;
    for (const auto& input : recipe.inputs) {
        if (input.item_key.empty() || input.count <= 0) return false;
        if (inventory_.count_item(input.item_key) < input.count) return false;
    }
    return true;
}

CraftedItemResult CraftingViewModel::craft(std::string_view recipe_id) {
    auto it = std::find_if(recipes_.begin(), recipes_.end(),
                           [&](const CraftingRecipeState& r) { return r.id == recipe_id; });
    if (it == recipes_.end()) {
        return {.ok = false, .reason = "recipe not found"};
    }
    if (!can_craft(*it)) {
        return {.ok = false, .reason = "missing inputs"};
    }

    for (const auto& input : it->inputs) {
        if (!inventory_.remove_item(input.item_key, input.count)) {
            return {.ok = false, .reason = "input removal failed"};
        }
    }
    if (!inventory_.add_item(it->output)) {
        return {.ok = false, .reason = "output inventory full"};
    }

    ++revision_;
    bindings_.set("crafting.last_crafted", it->output.item_key);
    return {.ok = true, .output = it->output};
}

GameplayUiController::GameplayUiController(InventoryViewModel inventory,
                                           std::vector<CraftingRecipeState> recipes,
                                           std::shared_ptr<IInventorySlotTransferCommandSink>
                                               slot_transfer_sink)
    : inventory_(std::move(inventory)),
      hotbar_(inventory_),
      crafting_(inventory_, std::move(recipes)),
      slot_transfer_sink_(std::move(slot_transfer_sink)) {}

void GameplayUiController::open_inventory() {
    set_open_screen(GameplayUiScreen::Inventory);
}

void GameplayUiController::open_crafting() {
    set_open_screen(GameplayUiScreen::Crafting);
}

void GameplayUiController::close() {
    set_open_screen(GameplayUiScreen::None);
}

void GameplayUiController::toggle_inventory() {
    set_open_screen(inventory_open() ? GameplayUiScreen::None : GameplayUiScreen::Inventory);
}

void GameplayUiController::toggle_crafting() {
    set_open_screen(crafting_open() ? GameplayUiScreen::None : GameplayUiScreen::Crafting);
}

void GameplayUiController::handle_inventory_slot_drag(const UiDragEvent& event) {
    if (event.type != UiDragEventType::Drop) return;
    if (pending_slot_transfer_) {
        SNT_LOG_WARN("Inventory slot transfer ignored while request %llu is awaiting confirmation",
                     static_cast<unsigned long long>(pending_slot_transfer_->request_id));
        return;
    }
    if (!slot_transfer_sink_) {
        SNT_LOG_WARN("Inventory slot transfer has no authority command sink: source='%s' target='%s'",
                     event.source_id.c_str(), event.target_id.c_str());
        return;
    }

    const std::optional<uint32_t> source_slot = inventory_slot_index(event.source_id);
    const std::optional<uint32_t> target_slot = inventory_slot_index(event.target_id);
    if (!source_slot || !target_slot || *source_slot == *target_slot) {
        SNT_LOG_WARN("Inventory slot transfer has invalid stable slot IDs: source='%s' target='%s'",
                     event.source_id.c_str(), event.target_id.c_str());
        return;
    }
    const InventoryState& state = inventory_.state();
    if (*source_slot >= state.slots.size() || *target_slot >= state.slots.size()) {
        SNT_LOG_WARN("Inventory slot transfer references unavailable slots: source=%u target=%u count=%zu",
                     *source_slot, *target_slot, state.slots.size());
        return;
    }
    const ItemStackState& source = state.slots[*source_slot];
    if (source.empty() || event.payload.type != "snt.item" ||
        event.payload.resource_key != source.item_key || event.payload.count <= 0 ||
        event.payload.count > source.count) {
        SNT_LOG_WARN("Inventory slot transfer payload no longer matches source slot %u", *source_slot);
        return;
    }
    if (next_inventory_transfer_request_id_ == 0 ||
        next_inventory_transfer_request_id_ == std::numeric_limits<uint64_t>::max()) {
        SNT_LOG_ERROR("Inventory slot transfer request IDs are exhausted");
        return;
    }

    InventorySlotTransferRequest request{
        .request_id = next_inventory_transfer_request_id_,
        .expected_revision = inventory_authority_revision_,
        .source_slot = *source_slot,
        .target_slot = *target_slot,
        .count = event.payload.count,
        .expected_source = source,
        .expected_target = state.slots[*target_slot],
    };
    if (auto submitted = slot_transfer_sink_->submit_slot_transfer(request); !submitted) {
        SNT_LOG_WARN("Inventory slot transfer request %llu was not submitted: %s",
                     static_cast<unsigned long long>(request.request_id),
                     submitted.error().format().c_str());
        return;
    }

    pending_slot_transfer_ = request;
    ++next_inventory_transfer_request_id_;
    SNT_LOG_INFO("Inventory slot transfer requested id=%llu source=%u target=%u count=%d",
                 static_cast<unsigned long long>(request.request_id), request.source_slot,
                 request.target_slot, request.count);
}

bool GameplayUiController::apply_inventory_slot_transfer_confirmation(
    InventorySlotTransferConfirmation confirmation) {
    if (!pending_slot_transfer_ || confirmation.request_id != pending_slot_transfer_->request_id) {
        SNT_LOG_WARN("Inventory authority returned an unknown slot transfer confirmation id=%llu",
                     static_cast<unsigned long long>(confirmation.request_id));
        return false;
    }

    const InventorySlotTransferRequest request = *pending_slot_transfer_;
    pending_slot_transfer_.reset();
    const bool accepted = confirmation.outcome == InventorySlotTransferOutcome::Accepted;
    if ((accepted && confirmation.authoritative_revision <= request.expected_revision) ||
        (!accepted && confirmation.authoritative_revision < request.expected_revision)) {
        SNT_LOG_ERROR("Inventory authority returned a stale confirmation id=%llu revision=%llu",
                      static_cast<unsigned long long>(confirmation.request_id),
                      static_cast<unsigned long long>(confirmation.authoritative_revision));
        return false;
    }
    if (!inventory_.apply_authoritative_slots(std::move(confirmation.slots),
                                               confirmation.max_stack_size)) {
        SNT_LOG_ERROR("Inventory authority confirmation id=%llu has an invalid snapshot",
                      static_cast<unsigned long long>(confirmation.request_id));
        return false;
    }

    inventory_authority_revision_ = confirmation.authoritative_revision;
    if (accepted) {
        SNT_LOG_INFO("Inventory slot transfer confirmed id=%llu revision=%llu",
                     static_cast<unsigned long long>(confirmation.request_id),
                     static_cast<unsigned long long>(inventory_authority_revision_));
        return true;
    }
    SNT_LOG_WARN("Inventory slot transfer rejected id=%llu: %s",
                 static_cast<unsigned long long>(confirmation.request_id),
                 confirmation.rejection_reason.empty() ? "authority rejected request"
                                                      : confirmation.rejection_reason.c_str());
    return false;
}

void GameplayUiController::set_open_screen(GameplayUiScreen screen) {
    if (open_screen_ == screen) return;
    open_screen_ = screen;
    ++revision_;
}

std::unique_ptr<ViewGroup> build_hotbar_view(const HotbarViewModel& model) {
    return instantiate_group(hotbar_template(model));
}

std::unique_ptr<ViewGroup> build_inventory_view(
    const InventoryViewModel& model, const localization::LocalizationService& localization) {
    return instantiate_group(inventory_template(model, localization));
}

std::unique_ptr<ViewGroup> build_crafting_view(
    CraftingViewModel& model, const localization::LocalizationService& localization) {
    return instantiate_group(crafting_template(model, localization), crafting_actions(model));
}

std::unique_ptr<ViewGroup> build_performance_panel_view(
    const PerformanceViewModel& model, const localization::LocalizationService& localization) {
    return instantiate_group(performance_panel_template(model, localization));
}

UiWidgetTree build_gameplay_ui_widget_tree(
    GameplayUiController& controller,
    Vec2 viewport,
    const localization::LocalizationService& localization) {
    UiWidgetTree tree;
    UiWidgetTemplate& root = tree.root;
    root.type = UiWidgetType::Frame;
    root.id = "gameplay_ui_root";
    root.layout.params = fixed(viewport.x, viewport.y);

    UiWidgetTemplate& hotbar = append_child(root, hotbar_template(controller.hotbar()));
    hotbar.layout.params = fixed(9.0f * kSlotSize + 8.0f * kSlotGap + 8.0f,
                                 kSlotSize + 8.0f);
    hotbar.layout.params.margin.left = (viewport.x - hotbar.layout.params.width) * 0.5f;
    hotbar.layout.params.margin.top = viewport.y - hotbar.layout.params.height - 18.0f;

    if (controller.inventory_open()) {
        UiWidgetTemplate& inventory = append_child(
            root, inventory_template(controller.inventory(), localization));
        inventory.layout.params.margin.left =
            (viewport.x - inventory.layout.params.width) * 0.5f;
        inventory.layout.params.margin.top =
            (viewport.y - inventory.layout.params.height) * 0.5f;
    } else if (controller.crafting_open()) {
        UiWidgetTemplate& crafting = append_child(
            root, crafting_template(controller.crafting(), localization));
        crafting.layout.params.margin.left =
            (viewport.x - crafting.layout.params.width) * 0.5f;
        crafting.layout.params.margin.top =
            (viewport.y - crafting.layout.params.height) * 0.5f;
    }

    return tree;
}

namespace {

// This snapshot deliberately tracks only inputs that change the generated
// WidgetTree. A retained mount stays untouched on ordinary frames, including
// while a ScrollView owns local scroll state.
struct GameplayUiTreeState {
    uint64_t controller_revision = 0;
    uint64_t inventory_revision = 0;
    uint64_t crafting_revision = 0;
    Vec2 viewport{};

    bool operator==(const GameplayUiTreeState& other) const {
        return controller_revision == other.controller_revision &&
            inventory_revision == other.inventory_revision &&
            crafting_revision == other.crafting_revision &&
            viewport.x == other.viewport.x && viewport.y == other.viewport.y;
    }
};

GameplayUiTreeState gameplay_ui_tree_state(GameplayUiController& controller,
                                           Vec2 viewport) {
    return {
        .controller_revision = controller.revision(),
        .inventory_revision = controller.inventory().revision(),
        .crafting_revision = controller.crafting().revision(),
        .viewport = viewport,
    };
}

struct GameplayUiMountState {
    GameplayUiTreeState tree_state{};
    bool refresh_failure_logged = false;
};

snt::core::Expected<void> replace_gameplay_ui_children(
    View& mounted_root,
    GameplayUiController& controller,
    Vec2 viewport,
    const localization::LocalizationService& localization,
    const UiWidgetActionDispatcher& dispatch_action) {
    auto* target_root = dynamic_cast<ViewGroup*>(&mounted_root);
    if (!target_root) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Gameplay UI mount root is not a ViewGroup"};
    }

    auto candidate = instantiate_ui_widget_tree(
        build_gameplay_ui_widget_tree(controller, viewport, localization), {
            .dispatch_action = dispatch_action,
        });
    if (!candidate) return candidate.error();

    auto* candidate_root = dynamic_cast<ViewGroup*>(candidate->get());
    if (!candidate_root) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Gameplay UI WidgetTree root is not a ViewGroup"};
    }
    bind_inventory_slot_drag_handlers(*candidate_root, controller);

    target_root->set_layout_params(candidate_root->layout_params());
    std::vector<std::unique_ptr<View>> children = std::move(candidate_root->children());
    target_root->clear_children();
    for (std::unique_ptr<View>& child : children) {
        target_root->add_child(std::move(child));
    }
    return {};
}

struct PerformanceUiMountState {
    uint64_t model_revision = 0;
    TextView* fps = nullptr;
    TextView* tps = nullptr;
    TextView* workers = nullptr;
    View* fps_bar = nullptr;
};

snt::core::Expected<void> bind_performance_panel(
    View& mounted_root,
    PerformanceUiMountState& state) {
    auto* root = dynamic_cast<ViewGroup*>(&mounted_root);
    if (!root) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Performance UI mount root is not a ViewGroup"};
    }
    state.fps = dynamic_cast<TextView*>(root->find("performance_fps"));
    state.tps = dynamic_cast<TextView*>(root->find("performance_tps"));
    state.workers = dynamic_cast<TextView*>(root->find("performance_jobs"));
    state.fps_bar = root->find("performance_fps_bar");
    if (!state.fps || !state.tps || !state.workers || !state.fps_bar) {
        return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                "Performance UI mount is missing a required widget"};
    }
    return {};
}

void refresh_performance_panel(PerformanceUiMountState& state,
                               const PerformanceViewModel& model,
                               const localization::LocalizationService& localization) {
    const PerformanceStats& stats = model.stats();
    const std::string fps_value = format_float("%.1f", stats.fps);
    const std::string frame_ms_value = format_float("%.2f", stats.frame_ms);
    state.fps->set_text(localization.format(
        "ui.performance.fps", {{"fps", fps_value}, {"frame_ms", frame_ms_value}}));

    const std::string tps_value = format_float("%.1f", stats.tps);
    const std::string mspt_value = format_float("%.2f", stats.mspt);
    state.tps->set_text(localization.format(
        "ui.performance.tps", {{"tps", tps_value}, {"mspt", mspt_value}}));
    state.workers->set_text(localization.format(
        "ui.performance.workers", {{"workers", std::to_string(stats.job_workers)}}));

    const float bar_width = std::max(2.0f, 210.0f * std::clamp(stats.fps / 120.0f, 0.0f, 1.0f));
    LayoutParams params = state.fps_bar->layout_params();
    if (params.width != bar_width) {
        params.width = bar_width;
        state.fps_bar->set_layout_params(params);
    }
}

}  // namespace

void dispatch_gameplay_ui_action(GameplayUiController& controller,
                                 std::string_view action_id) {
    dispatch_crafting_action(controller.crafting(), action_id);
}

UiScreenFactory make_gameplay_ui_factory(
    GameplayUiController& controller,
    const localization::LocalizationService& localization) {
    return [&controller, &localization](const UiScreenMountContext& context)
        -> snt::core::Expected<UiScreenMount> {
        const Vec2 viewport = context.viewport;
        UiWidgetActionDispatcher dispatch_action = context.dispatch_action;
        auto root = instantiate_ui_widget_tree(
            build_gameplay_ui_widget_tree(controller, viewport, localization), {
                .dispatch_action = dispatch_action,
            });
        if (!root) return root.error();
        if (!dynamic_cast<ViewGroup*>(root->get())) {
            return snt::core::Error{snt::core::ErrorCode::kInvalidState,
                                    "Gameplay UI WidgetTree root is not a ViewGroup"};
        }
        bind_inventory_slot_drag_handlers(**root, controller);

        auto state = std::make_shared<GameplayUiMountState>();
        state->tree_state = gameplay_ui_tree_state(controller, viewport);
        return UiScreenMount{
            .root = std::move(*root),
            .update = [&controller, &localization, dispatch_action = std::move(dispatch_action), state](
                          View& mounted_root,
                          const UiScreenFrameContext& frame_context) {
                const GameplayUiTreeState next_state = gameplay_ui_tree_state(
                    controller, frame_context.viewport);
                if (next_state == state->tree_state) return;

                auto refreshed = replace_gameplay_ui_children(
                    mounted_root, controller, frame_context.viewport, localization, dispatch_action);
                if (!refreshed) {
                    if (!state->refresh_failure_logged) {
                        SNT_LOG_ERROR("Gameplay retained UI refresh failed: %s",
                                      refreshed.error().format().c_str());
                        state->refresh_failure_logged = true;
                    }
                    return;
                }
                state->tree_state = next_state;
                state->refresh_failure_logged = false;
            },
        };
    };
}

UiScreenFactory make_performance_ui_factory(
    PerformanceViewModel& model,
    const localization::LocalizationService& localization) {
    return [&model, &localization](const UiScreenMountContext&)
        -> snt::core::Expected<UiScreenMount> {
        auto root = instantiate_ui_widget_tree(performance_ui_widget_tree(model, localization));
        if (!root) return root.error();

        auto state = std::make_shared<PerformanceUiMountState>();
        if (auto binding = bind_performance_panel(**root, *state); !binding) {
            return binding.error();
        }
        state->model_revision = model.revision();
        return UiScreenMount{
            .root = std::move(*root),
            .update = [&model, &localization, state](View&,
                                                      const UiScreenFrameContext&) {
                if (state->model_revision == model.revision()) return;
                refresh_performance_panel(*state, model, localization);
                state->model_revision = model.revision();
            },
        };
    };
}

std::unique_ptr<ViewGroup> build_gameplay_ui_root(GameplayUiController& controller,
                                                  Vec2 viewport,
                                                  const localization::LocalizationService& localization) {
    UiWidgetTree tree = build_gameplay_ui_widget_tree(controller, viewport, localization);
    auto root = instantiate_group(std::move(tree.root), crafting_actions(controller.crafting()));
    if (root) bind_inventory_slot_drag_handlers(*root, controller);
    return root;
}

snt::core::Expected<void> register_gameplay_ui_images(
    UiImageRegistry& images,
    const snt::core::RuntimePathResolver& paths) {
    struct ImageRegistration {
        const char* key;
        const char* relative_path;
    };
    constexpr ImageRegistration kImages[] = {
        {"item.missing", "resource/items/missing_icon_32.png"},
        {"plank.oak", "resource/items/materials/wood_plank_icon_32.png"},
        {"material.coal", "resource/items/tfc/charcoal_icon_32.png"},
        {"stick", "resource/items/materials/stick_icon_32.png"},
        {"workbench", "resource/items/placeables/workbench_icon_32.png"},
    };

    for (const ImageRegistration& image : kImages) {
        if (auto result = images.register_file(image.key, paths.resolve_game(image.relative_path)); !result) {
            auto error = result.error();
            error.with_context("register_gameplay_ui_images");
            return error;
        }
    }
    if (auto result = images.set_fallback("item.missing"); !result) {
        return result.error();
    }
    SNT_LOG_INFO("Gameplay UI image catalog registered (%zu images)", std::size(kImages));
    return {};
}

std::vector<CraftingRecipeState> make_starting_crafting_recipes() {
    return {
        {
            .id = "craft_workbench",
            .category = "tools",
            .inputs = {{"plank.oak", 4}},
            .output = {"workbench", 1},
        },
        {
            .id = "craft_torch",
            .category = "misc",
            .inputs = {{"material.coal", 1}, {"stick", 1}},
            .output = {"torch", 4},
        },
    };
}

InventoryState make_starting_inventory() {
    InventoryState state;
    state.columns = 9;
    state.selected_hotbar = 0;
    state.slots.resize(36);
    state.slots[0] = {"plank.oak", 8};
    state.slots[1] = {"material.coal", 2};
    state.slots[2] = {"stick", 4};
    return state;
}

}  // namespace snt::game
