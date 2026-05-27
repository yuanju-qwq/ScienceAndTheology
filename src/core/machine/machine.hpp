#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/resource_key.hpp"
#include "recipe.hpp"
#include "processing_logic.hpp"
#include "machine_port.hpp"
#include "module.hpp"
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
    int64_t max_input_voltage = 0;      // 0 = auto from tier (used when no energy module)
    size_t input_slot_count = 1;
    size_t output_slot_count = 1;
    int64_t internal_power_buffer = 0;  // max stored power (0 = draw directly)

    // Multi-tile footprint.
    int footprint_width = 1;
    int footprint_height = 1;

    // Physical I/O ports on the machine boundary.
    std::vector<MachinePort> ports;

    // Module slot templates (what modules this machine accepts).
    // Every machine MUST define at least one ENERGY_INPUT slot.
    std::vector<ModuleSlot> module_slots;

    MachineConfig() {
        max_input_voltage = get_voltage(tier);
    }
};

// ============================================================
// Runtime port state
// ============================================================

// Mirrors MachineConfig::ports at runtime; allows the player to flip
// non-locked port directions via the machine UI.
struct PortState {
    PortDirection direction = PortDirection::INPUT;
};

// ============================================================
// Base machine class
// ============================================================

// Represents a processing machine that may occupy multiple tiles.
// Machines have:
//   - A multi-tile footprint with boundary I/O ports
//   - Module slots for energy hatches, coils, mufflers, etc.
//   - Input/output inventory slots
//   - A recipe map for lookup
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

    // --- Footprint ---

    int footprint_width() const { return config_.footprint_width; }
    int footprint_height() const { return config_.footprint_height; }
    const std::vector<MachinePort>& ports() const { return config_.ports; }
    const std::vector<ModuleSlot>& module_slots() const { return config_.module_slots; }

    // --- Inventory ---

    std::vector<ResourceStack>& input_slots() { return input_slots_; }
    const std::vector<ResourceStack>& input_slots() const { return input_slots_; }
    std::vector<ResourceStack>& output_slots() { return output_slots_; }
    const std::vector<ResourceStack>& output_slots() const { return output_slots_; }

    // Get all non-empty input items as a flat list (for recipe matching).
    std::vector<ResourceStack> get_available_inputs() const;

    // --- Power ---

    // Called by the game loop to report available power this tick.
    void set_power_available(int64_t available_eu_t);

    // Returns the current power demand (overclocked EU/t of active recipe).
    int64_t get_power_demand() const;

    // Returns true if the machine has sufficient power this tick.
    bool is_powered() const { return power_available_ >= power_demand_; }

    // --- Modules ---

    // Recomputes derived stats (max_input_voltage, heat, parallel, etc.)
    // from currently installed modules. Call after installing/removing modules.
    void recompute_from_modules();

    // --- Runtime port state ---

    std::vector<PortState>& port_states() { return port_states_; }
    const std::vector<PortState>& port_states() const { return port_states_; }

    // Flip a port's direction at runtime (only if not locked).
    void set_port_direction(int index, PortDirection dir);

    // Installed module access.
    std::vector<InstalledModule>& installed_modules() { return installed_modules_; }
    const std::vector<InstalledModule>& installed_modules() const {
        return installed_modules_;
    }

    // Convenience: install a module by definition.
    bool install_module(const ModuleDefinition* def);
    bool remove_module(const ModuleDefinition* def);

    // Derived stats computed by recompute_from_modules().
    int64_t derived_heat_capacity() const { return derived_heat_; }
    int64_t derived_parallel() const { return derived_parallel_; }
    int64_t derived_efficiency_pct() const { return derived_eff_pct_; }
    int64_t derived_pollution_pct() const { return derived_pollution_pct_; }

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
    std::vector<ResourceStack> input_slots_;
    std::vector<ResourceStack> output_slots_;

    // Active recipe tracking.
    const Recipe* current_recipe_ = nullptr;
    int64_t current_duration_ = 0;    // overclocked duration
    int64_t progress_ticks_ = 0;
    int64_t power_demand_ = 0;        // overclocked EU/t
    int64_t power_available_ = 0;     // EU/t available this tick

    // Module system.
    std::vector<InstalledModule> installed_modules_;

    // Runtime port state (mirrors config_.ports, direction may be flipped).
    std::vector<PortState> port_states_;

    // Derived stats from installed modules (set by recompute_from_modules).
    int64_t derived_heat_ = 0;
    int64_t derived_parallel_ = 1;
    int64_t derived_eff_pct_ = 100;
    int64_t derived_pollution_pct_ = 100;

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
