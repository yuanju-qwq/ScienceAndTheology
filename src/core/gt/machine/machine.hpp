#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../material/item.hpp"
#include "recipe.hpp"
#include "processing_logic.hpp"
#include "../power/power_node.hpp"

namespace science_and_theology::gt {

// ============================================================
// Machine state
// ============================================================

enum class MachineState : uint8_t {
    IDLE = 0,
    PROCESSING,     // actively processing a recipe
    OUTPUT_FULL,    // recipe done but output slots full
    NO_POWER,       // insufficient power to continue
    NO_RECIPE,      // no matching recipe for current inputs
    TIER_TOO_LOW,   // recipe requires higher tier
    ERROR,          // machine in error state (needs player intervention)
};

constexpr const char* kMachineStateNames[] = {
    "Idle",
    "Processing",
    "Output Full",
    "No Power",
    "No Recipe",
    "Tier Too Low",
    "Error",
};

// ============================================================
// Machine configuration
// ============================================================

struct MachineConfig {
    const char* machine_name = "";
    const char* machine_type = "";      // e.g. "centrifuge", "furnace"
    const char* recipe_map_name = "";   // which RecipeMap to query
    VoltageTier tier = VoltageTier::ULV;
    int64_t max_input_voltage = 0;      // 0 = auto from tier
    size_t input_slot_count = 1;
    size_t output_slot_count = 1;
    int64_t internal_power_buffer = 0;  // max stored power (0 = draw directly)

    MachineConfig() {
        max_input_voltage = get_voltage(tier);
    }
};

// ============================================================
// Base machine class
// ============================================================

// Represents a single-block processing machine.
// Machines have:
//   - Input/output inventory slots
//   - A recipe map for lookup
//   - A voltage tier determining power limits
//   - Tick-based processing lifecycle
//
// Usage pattern (game loop):
//   1. machine.set_power_available(network_power)
//   2. machine.tick()
//   3. report machine.get_power_demand() back to network
//
// This decouples the machine from the power network — the game
// loop mediates power flow.
class Machine {
public:
    explicit Machine(const MachineConfig& config);
    virtual ~Machine() = default;

    // --- Accessors ---

    const MachineConfig& config() const { return config_; }
    MachineState state() const { return state_; }
    const char* state_name() const;
    const Recipe* current_recipe() const { return current_recipe_; }
    int64_t progress() const { return progress_ticks_; }
    int64_t progress_max() const { return current_duration_; }
    float progress_percent() const;

    // --- Inventory ---

    std::vector<ItemStack>& input_slots() { return input_slots_; }
    const std::vector<ItemStack>& input_slots() const { return input_slots_; }
    std::vector<ItemStack>& output_slots() { return output_slots_; }
    const std::vector<ItemStack>& output_slots() const { return output_slots_; }

    // Get all non-empty input items as a flat list (for recipe matching).
    std::vector<ItemStack> get_available_inputs() const;

    // --- Power ---

    // Called by the game loop to report available power this tick.
    void set_power_available(int64_t available_eu_t);

    // Returns the current power demand (overclocked EU/t of active recipe).
    int64_t get_power_demand() const;

    // Returns true if the machine has sufficient power this tick.
    bool is_powered() const { return power_available_ >= power_demand_; }

    // --- Processing ---

    // Advance the machine by one tick. Called each game tick (20 TPS).
    void tick();

    // Force-stop processing and return to IDLE. Consumes no inputs.
    void abort();

    // Reset to IDLE state.
    void reset();

    // --- Hooks for subclasses ---

    // Called when a recipe starts. Override for effects/sounds.
    virtual void on_recipe_started(const Recipe& recipe);

    // Called when a recipe completes. Override for effects/sounds.
    virtual void on_recipe_completed(const Recipe& recipe);

    // Called when a recipe fails or is aborted.
    virtual void on_recipe_aborted(const Recipe& recipe);

protected:
    MachineConfig config_;
    MachineState state_ = MachineState::IDLE;

    // Inventory.
    std::vector<ItemStack> input_slots_;
    std::vector<ItemStack> output_slots_;

    // Active recipe tracking.
    const Recipe* current_recipe_ = nullptr;
    int64_t current_duration_ = 0;    // overclocked duration
    int64_t progress_ticks_ = 0;
    int64_t power_demand_ = 0;        // overclocked EU/t
    int64_t power_available_ = 0;     // EU/t available this tick

    // --- Internal methods ---

    // Try to find a recipe and begin processing.
    ProcessResult try_start();

    // Advance processing and complete if done.
    ProcessResult try_complete();

    // Consume input items from input slots.
    bool consume_inputs(const Recipe& recipe);

    // Place output items into output slots. Returns false if slots are full.
    bool place_outputs(const Recipe& recipe);

    // Verify output slots can accept recipe outputs without modifying state.
    bool place_outputs_preview(const Recipe& recipe);

    // Returns the RecipeMap for this machine's type.
    RecipeMap* get_recipe_map();
};

} // namespace science_and_theology::gt
