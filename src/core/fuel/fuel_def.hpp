#pragma once

#include <cstdint>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

enum class FuelCategory : uint8_t {
    SOLID = 0,
    LIQUID,
    GAS,
    MAGIC,
    PLASMA,
    OTHER,
};

struct FuelDefinition {
    const char* name = "";
    const char* display_name = "";
    FuelCategory category = FuelCategory::SOLID;

    ItemId item_id = kInvalidItemId;
    FluidId fluid_id = kInvalidFluidId;

    // Burn duration in game ticks (20 TPS).
    // For items: per single item unit.
    // For fluids: per 1000 mB (1 bucket equivalent).
    int64_t burn_ticks = 0;
};

} // namespace science_and_theology::gt
