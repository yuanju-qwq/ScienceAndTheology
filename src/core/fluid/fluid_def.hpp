#pragma once

#include <cstdint>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// Fluid amounts are measured in millibuckets (mB), mirroring GT5 convention.
// 1000 mB = 1 B (bucket).

// ============================================================
// FluidDefinition — pure data descriptor for a fluid type
// ============================================================

struct FluidDefinition {
    const char* name = "";
    const char* display_name = "";
    const char* chemical_formula = "";
    int64_t temperature = 300;
    bool is_gas = false;

    // Phase transition: when this fluid is heated above evaporation_temp,
    // it transitions to evaporation_target. 0 = no transition.
    int64_t evaporation_temp = 0;
    FluidId evaporation_target = kInvalidFluidId;

    // Phase transition: when this fluid is cooled below condensation_temp,
    // it transitions to condensation_target. 0 = no transition.
    int64_t condensation_temp = 0;
    FluidId condensation_target = kInvalidFluidId;
};

} // namespace science_and_theology::gt