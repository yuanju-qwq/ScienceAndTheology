#pragma once

#include <algorithm>
#include <cstdint>

#include "common/resource_types.hpp"

namespace science_and_theology::gt {

// ============================================================
// FluidTank — container with capacity and type tracking
// ============================================================

struct FluidTank {
    // If set, only accepts this specific fluid. kInvalidFluidId = accepts any.
    FluidId accepted_fluid = kInvalidFluidId;

    FluidId current_fluid = kInvalidFluidId;
    int64_t capacity = 0;
    int64_t amount = 0;
    bool is_input = false;
    bool is_output = false;

    bool is_empty() const { return amount <= 0; }
    bool is_full() const { return amount >= capacity; }
    int64_t remaining_space() const {
        return (capacity > amount) ? (capacity - amount) : 0;
    }

    // Attempts to insert fluid. Returns the amount actually inserted.
    int64_t insert(FluidId fluid, int64_t to_insert) {
        if (to_insert <= 0) return 0;
        if (current_fluid != kInvalidFluidId && current_fluid != fluid) return 0;
        if (accepted_fluid != kInvalidFluidId && accepted_fluid != fluid) return 0;

        int64_t space = remaining_space();
        int64_t inserted = (to_insert < space) ? to_insert : space;
        amount += inserted;
        if (amount > 0 && current_fluid == kInvalidFluidId) {
            current_fluid = fluid;
        }
        return inserted;
    }

    // Attempts to extract fluid. Returns the amount actually extracted.
    int64_t extract(int64_t to_extract) {
        if (to_extract <= 0) return 0;
        int64_t extracted = (to_extract < amount) ? to_extract : amount;
        amount -= extracted;
        if (amount <= 0) {
            current_fluid = kInvalidFluidId;
            amount = 0;
        }
        return extracted;
    }
};

} // namespace science_and_theology::gt