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
};

} // namespace science_and_theology::gt