// ScienceAndTheology gameplay UI state and WidgetTree builders.
//
// Ownership: ScienceAndTheologyClientSession owns these models for one game
// session. They depend on snt::ui only for generic retained-MUI primitives;
// inventory, recipes, labels, layout choices, and demo content remain game
// content and must not be moved back into the engine.
//
// Thread affinity: mutate and build views on the game/runtime main thread.
// The models do not retain ECS, script, or Vulkan pointers.

#pragma once

#include "ui/ui_packed_scene.h"
#include "core/expected.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::game::localization {
class LocalizationService;
}
namespace snt::core { class RuntimePathResolver; }

namespace snt::game {

struct ItemStackState {
    std::string item_key;
    int32_t count = 0;

    bool empty() const { return item_key.empty() || count <= 0; }
};

struct InventoryState {
    std::vector<ItemStackState> slots;
    int32_t columns = 9;
    int32_t selected_hotbar = 0;
};

struct RecipeIngredientState {
    std::string item_key;
    int32_t count = 0;
};

struct CraftingRecipeState {
    std::string id;
    std::string category;
    std::vector<RecipeIngredientState> inputs;
    ItemStackState output;
};

struct CraftedItemResult {
    bool ok = false;
    ItemStackState output;
    std::string reason;
};

struct PerformanceStats {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float tps = 0.0f;
    float mspt = 0.0f;
    int32_t job_workers = 0;
};

class PerformanceViewModel {
public:
    PerformanceViewModel();

    void publish(PerformanceStats stats);
    const PerformanceStats& stats() const { return stats_; }

    void set_visible(bool visible);
    void toggle_visible() { set_visible(!visible_); }
    bool visible() const { return visible_; }
    uint64_t revision() const noexcept { return revision_; }

    snt::ui::ViewModel& bindings() { return bindings_; }
    const snt::ui::ViewModel& bindings() const { return bindings_; }

private:
    PerformanceStats stats_{};
    bool visible_ = true;
    snt::ui::ViewModel bindings_;
    uint64_t revision_ = 0;
};

class InventoryViewModel {
public:
    explicit InventoryViewModel(InventoryState state = {});

    const InventoryState& state() const { return state_; }
    InventoryState& mutable_state() { return state_; }

    int32_t count_item(std::string_view item_key) const;
    bool remove_item(std::string_view item_key, int32_t count);
    bool add_item(ItemStackState stack);

    snt::ui::ViewModel& bindings() { return bindings_; }
    const snt::ui::ViewModel& bindings() const { return bindings_; }
    uint64_t revision() const noexcept { return revision_; }
    void publish();

private:
    InventoryState state_{};
    snt::ui::ViewModel bindings_;
    uint64_t revision_ = 0;
};

class HotbarViewModel {
public:
    explicit HotbarViewModel(InventoryViewModel& inventory);

    int32_t selected_index() const;
    void select(int32_t index);
    const InventoryState& inventory() const;
    snt::ui::ViewModel& bindings() { return bindings_; }

private:
    InventoryViewModel& inventory_;
    snt::ui::ViewModel bindings_;
};

class CraftingViewModel {
public:
    CraftingViewModel(InventoryViewModel& inventory,
                      std::vector<CraftingRecipeState> recipes = {});

    const std::vector<CraftingRecipeState>& recipes() const { return recipes_; }
    void set_recipes(std::vector<CraftingRecipeState> recipes);
    bool can_craft(const CraftingRecipeState& recipe) const;
    CraftedItemResult craft(std::string_view recipe_id);
    snt::ui::ViewModel& bindings() { return bindings_; }
    uint64_t revision() const noexcept { return revision_; }

private:
    InventoryViewModel& inventory_;
    std::vector<CraftingRecipeState> recipes_;
    snt::ui::ViewModel bindings_;
    uint64_t revision_ = 0;
};

enum class GameplayUiScreen : uint8_t {
    None,
    Inventory,
    Crafting,
};

class GameplayUiController {
public:
    GameplayUiController(InventoryViewModel inventory,
                         std::vector<CraftingRecipeState> recipes);

    InventoryViewModel& inventory() { return inventory_; }
    HotbarViewModel& hotbar() { return hotbar_; }
    CraftingViewModel& crafting() { return crafting_; }

    GameplayUiScreen open_screen() const { return open_screen_; }
    bool inventory_open() const { return open_screen_ == GameplayUiScreen::Inventory; }
    bool crafting_open() const { return open_screen_ == GameplayUiScreen::Crafting; }
    uint64_t revision() const noexcept { return revision_; }

    void open_inventory();
    void open_crafting();
    void close();
    void toggle_inventory();
    void toggle_crafting();

private:
    void set_open_screen(GameplayUiScreen screen);

    InventoryViewModel inventory_;
    HotbarViewModel hotbar_;
    CraftingViewModel crafting_;
    GameplayUiScreen open_screen_ = GameplayUiScreen::None;
    uint64_t revision_ = 0;
};

std::unique_ptr<snt::ui::ViewGroup> build_hotbar_view(const HotbarViewModel& model);
std::unique_ptr<snt::ui::ViewGroup> build_inventory_view(
    const InventoryViewModel& model, const localization::LocalizationService& localization);
std::unique_ptr<snt::ui::ViewGroup> build_crafting_view(
    CraftingViewModel& model, const localization::LocalizationService& localization);
std::unique_ptr<snt::ui::ViewGroup> build_performance_panel_view(
    const PerformanceViewModel& model, const localization::LocalizationService& localization);

std::unique_ptr<snt::ui::ViewGroup> build_gameplay_ui_root(GameplayUiController& controller,
                                                            snt::ui::Vec2 viewport,
                                                            const localization::LocalizationService& localization);

// The client layer stack consumes this canonical tree directly. The
// retained-MUI mounting layer owns the resulting View objects and preserves
// their state while this tree identity remains stable.
[[nodiscard]] snt::ui::UiWidgetTree build_gameplay_ui_widget_tree(
    GameplayUiController& controller,
    snt::ui::Vec2 viewport,
    const localization::LocalizationService& localization);

// Host-owned dispatch boundary for declarative action IDs in the tree.
void dispatch_gameplay_ui_action(GameplayUiController& controller,
                                 std::string_view action_id);

// Dynamic Retained-MUI entry point. The factory instantiates the shared
// WidgetTree once, then its updater replaces children only after a gameplay
// UI model revision, open-screen state, or viewport change.
[[nodiscard]] snt::ui::UiScreenFactory make_gameplay_ui_factory(
    GameplayUiController& controller,
    const localization::LocalizationService& localization);

// The performance overlay is a HUD-layer retained screen. Its updater changes
// existing text and bar views instead of allocating a new panel every frame.
[[nodiscard]] snt::ui::UiScreenFactory make_performance_ui_factory(
    PerformanceViewModel& model,
    const localization::LocalizationService& localization);

// Game-owned mapping from stable item keys to packaged PNG resources. The
// engine only accepts logical UI image keys; content chooses their files.
[[nodiscard]] snt::core::Expected<void> register_gameplay_ui_images(
    snt::ui::UiImageRegistry& images,
    const snt::core::RuntimePathResolver& paths);

std::vector<CraftingRecipeState> make_starting_crafting_recipes();
InventoryState make_starting_inventory();

}  // namespace snt::game
