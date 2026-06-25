#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fluid_def.hpp"

namespace science_and_theology::gt {

// ============================================================
// FluidRegistry — global lookup table for all fluid types
// ============================================================
//
// Separate from FluidDefinition / FluidTank to keep pure data
// types lightweight. Consumers who only need FluidId or
// FluidDefinition include fluid_def.hpp; consumers who need
// lookup include fluid_registry.hpp.

class FluidRegistry {
public:
    // initialize = reset + reserve ID 0 as invalid.
    static void initialize();
    // 完全清空 registry（用于热重载），不预留 ID 0。
    static void reset();

    // Register a fluid. If explicit_id != kInvalidFluidId, stores at that ID
    // (enables deterministic ID assignment from GD). Otherwise auto-assigns.
    static FluidId register_fluid(const FluidDefinition& def,
                                  FluidId explicit_id = kInvalidFluidId);
    static const FluidDefinition* get_fluid(FluidId id);
    static const FluidDefinition* get_fluid_by_name(const char* name);
    static FluidId get_fluid_id(const char* name);
    static size_t get_fluid_count();

private:
    static std::vector<FluidDefinition>& registry();
};

} // namespace science_and_theology::gt