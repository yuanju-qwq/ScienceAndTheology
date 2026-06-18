#pragma once

#include <cstdint>

namespace science_and_theology::gt {

// Physical form of a material. Mirrors GT's OrePrefix system.
// Each form defines how a material is shaped and how much material it contains.
enum class MaterialForm : uint16_t {
    // Raw / unrefined
    DUST = 0,
    TINY_DUST,
    SMALL_DUST,
    IMPURE_DUST,
    PURIFIED_DUST,
    CRUSHED,
    CRUSHED_PURIFIED,
    CRUSHED_CENTRIFUGED,

    // Gem / crystal
    GEM,
    FLAWED_GEM,
    FLAWLESS_GEM,
    EXQUISITE_GEM,

    // Refined metal
    INGOT,
    INGOT_HOT,
    NUGGET,
    BLOCK,

    // Plates
    PLATE,
    DOUBLE_PLATE,
    DENSE_PLATE,

    // Rods
    ROD,
    LONG_ROD,

    // Fasteners
    BOLT,
    SCREW,

    // Mechanical
    RING,
    ROTOR,
    GEAR,
    SMALL_GEAR,

    // Wire / cable
    WIRE_FINE,
    WIRE,

    // Cells (fluid containers)
    CELL,
    PLASMA_CELL,

    COUNT
};

// How much material (in "material units", mb equivalent) each form represents.
// 1 ingot = 144 units (matching GT's 144mb per ingot).
constexpr int64_t kMaterialAmounts[] = {
    144,  // DUST
    16,   // TINY_DUST
    36,   // SMALL_DUST
    144,  // IMPURE_DUST
    144,  // PURIFIED_DUST
    144,  // CRUSHED
    144,  // CRUSHED_PURIFIED
    144,  // CRUSHED_CENTRIFUGED
    144,  // GEM
    144,  // FLAWED_GEM
    144,  // FLAWLESS_GEM
    144,  // EXQUISITE_GEM
    144,  // INGOT
    144,  // INGOT_HOT
    16,   // NUGGET
    1296, // BLOCK (9 ingots)
    144,  // PLATE
    288,  // DOUBLE_PLATE
    1296, // DENSE_PLATE (9 plates)
    72,   // ROD
    144,  // LONG_ROD
    8,    // BOLT
    8,    // SCREW
    36,   // RING
    576,  // ROTOR
    576,  // GEAR
    72,   // SMALL_GEAR
    72,   // WIRE_FINE
    144,  // WIRE
    144,  // CELL
    144,  // PLASMA_CELL
};

static_assert(sizeof(kMaterialAmounts) / sizeof(kMaterialAmounts[0]) ==
              static_cast<size_t>(MaterialForm::COUNT),
              "kMaterialAmounts must have one entry per MaterialForm");

// Human-readable name for each form (GDScript-facing).
constexpr const char* kFormNames[] = {
    "dust", "tiny_dust", "small_dust",
    "impure_dust", "purified_dust",
    "crushed", "crushed_purified", "crushed_centrifuged",
    "gem", "flawed_gem", "flawless_gem", "exquisite_gem",
    "ingot", "ingot_hot", "nugget", "block",
    "plate", "double_plate", "dense_plate",
    "rod", "long_rod",
    "bolt", "screw",
    "ring", "rotor", "gear", "small_gear",
    "wire_fine", "wire",
    "cell", "plasma_cell",
};

// Translation key for each form.
constexpr const char* kFormTitleKeys[] = {
    "Dust", "Tiny Dust", "Small Dust",
    "Impure Dust", "Purified Dust",
    "Crushed Ore", "Purified Crushed", "Centrifuged Crushed",
    "Gem", "Flawed Gem", "Flawless Gem", "Exquisite Gem",
    "Ingot", "Hot Ingot", "Nugget", "Block",
    "Plate", "Double Plate", "Dense Plate",
    "Rod", "Long Rod",
    "Bolt", "Screw",
    "Ring", "Rotor", "Gear", "Small Gear",
    "Fine Wire", "Wire",
    "Cell", "Plasma Cell",
};

// Category for UI grouping.
enum class FormCategory : uint8_t {
    RAW,       // dusts, crushed ores
    GEM,       // gems
    REFINED,   // ingots, nuggets, blocks
    PLATE,     // plates
    ROD,       // rods
    FASTENER,  // bolts, screws
    MECHANISM, // rings, rotors, gears
    WIRE,      // wires
    FLUID,     // cells
};

constexpr FormCategory kFormCategories[] = {
    FormCategory::RAW,       // DUST
    FormCategory::RAW,       // TINY_DUST
    FormCategory::RAW,       // SMALL_DUST
    FormCategory::RAW,       // IMPURE_DUST
    FormCategory::RAW,       // PURIFIED_DUST
    FormCategory::RAW,       // CRUSHED
    FormCategory::RAW,       // CRUSHED_PURIFIED
    FormCategory::RAW,       // CRUSHED_CENTRIFUGED
    FormCategory::GEM,       // GEM
    FormCategory::GEM,       // FLAWED_GEM
    FormCategory::GEM,       // FLAWLESS_GEM
    FormCategory::GEM,       // EXQUISITE_GEM
    FormCategory::REFINED,   // INGOT
    FormCategory::REFINED,   // INGOT_HOT
    FormCategory::REFINED,   // NUGGET
    FormCategory::REFINED,   // BLOCK
    FormCategory::PLATE,     // PLATE
    FormCategory::PLATE,     // DOUBLE_PLATE
    FormCategory::PLATE,     // DENSE_PLATE
    FormCategory::ROD,       // ROD
    FormCategory::ROD,       // LONG_ROD
    FormCategory::FASTENER,  // BOLT
    FormCategory::FASTENER,  // SCREW
    FormCategory::MECHANISM, // RING
    FormCategory::MECHANISM, // ROTOR
    FormCategory::MECHANISM, // GEAR
    FormCategory::MECHANISM, // SMALL_GEAR
    FormCategory::WIRE,      // WIRE_FINE
    FormCategory::WIRE,      // WIRE
    FormCategory::FLUID,     // CELL
    FormCategory::FLUID,     // PLASMA_CELL
};

// Returns true if the form represents a dust-type item.
inline constexpr bool is_dust_form(MaterialForm form) {
    return kFormCategories[static_cast<uint16_t>(form)] == FormCategory::RAW;
}

// Returns the material amount for a given form.
inline constexpr int64_t get_material_amount(MaterialForm form) {
    return kMaterialAmounts[static_cast<uint16_t>(form)];
}

// Returns the form name (snake_case, GDScript-friendly).
inline constexpr const char* get_form_name(MaterialForm form) {
    return kFormNames[static_cast<uint16_t>(form)];
}

// Returns the translation key.
inline constexpr const char* get_form_title_key(MaterialForm form) {
    return kFormTitleKeys[static_cast<uint16_t>(form)];
}

} // namespace science_and_theology::gt
