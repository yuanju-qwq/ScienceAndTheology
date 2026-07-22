#pragma once

#include <cstdint>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// ============================================================
// ItemDefinition — universal item base
// ============================================================
//
// Every game item has a unique ItemId and these basic properties.
// Module-specific types (MaterialItem and GT tools) extend
// this struct with module-specific data.
//
// const char* fields point to storage owned by the extending struct.

struct ItemDefinition {
    ItemId id = kInvalidItemId;
    const char* name_key = "";        // e.g. "ingot.copper"
    const char* title_key = "";       // e.g. "material.copper"
    int64_t max_stack_size = 64;
};

} // namespace science_and_theology::gt
