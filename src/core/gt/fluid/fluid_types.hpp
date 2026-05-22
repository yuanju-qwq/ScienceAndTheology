#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace science_and_theology::gt {

// Unique fluid identifier.
using FluidId = uint16_t;
inline constexpr FluidId kInvalidFluidId = 0xFFFF;

// Fluid amounts are measured in millibuckets (mB), mirroring GT5 convention.
// 1000 mB = 1 B (bucket).

// ============================================================
// Fluid definition
// ============================================================

struct FluidDefinition {
    const char* name = "";
    const char* display_name = "";
    const char* chemical_formula = "";
    int64_t temperature = 300;
    bool is_gas = false;
};

// ============================================================
// Fluid stack — a quantity of a specific fluid
// ============================================================

struct FluidStack {
    FluidId fluid_id = kInvalidFluidId;
    int64_t amount = 0;

    bool is_valid() const { return fluid_id != kInvalidFluidId && amount > 0; }
    bool is_empty() const { return fluid_id == kInvalidFluidId || amount <= 0; }

    bool operator==(const FluidStack& other) const {
        return fluid_id == other.fluid_id && amount == other.amount;
    }
    bool operator!=(const FluidStack& other) const {
        return !(*this == other);
    }
};

// ============================================================
// Fluid tank — container with capacity and type tracking
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

// ============================================================
// Fluid registry — global lookup table for all fluid types
// ============================================================

class FluidRegistry {
public:
    static void initialize();

    static FluidId register_fluid(const FluidDefinition& def);
    static const FluidDefinition* get_fluid(FluidId id);
    static const FluidDefinition* get_fluid_by_name(const char* name);
    static FluidId get_fluid_id(const char* name);
    static size_t get_fluid_count();

    // Pre-register all built-in fluids.
    static void register_builtin_fluids();

private:
    static std::vector<FluidDefinition>& registry();
};

} // namespace science_and_theology::gt