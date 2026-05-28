#include "machine.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace science_and_theology::gt {

// ============================================================
// Construction
// ============================================================

Machine::Machine(const MachineConfig& config)
    : config_(config) {
    input_slots_.resize(config.input_slot_count);
    output_slots_.resize(config.output_slot_count);

    // Initialize runtime port state from config blueprint.
    port_states_.reserve(config_.ports.size());
    for (const auto& port : config_.ports) {
        port_states_.push_back({port.direction});
    }
}

const char* Machine::state_name() const {
    return kMachineStateNames[static_cast<uint8_t>(state_)];
}

float Machine::progress_percent() const {
    if (current_duration_ <= 0) return 0.0f;
    return static_cast<float>(progress_ticks_) /
           static_cast<float>(current_duration_);
}

// ============================================================
// Inventory helpers
// ============================================================

std::vector<ResourceStack> Machine::get_available_inputs() const {
    std::vector<ResourceStack> available;
    for (const auto& stack : input_slots_) {
        if (stack.is_valid()) {
            available.push_back(stack);
        }
    }
    return available;
}

// ============================================================
// Power
// ============================================================

void Machine::set_power_available(int64_t available_eu_t) {
    power_available_ = available_eu_t;
}

int64_t Machine::get_power_demand() const {
    return power_demand_;
}

// ============================================================
// Module system
// ============================================================

void Machine::recompute_from_modules() {
    // Reset to defaults.
    config_.max_input_voltage = get_voltage(config_.tier);
    derived_heat_ = 0;
    derived_parallel_ = 1;
    derived_eff_pct_ = 100;
    derived_pollution_pct_ = 100;

    int coil_count = 0;

    for (const auto& mod : installed_modules_) {
        if (mod.def == nullptr) continue;

        switch (mod.def->category) {
        case ModuleCategory::ENERGY_INPUT:
            config_.max_input_voltage = (mod.def->max_eu_per_tick > 0)
                ? mod.def->max_eu_per_tick
                : get_voltage(mod.def->tier);
            break;

        case ModuleCategory::COIL:
            derived_heat_ += mod.def->heat_capacity;
            coil_count++;
            break;

        case ModuleCategory::MUFFLER:
            derived_pollution_pct_ -= mod.def->pollution_reduction_pct;
            if (derived_pollution_pct_ < 0) derived_pollution_pct_ = 0;
            break;

        case ModuleCategory::OVERCLOCK:
            // Future: modify speed/power multipliers.
            break;

        case ModuleCategory::TRANSFORMER:
            config_.max_input_voltage = get_voltage(mod.def->tier);
            break;
        }
    }

    // Efficiency loss from coils: average the efficiency of all coils.
    if (coil_count > 0) {
        int64_t total_eff = 0;
        for (const auto& mod : installed_modules_) {
            if (mod.def != nullptr &&
                mod.def->category == ModuleCategory::COIL) {
                total_eff += mod.def->efficiency_pct;
            }
        }
        derived_eff_pct_ = total_eff / coil_count;
    }

    // Parallel = base 1 + sum of all coil parallel bonuses.
    for (const auto& mod : installed_modules_) {
        if (mod.def != nullptr &&
            mod.def->category == ModuleCategory::COIL) {
            derived_parallel_ += mod.def->parallel_bonus;
        }
    }
}

namespace {

// Cross-category compatibility: some module categories conflict because
// they both alter the same machine parameter (e.g. max_input_voltage).
// Returns true if `new_def` can coexist with all `installed`.
bool is_module_compatible(const ModuleDefinition* new_def,
                           const std::vector<InstalledModule>& installed) {
    if (new_def == nullptr) return false;

    for (const auto& mod : installed) {
        if (mod.def == nullptr) continue;

        // ENERGY_INPUT and TRANSFORMER both set max_input_voltage.
        // Installing both would produce ambiguous results.
        bool voltage_clash =
            (new_def->category == ModuleCategory::ENERGY_INPUT &&
             mod.def->category == ModuleCategory::TRANSFORMER) ||
            (new_def->category == ModuleCategory::TRANSFORMER &&
             mod.def->category == ModuleCategory::ENERGY_INPUT);
        if (voltage_clash) return false;
    }

    return true;
}

} // namespace

bool Machine::install_module(const ModuleDefinition* def) {
    if (def == nullptr) return false;

    // Only multi-tile machines (footprint > 1x1) can install modules.
    if (config_.footprint_width <= 1 && config_.footprint_height <= 1) {
        return false;
    }

    // Count how many of this category are already installed.
    int category_installed = 0;
    int category_max = 0;
    for (const auto& slot : config_.module_slots) {
        if (slot.category == def->category) {
            category_max = slot.max_count;
            break;
        }
    }

    for (const auto& mod : installed_modules_) {
        if (mod.def != nullptr && mod.def->category == def->category) {
            category_installed++;
        }
    }

    if (category_installed >= category_max) return false;

    // Check tier range.
    for (const auto& slot : config_.module_slots) {
        if (slot.category == def->category) {
            if (def->tier < slot.min_tier || def->tier > slot.max_tier) {
                return false;
            }
            break;
        }
    }

    // Check cross-category compatibility.
    if (!is_module_compatible(def, installed_modules_)) return false;

    InstalledModule inst;
    inst.def = def;
    installed_modules_.push_back(inst);
    recompute_from_modules();
    return true;
}

bool Machine::remove_module(const ModuleDefinition* def) {
    if (def == nullptr) return false;

    for (auto it = installed_modules_.begin();
         it != installed_modules_.end(); ++it) {
        if (it->def == def) {
            installed_modules_.erase(it);
            recompute_from_modules();
            return true;
        }
    }
    return false;
}

// ============================================================
// Runtime port state
// ============================================================

void Machine::set_port_direction(int index, PortDirection dir) {
    if (index < 0 || index >= static_cast<int>(port_states_.size())) return;
    if (config_.ports[index].direction_locked) return;
    port_states_[index].direction = dir;
}

// ============================================================
// Tick processing
// ============================================================

void Machine::tick() {
    MachineState old_state = state_;

    switch (state_) {
    case MachineState::IDLE:
    case MachineState::NO_RECIPE:
    case MachineState::OUTPUT_FULL: {
        ProcessResult result = try_start();
        if (result == ProcessResult::SUCCESS) {
            state_ = MachineState::PROCESSING;
        } else if (result == ProcessResult::NO_RECIPE) {
            state_ = MachineState::NO_RECIPE;
        } else if (result == ProcessResult::NO_POWER) {
            state_ = MachineState::NO_POWER;
        } else if (result == ProcessResult::OUTPUT_FULL) {
            state_ = MachineState::OUTPUT_FULL;
        } else if (result == ProcessResult::TIER_TOO_LOW) {
            state_ = MachineState::TIER_TOO_LOW;
        }
        break;
    }
    case MachineState::PROCESSING: {
        // Check power.
        if (!is_powered()) {
            state_ = MachineState::NO_POWER;
            on_recipe_aborted(*current_recipe_);
            break;
        }

        // Advance progress.
        ++progress_ticks_;

        ProcessResult result = try_complete();
        if (result == ProcessResult::SUCCESS) {
            // Recipe completed — remain in PROCESSING or go IDLE after
            // try_start() is called next tick (or now if continuous).
            state_ = MachineState::IDLE;
            // Immediately attempt to start next recipe.
            ProcessResult next = try_start();
            if (next == ProcessResult::SUCCESS) {
                state_ = MachineState::PROCESSING;
            }
        } else if (result == ProcessResult::OUTPUT_FULL) {
            state_ = MachineState::OUTPUT_FULL;
        }
        break;
    }
    case MachineState::NO_POWER:
    case MachineState::TIER_TOO_LOW:
    case MachineState::ERROR:
        // Wait for player intervention or power restoration.
        state_ = MachineState::IDLE;
        break;
    }

    // Emit state change event if state changed.
    if (state_ != old_state && state_change_cb_) {
        state_change_cb_(machine_id_, old_state, state_);
    }
}

void Machine::abort() {
    if (state_ == MachineState::PROCESSING && current_recipe_ != nullptr) {
        on_recipe_aborted(*current_recipe_);
    }
    current_recipe_ = nullptr;
    progress_ticks_ = 0;
    current_duration_ = 0;
    power_demand_ = 0;
    state_ = MachineState::IDLE;
}

void Machine::reset() {
    abort();
    input_slots_.clear();
    output_slots_.clear();
    input_slots_.resize(config_.input_slot_count);
    output_slots_.resize(config_.output_slot_count);
}

// ============================================================
// Recipe processing
// ============================================================

ProcessResult Machine::try_start() {
    RecipeMap* map = get_recipe_map();
    if (map == nullptr) {
        return ProcessResult::MACHINE_ERROR;
    }

    // Get available inputs.
    auto available = get_available_inputs();
    if (available.empty()) {
        return ProcessResult::MISSING_INPUTS;
    }

    // Find a matching recipe.
    const Recipe* recipe = map->find_first_matching(available);
    if (recipe == nullptr) {
        return ProcessResult::NO_RECIPE;
    }

    // Check voltage tier.
    if (!can_process_recipe(*recipe, config_.tier, power_available_)) {
        if (!recipe->is_voltage_sufficient(config_.tier)) {
            return ProcessResult::TIER_TOO_LOW;
        }
        return ProcessResult::NO_POWER;
    }

    // Compute overclocked parameters.
    OverclockResult oc = compute_overclock(*recipe, config_.tier);
    if (!oc.is_viable) {
        return ProcessResult::TIER_TOO_LOW;
    }

    // Check output slots can accept the outputs.
    if (!place_outputs_preview(*recipe)) {
        return ProcessResult::OUTPUT_FULL;
    }

    // Start processing.
    current_recipe_ = recipe;
    current_duration_ = oc.duration_ticks;
    progress_ticks_ = 0;
    power_demand_ = oc.eu_per_tick;

    on_recipe_started(*recipe);
    return ProcessResult::SUCCESS;
}

ProcessResult Machine::try_complete() {
    if (current_recipe_ == nullptr) {
        return ProcessResult::MACHINE_ERROR;
    }

    if (progress_ticks_ < current_duration_) {
        return ProcessResult::NO_RECIPE; // Still processing — not done yet.
    }

    // Consume inputs.
    if (!consume_inputs(*current_recipe_)) {
        return ProcessResult::MISSING_INPUTS;
    }

    // Place outputs.
    if (!place_outputs(*current_recipe_)) {
        // Should not happen — we pre-checked in try_start.
        return ProcessResult::OUTPUT_FULL;
    }

    on_recipe_completed(*current_recipe_);
    current_recipe_ = nullptr;
    current_duration_ = 0;
    progress_ticks_ = 0;
    power_demand_ = 0;

    return ProcessResult::SUCCESS;
}

// ============================================================
// Inventory operations
// ============================================================

bool Machine::consume_inputs(const Recipe& recipe) {
    for (const auto& input : recipe.inputs) {
        if (!input.is_valid()) continue;

        int64_t remaining = input.amount;
        for (auto& slot : input_slots_) {
            if (!slot.is_valid()) continue;
            if (!slot.has_same_key(input)) continue;

            int64_t take = std::min(remaining, slot.amount);
            slot.amount -= take;
            remaining -= take;
            if (slot.amount <= 0) {
                slot = ResourceStack{};
            }
            if (remaining == 0) break;
        }

        if (remaining > 0) {
            return false; // Not enough items (shouldn't happen if validated).
        }
    }
    return true;
}

bool Machine::place_outputs(const Recipe& recipe) {
    for (const auto& output : recipe.outputs) {
        if (!output.is_valid()) continue;
        if (output.probability < 1.0f) {
            // Chanced output — skip if roll fails.
            // (Random is handled by the game layer; here we place all.)
            // For deterministic C++ core, we always place chanced outputs.
        }

        int64_t remaining = output.stack.amount;
        // First, try to stack onto existing matching slots.
        for (auto& slot : output_slots_) {
            if (slot.has_same_key(output.stack) && slot.amount < INT64_MAX) {
                int64_t space = INT64_MAX - slot.amount;
                int64_t add = std::min(remaining, space);
                slot.amount += add;
                remaining -= add;
                if (remaining == 0) break;
            }
        }

        // Fill empty slots if needed.
        if (remaining > 0) {
            for (auto& slot : output_slots_) {
                if (!slot.is_valid()) {
                    slot = ResourceStack(output.stack.what, remaining);
                    remaining = 0;
                    break;
                }
            }
        }

        if (remaining > 0) {
            return false; // Not enough output space.
        }
    }
    return true;
}

bool Machine::place_outputs_preview(const Recipe& recipe) {
    // Clone output slots and simulate placement.
    std::vector<ResourceStack> preview = output_slots_;
    for (const auto& output : recipe.outputs) {
        if (!output.is_valid()) continue;

        int64_t remaining = output.stack.amount;
        for (auto& slot : preview) {
            if (slot.has_same_key(output.stack) && slot.amount < INT64_MAX) {
                int64_t add = std::min(remaining, INT64_MAX - slot.amount);
                slot.amount += add;
                remaining -= add;
                if (remaining == 0) break;
            }
        }
        if (remaining > 0) {
            for (auto& slot : preview) {
                if (!slot.is_valid()) {
                    slot = ResourceStack(output.stack.what, remaining);
                    remaining = 0;
                    break;
                }
            }
        }
        if (remaining > 0) return false;
    }
    return true;
}

// ============================================================
// Recipe map lookup
// ============================================================

RecipeMap* Machine::get_recipe_map() {
    return RecipeDatabase::get_map(config_.recipe_map_name);
}

// ============================================================
// Subclass hooks (default no-ops)
// ============================================================

void Machine::on_recipe_started(const Recipe& recipe) {
    if (recipe_started_cb_) {
        recipe_started_cb_(machine_id_, recipe);
    }
}
void Machine::on_recipe_completed(const Recipe& recipe) {
    if (recipe_complete_cb_) {
        recipe_complete_cb_(machine_id_, recipe);
    }
}
void Machine::on_recipe_aborted(const Recipe&) {}

} // namespace science_and_theology::gt
