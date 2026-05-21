#pragma once

#include <cstdint>
#include <cmath>

#include "recipe.hpp"
#include "../power/gt_values.hpp"

namespace science_and_theology::gt {

// ============================================================
// Overclock calculation (GT5-derived formula)
// ============================================================

// Result of overclock calculation for a recipe at a given machine tier.
struct OverclockResult {
    int64_t eu_per_tick = 0;         // actual EU/t after overclock
    int64_t duration_ticks = 0;      // actual duration after overclock
    int64_t total_eu = 0;            // total energy = eu_per_tick * duration
    int tier_diff = 0;               // tiers above minimum
    bool is_viable = false;          // can the recipe run at this tier?
    bool is_perfect_overclock = false; // duration_ticks == 1 (max speed)

    // Convenience: total energy cost.
    int64_t energy_cost() const { return total_eu; }
};

// Computes the overclocked parameters for running a recipe at a given tier.
//
// GT5 formula:
//   Each tier above minimum:
//     - EU/t multiplies by 4 (power cost rises quadratically)
//     - Duration divides by 2 (speed doubles)
//     - Duration is floored, minimum 1 tick
//
// Example: recipe at LV(32V), 5 EU/t, 400 ticks
//   MV(128V): 1 tier above → 20 EU/t, 200 ticks
//   HV(512V): 2 tiers above → 80 EU/t, 100 ticks
//   EV(2048V): 3 tiers above → 320 EU/t, 50 ticks
inline OverclockResult compute_overclock(const Recipe& recipe,
                                          VoltageTier machine_tier) {
    OverclockResult result;

    int recipe_tier = static_cast<int>(recipe.min_tier);
    int actual_tier = static_cast<int>(machine_tier);
    result.tier_diff = actual_tier - recipe_tier;

    // Can't run a higher-tier recipe on a lower-tier machine.
    if (result.tier_diff < 0) {
        result.is_viable = false;
        return result;
    }

    result.is_viable = true;

    // Base values.
    int64_t base_eu = recipe.eu_per_tick;
    int64_t base_duration = recipe.duration_ticks;

    // EU/t: multiply by 4^tier_diff.
    // 4^tier_diff = 2^(2 * tier_diff) = 1 << (2 * tier_diff).
    int64_t overclock_multiplier = int64_t(1) << (2 * result.tier_diff);
    result.eu_per_tick = base_eu * overclock_multiplier;

    // Duration: divide by 2^tier_diff.
    int64_t duration_divisor = int64_t(1) << result.tier_diff;
    result.duration_ticks = std::max<int64_t>(1, base_duration / duration_divisor);

    result.total_eu = result.eu_per_tick * result.duration_ticks;
    result.is_perfect_overclock = (result.duration_ticks == 1);

    return result;
}

// ============================================================
// Recipe matching utilities
// ============================================================

// Checks whether a machine at a given tier can process a recipe.
// Returns true if voltage is sufficient and power requirements are met.
inline bool can_process_recipe(const Recipe& recipe,
                                VoltageTier machine_tier,
                                int64_t available_power) {
    if (!recipe.is_voltage_sufficient(machine_tier)) return false;

    OverclockResult oc = compute_overclock(recipe, machine_tier);
    if (!oc.is_viable) return false;

    // The machine must be able to supply the overclocked EU/t.
    return available_power >= oc.eu_per_tick;
}

// ============================================================
// Processing state machine
// ============================================================

// Possible processing outcomes when attempting to start a recipe.
enum class ProcessResult : uint8_t {
    SUCCESS = 0,
    NO_RECIPE,          // no matching recipe found for inputs
    NO_POWER,           // insufficient power
    TIER_TOO_LOW,       // machine tier too low for recipe
    OUTPUT_FULL,        // output slots can't accept the result
    MISSING_INPUTS,     // required inputs not present (or wrong count)
    MACHINE_ERROR,      // machine in error state
};

// Human-readable names for process results.
constexpr const char* kProcessResultNames[] = {
    "Success",
    "No Recipe",
    "No Power",
    "Tier Too Low",
    "Output Full",
    "Missing Inputs",
    "Machine Error",
};

} // namespace science_and_theology::gt
