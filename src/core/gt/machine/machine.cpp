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

std::vector<ItemStack> Machine::get_available_inputs() const {
    std::vector<ItemStack> available;
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
// Tick processing
// ============================================================

void Machine::tick() {
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
        // Machines in these states attempt to recover on next tick.
        state_ = MachineState::IDLE;
        break;
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

        int64_t remaining = input.count;
        for (auto& slot : input_slots_) {
            if (!slot.is_valid()) continue;
            if (slot.item_id != input.item_id) continue;

            int64_t take = std::min(remaining, slot.count);
            slot.count -= take;
            remaining -= take;
            if (slot.count <= 0) {
                slot = ItemStack{};
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

        int64_t remaining = output.count;
        // First, try to stack onto existing matching slots.
        for (auto& slot : output_slots_) {
            if (slot.item_id == output.item_id && slot.count < INT64_MAX) {
                int64_t space = INT64_MAX - slot.count;
                int64_t add = std::min(remaining, space);
                slot.count += add;
                remaining -= add;
                if (remaining == 0) break;
            }
        }

        // Fill empty slots if needed.
        if (remaining > 0) {
            for (auto& slot : output_slots_) {
                if (!slot.is_valid()) {
                    slot = ItemStack{output.item_id, remaining};
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
    std::vector<ItemStack> preview = output_slots_;
    for (const auto& output : recipe.outputs) {
        if (!output.is_valid()) continue;

        int64_t remaining = output.count;
        for (auto& slot : preview) {
            if (slot.item_id == output.item_id && slot.count < INT64_MAX) {
                int64_t add = std::min(remaining, INT64_MAX - slot.count);
                slot.count += add;
                remaining -= add;
                if (remaining == 0) break;
            }
        }
        if (remaining > 0) {
            for (auto& slot : preview) {
                if (!slot.is_valid()) {
                    slot = ItemStack{output.item_id, remaining};
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

void Machine::on_recipe_started(const Recipe&) {}
void Machine::on_recipe_completed(const Recipe&) {}
void Machine::on_recipe_aborted(const Recipe&) {}

} // namespace science_and_theology::gt
