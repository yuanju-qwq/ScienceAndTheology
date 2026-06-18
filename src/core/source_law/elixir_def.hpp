#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/magic/rune_def.hpp"
#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// ElixirType — category of elixir/potion
// ============================================================
enum class ElixirType : uint8_t {
    INITIATION = 0,
    ENHANCEMENT,
    PROMOTION,
    TUNING,
    PURIFICATION,
    COUNT
};

inline const char* elixir_type_name(ElixirType type) {
    switch (type) {
        case ElixirType::INITIATION:   return "initiation";
        case ElixirType::ENHANCEMENT:  return "enhancement";
        case ElixirType::PROMOTION:    return "promotion";
        case ElixirType::TUNING:       return "tuning";
        case ElixirType::PURIFICATION: return "purification";
        default:                       return "unknown";
    }
}

// ============================================================
// ElixirRecipe — definition of an elixir/potion recipe
// ============================================================
//
// An elixir recipe defines what the potion does when consumed.
// Initiation elixirs transform a normal organ into a source law organ.
// Tuning elixirs partially reduce sublimation degree.
// Purification elixirs fully revert an organ to normal.

struct ElixirRecipe {
    const char* id = "";
    const char* display_name = "";
    ElixirType type = ElixirType::INITIATION;

    // Target sublimation path (for initiation elixirs).
    SublimationPath target_path = SublimationPath::NONE;

    // Target organ slot (for initiation/organ-specific elixirs).
    // Set to COUNT to mean "any eligible slot".
    OrganSlot target_slot = OrganSlot::COUNT;

    // Primary element imbued by this elixir.
    magic::RuneElement primary_element = magic::RuneElement::EARTH;

    // Source reserve cost to apply the elixir's effect.
    int source_cost = 0;

    // Stability modifier applied on consumption.
    float stability_modifier = 0.0f;

    // Mutation modifier applied on consumption.
    float mutation_modifier = 0.0f;

    // For tuning elixirs: how much sublimation degree to reduce.
    int tuning_degree = 0;

    // Rune elements required for calibration (optional, for recipe matching).
    std::vector<magic::RuneElement> required_rune_elements;
};

} // namespace science_and_theology::source_law
