#pragma once

#include "material_item.hpp"

namespace science_and_theology::gt {

// Non-material item ID constants.
// These start at kNonMaterialItemBase and are assigned sequentially.
// Tools, machine components, circuits, and other non-material items.

// ---- GT Tools ----
inline constexpr ItemId GT_HAMMER         = kNonMaterialItemBase + 0;
inline constexpr ItemId GT_WRENCH         = kNonMaterialItemBase + 1;
inline constexpr ItemId GT_FILE           = kNonMaterialItemBase + 2;
inline constexpr ItemId GT_SCREWDRIVER    = kNonMaterialItemBase + 3;
inline constexpr ItemId GT_SAW            = kNonMaterialItemBase + 4;
inline constexpr ItemId GT_WIRE_CUTTER    = kNonMaterialItemBase + 5;
inline constexpr ItemId GT_CROWBAR        = kNonMaterialItemBase + 6;
inline constexpr ItemId GT_SOFT_MALLET    = kNonMaterialItemBase + 7;
inline constexpr ItemId GT_HARD_HAMMER    = kNonMaterialItemBase + 8;

// ---- Machine Components ----
inline constexpr ItemId MACHINE_HULL_BASIC     = kNonMaterialItemBase + 10;
inline constexpr ItemId MACHINE_HULL_ADVANCED  = kNonMaterialItemBase + 11;
inline constexpr ItemId ELECTRIC_MOTOR_LV      = kNonMaterialItemBase + 12;
inline constexpr ItemId ELECTRIC_PISTON_LV     = kNonMaterialItemBase + 13;
inline constexpr ItemId ROBOT_ARM_LV           = kNonMaterialItemBase + 14;
inline constexpr ItemId CONVEYOR_MODULE_LV     = kNonMaterialItemBase + 15;
inline constexpr ItemId PUMP_LV                = kNonMaterialItemBase + 16;
inline constexpr ItemId EMPTY_FLUID_CELL       = kNonMaterialItemBase + 17;

// ---- Circuits ----
inline constexpr ItemId VACUUM_TUBE         = kNonMaterialItemBase + 20;
inline constexpr ItemId CIRCUIT_PRIMITIVE   = kNonMaterialItemBase + 21;
inline constexpr ItemId CIRCUIT_BASIC       = kNonMaterialItemBase + 22;
inline constexpr ItemId CIRCUIT_GOOD        = kNonMaterialItemBase + 23;
inline constexpr ItemId CIRCUIT_ADVANCED    = kNonMaterialItemBase + 24;

// ---- Misc ----
inline constexpr ItemId COAL_BLOCK       = kNonMaterialItemBase + 30;
inline constexpr ItemId COKE_BRICK       = kNonMaterialItemBase + 31;
inline constexpr ItemId FIREBRICK        = kNonMaterialItemBase + 32;
inline constexpr ItemId STONE_PLATE      = kNonMaterialItemBase + 33;
inline constexpr ItemId WOOD_PLATE       = kNonMaterialItemBase + 34;

// Total number of non-material items.
inline constexpr ItemId kNonMaterialItemCount = 35;
inline constexpr ItemId kNonMaterialItemMax =
    kNonMaterialItemBase + kNonMaterialItemCount;

// Display names for non-material items.
// Indexed by (item_id - kNonMaterialItemBase).
constexpr const char* kNonMaterialItemNames[] = {
    // 0-8: Tools
    "Hammer", "Wrench", "File", "Screwdriver", "Saw",
    "Wire Cutter", "Crowbar", "Soft Mallet", "Hard Hammer",
    // 9: unused
    nullptr,
    // 10-17: Machine Components
    "Basic Machine Hull", "Advanced Machine Hull",
    "LV Electric Motor", "LV Electric Piston", "LV Robot Arm",
    "LV Conveyor Module", "LV Pump", "Empty Fluid Cell",
    // 18-19: unused
    nullptr, nullptr,
    // 20-24: Circuits
    "Vacuum Tube", "Primitive Circuit", "Basic Circuit",
    "Good Circuit", "Advanced Circuit",
    // 25-29: unused
    nullptr, nullptr, nullptr, nullptr, nullptr,
    // 30-34: Misc
    "Coal Block", "Coke Brick", "Firebrick", "Stone Plate", "Wood Plate",
};

// Look up the display name of a non-material item.
inline const char* get_non_material_item_name(ItemId item_id) {
    if (item_id < kNonMaterialItemBase || item_id >= kNonMaterialItemMax) {
        return nullptr;
    }
    uint32_t idx = static_cast<uint32_t>(item_id - kNonMaterialItemBase);
    if (idx >= kNonMaterialItemCount) return nullptr;
    return kNonMaterialItemNames[idx];
}

} // namespace science_and_theology::gt
