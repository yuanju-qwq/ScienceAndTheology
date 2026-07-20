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
#include "ui/retained_mui_drag.h"
#include "ui/retained_mui_layout.h"
#include "core/expected.h"
#include "inventory_slot_transaction.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace snt::game::localization {
class LocalizationService;
}
namespace snt::core { class RuntimePathResolver; }
namespace snt::ui { class UiImageRegistry; }

namespace snt::game {

class GameContentRegistry;

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

// Production-machine presentation is a copied network value. It deliberately
// has no ECS handle, sidecar record, or mutable inventory reference: the UI
// can only turn a drag into a typed request and wait for host replication.
struct MachinePanelState {
    std::string dimension_id;
    int32_t root_x = 0;
    int32_t root_y = 0;
    int32_t root_z = 0;
    uint16_t expected_material = 0;
    std::string machine_id;
    std::vector<ItemStackState> input_slots;
    std::vector<ItemStackState> output_slots;
    int32_t max_input_slots = 0;
    int32_t max_output_slots = 0;
    int32_t stored_energy = 0;
    int32_t energy_capacity = 0;
    int32_t progress_ticks = 0;
    int32_t active_recipe_duration_ticks = 0;

    friend bool operator==(const MachinePanelState&, const MachinePanelState&) = default;
};

enum class MachineInputSlotTransferDirection : uint8_t {
    PlayerToMachineInput,
    MachineInputToPlayer,
};

struct MachineInputSlotTransferRequest {
    uint64_t request_id = 0;
    uint64_t expected_inventory_revision = 0;
    MachineInputSlotTransferDirection direction =
        MachineInputSlotTransferDirection::PlayerToMachineInput;
    MachinePanelState target;
    uint32_t player_slot = 0;
    uint32_t machine_input_slot = 0;
    int32_t count = 0;
    ItemStackState expected_player_slot;
    ItemStackState expected_machine_input_slot;
};

struct MachineInputSlotTransferConfirmation {
    uint64_t request_id = 0;
    InventorySlotTransferOutcome outcome = InventorySlotTransferOutcome::Rejected;
    uint64_t authoritative_inventory_revision = 0;
    std::vector<ItemStackState> inventory_slots;
    int32_t inventory_max_stack_size = 64;
    std::string rejection_reason;
};

class IMachineInputSlotTransferCommandSink {
public:
    virtual ~IMachineInputSlotTransferCommandSink() = default;

    [[nodiscard]] virtual snt::core::Expected<void> submit_machine_input_slot_transfer(
        MachineInputSlotTransferRequest request) = 0;
};

class MachinePanelViewModel {
public:
    [[nodiscard]] bool apply_authoritative_state(MachinePanelState state);
    void clear() noexcept;

    [[nodiscard]] const MachinePanelState* state() const noexcept {
        return state_ ? &*state_ : nullptr;
    }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }

private:
    std::optional<MachinePanelState> state_;
    uint64_t revision_ = 0;
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
    // Only an inventory transaction confirmation may replace slots used by
    // retained SlotViews. Layout-only state such as the selected hotbar slot
    // remains local presentation state.
    [[nodiscard]] bool apply_authoritative_slots(std::vector<ItemStackState> slots,
                                                 int32_t max_stack_size);

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
    Machine,
};

class GameplayUiController {
public:
    GameplayUiController(InventoryViewModel inventory,
                         std::vector<CraftingRecipeState> recipes,
                         std::shared_ptr<IInventorySlotTransferCommandSink>
                             slot_transfer_sink = {},
                         std::shared_ptr<IMachineInputSlotTransferCommandSink>
                             machine_input_slot_transfer_sink = {},
                         const GameContentRegistry* content = nullptr,
                         const snt::core::RuntimePathResolver* paths = nullptr);

    InventoryViewModel& inventory() { return inventory_; }
    HotbarViewModel& hotbar() { return hotbar_; }
    CraftingViewModel& crafting() { return crafting_; }
    MachinePanelViewModel& machine_panel() { return machine_panel_; }
    [[nodiscard]] const GameContentRegistry* content() const noexcept { return content_; }
    [[nodiscard]] uint64_t item_content_revision() const noexcept;
    // The source path is session-owned and stable for the controller lifetime.
    // Calling this again after a content reload is idempotent for unchanged
    // assets and registers only newly referenced item images.
    [[nodiscard]] snt::core::Expected<void> register_content_images(
        snt::ui::UiImageRegistry& images) const;

    GameplayUiScreen open_screen() const { return open_screen_; }
    bool inventory_open() const { return open_screen_ == GameplayUiScreen::Inventory; }
    bool crafting_open() const { return open_screen_ == GameplayUiScreen::Crafting; }
    bool machine_open() const { return open_screen_ == GameplayUiScreen::Machine; }
    uint64_t revision() const noexcept { return revision_; }

    void open_inventory();
    void open_crafting();
    void open_machine(MachinePanelState state);
    void close();
    void toggle_inventory();
    void toggle_crafting();

    // SlotView drag callbacks submit value-only requests. They do not mutate
    // InventoryViewModel; the owning game session later supplies a matching
    // authority confirmation from its simulator or network adapter.
    void handle_inventory_slot_drag(const snt::ui::UiDragEvent& event);
    void handle_machine_input_slot_drag(const snt::ui::UiDragEvent& event);
    // Applies a complete client-side reconstruction of the authoritative
    // inventory. Network replication may contain only changed slots on the
    // wire, but the UI always receives one validated full slot view.
    [[nodiscard]] bool apply_inventory_authoritative_snapshot(
        std::vector<ItemStackState> slots, int32_t max_stack_size,
        uint64_t authoritative_revision);
    [[nodiscard]] bool apply_inventory_slot_transfer_confirmation(
        InventorySlotTransferConfirmation confirmation);
    [[nodiscard]] bool apply_machine_input_slot_transfer_confirmation(
        MachineInputSlotTransferConfirmation confirmation);
    // A reconnect invalidates presentation authority until the next full
    // server snapshot. Existing local slots remain visible but cannot be
    // safely used by a network command adapter with revision zero.
    void clear_inventory_authority() noexcept;
    void clear_machine_authority() noexcept;
    [[nodiscard]] bool inventory_slot_transfer_pending() const noexcept {
        return pending_slot_transfer_.has_value();
    }
    [[nodiscard]] uint64_t pending_inventory_slot_transfer_request_id() const noexcept {
        return pending_slot_transfer_ ? pending_slot_transfer_->request_id : 0;
    }
    [[nodiscard]] uint64_t inventory_authority_revision() const noexcept {
        return inventory_authority_revision_;
    }
    [[nodiscard]] bool machine_input_slot_transfer_pending() const noexcept {
        return pending_machine_input_slot_transfer_.has_value();
    }
    [[nodiscard]] uint64_t pending_machine_input_slot_transfer_request_id() const noexcept {
        return pending_machine_input_slot_transfer_
            ? pending_machine_input_slot_transfer_->request_id
            : 0;
    }

private:
    void set_open_screen(GameplayUiScreen screen);

    InventoryViewModel inventory_;
    HotbarViewModel hotbar_;
    CraftingViewModel crafting_;
    MachinePanelViewModel machine_panel_;
    GameplayUiScreen open_screen_ = GameplayUiScreen::None;
    std::shared_ptr<IInventorySlotTransferCommandSink> slot_transfer_sink_;
    std::shared_ptr<IMachineInputSlotTransferCommandSink> machine_input_slot_transfer_sink_;
    std::optional<InventorySlotTransferRequest> pending_slot_transfer_;
    std::optional<MachineInputSlotTransferRequest> pending_machine_input_slot_transfer_;
    const GameContentRegistry* content_ = nullptr;
    const snt::core::RuntimePathResolver* paths_ = nullptr;
    uint64_t inventory_authority_revision_ = 0;
    uint64_t next_inventory_transfer_request_id_ = 1;
    uint64_t next_machine_input_slot_transfer_request_id_ = 1;
    uint64_t revision_ = 0;
};

std::unique_ptr<snt::ui::ViewGroup> build_hotbar_view(const HotbarViewModel& model);
std::unique_ptr<snt::ui::ViewGroup> build_inventory_view(
    const InventoryViewModel& model, const localization::LocalizationService& localization);
std::unique_ptr<snt::ui::ViewGroup> build_crafting_view(
    CraftingViewModel& model, const localization::LocalizationService& localization);
std::unique_ptr<snt::ui::ViewGroup> build_machine_panel_view(
    const MachinePanelViewModel& model, const InventoryViewModel& inventory,
    const localization::LocalizationService& localization);
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
    const snt::core::RuntimePathResolver& paths,
    const GameContentRegistry* content = nullptr);

std::vector<CraftingRecipeState> make_starting_crafting_recipes();
InventoryState make_starting_inventory();

}  // namespace snt::game
