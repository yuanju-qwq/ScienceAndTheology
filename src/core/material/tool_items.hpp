#pragma once

#include "material_item.hpp"

#include <cstring>

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

// ---- AE2 Pattern Items ----
inline constexpr ItemId BLANK_PATTERN    = kNonMaterialItemBase + 35;

// ---- Player Tools ----
inline constexpr ItemId WOODEN_PICKAXE   = kNonMaterialItemBase + 36;
inline constexpr ItemId STONE_PICKAXE    = kNonMaterialItemBase + 37;
inline constexpr ItemId IRON_PICKAXE     = kNonMaterialItemBase + 38;
inline constexpr ItemId DIAMOND_PICKAXE  = kNonMaterialItemBase + 39;
inline constexpr ItemId WOODEN_AXE       = kNonMaterialItemBase + 40;
inline constexpr ItemId STONE_AXE        = kNonMaterialItemBase + 41;
inline constexpr ItemId IRON_AXE         = kNonMaterialItemBase + 42;
inline constexpr ItemId DIAMOND_AXE      = kNonMaterialItemBase + 43;
inline constexpr ItemId WOODEN_SHOVEL    = kNonMaterialItemBase + 44;
inline constexpr ItemId STONE_SHOVEL     = kNonMaterialItemBase + 45;
inline constexpr ItemId IRON_SHOVEL      = kNonMaterialItemBase + 46;
inline constexpr ItemId DIAMOND_SHOVEL   = kNonMaterialItemBase + 47;
inline constexpr ItemId WOODEN_SWORD     = kNonMaterialItemBase + 48;
inline constexpr ItemId STONE_SWORD      = kNonMaterialItemBase + 49;
inline constexpr ItemId IRON_SWORD       = kNonMaterialItemBase + 50;
inline constexpr ItemId DIAMOND_SWORD    = kNonMaterialItemBase + 51;

// ---- Survival Items ----
inline constexpr ItemId WORKBENCH_ITEM   = kNonMaterialItemBase + 52;
inline constexpr ItemId FURNACE_ITEM     = kNonMaterialItemBase + 53;
inline constexpr ItemId LADDER_ITEM      = kNonMaterialItemBase + 54;

// Total number of non-material items.
inline constexpr ItemId kNonMaterialItemCount = 55;

// Encoded patterns use dynamic IDs in [ENCODED_PATTERN_BASE, ...).
inline constexpr ItemId ENCODED_PATTERN_BASE = kNonMaterialItemBase + kNonMaterialItemCount;
inline constexpr ItemId kNonMaterialItemMax =
    kNonMaterialItemBase + kNonMaterialItemCount;

// Stable content keys for non-material items.
// Indexed by (item_id - kNonMaterialItemBase).
constexpr const char* kNonMaterialItemKeys[] = {
    // 0-8: Tools
    "gt_hammer", "gt_wrench", "gt_file", "gt_screwdriver", "gt_saw",
    "gt_wire_cutter", "gt_crowbar", "gt_soft_mallet", "gt_hard_hammer",
    // 9: unused
    nullptr,
    // 10-17: Machine Components
    "machine_hull_basic", "machine_hull_advanced",
    "electric_motor_lv", "electric_piston_lv", "robot_arm_lv",
    "conveyor_module_lv", "pump_lv", "empty_fluid_cell",
    // 18-19: unused
    nullptr, nullptr,
    // 20-24: Circuits
    "vacuum_tube", "circuit_primitive", "circuit_basic",
    "circuit_good", "circuit_advanced",
    // 25-29: unused
    nullptr, nullptr, nullptr, nullptr, nullptr,
    // 30-34: Misc
    "coal_block", "coke_brick", "firebrick", "stone_plate", "wood_plate",
    // 35: AE2 Pattern
    "blank_pattern",
    // 36-39: Pickaxes
    "wooden_pickaxe", "stone_pickaxe", "iron_pickaxe", "diamond_pickaxe",
    // 40-43: Axes
    "wooden_axe", "stone_axe", "iron_axe", "diamond_axe",
    // 44-47: Shovels
    "wooden_shovel", "stone_shovel", "iron_shovel", "diamond_shovel",
    // 48-51: Swords
    "wooden_sword", "stone_sword", "iron_sword", "diamond_sword",
    // 52: Survival
    "workbench",
    // 53-54: New placeable items
    "stone_furnace",
    "ladder",
};

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
    // 35: AE2 Pattern
    "Blank Pattern",
    // 36-39: Pickaxes
    "Wooden Pickaxe", "Stone Pickaxe", "Iron Pickaxe", "Diamond Pickaxe",
    // 40-43: Axes
    "Wooden Axe", "Stone Axe", "Iron Axe", "Diamond Axe",
    // 44-47: Shovels
    "Wooden Shovel", "Stone Shovel", "Iron Shovel", "Diamond Shovel",
    // 48-51: Swords
    "Wooden Sword", "Stone Sword", "Iron Sword", "Diamond Sword",
    // 52: Survival
    "Workbench",
    // 53-54: New placeable items
    "Stone Furnace",
    "Ladder",
};

static_assert(sizeof(kNonMaterialItemKeys) / sizeof(kNonMaterialItemKeys[0]) ==
              kNonMaterialItemCount,
              "kNonMaterialItemKeys must have one entry per non-material item");

static_assert(sizeof(kNonMaterialItemNames) / sizeof(kNonMaterialItemNames[0]) ==
              kNonMaterialItemCount,
              "kNonMaterialItemNames must have one entry per non-material item");

// Look up the stable key of a non-material item.
inline const char* get_non_material_item_key(ItemId item_id) {
    if (item_id < kNonMaterialItemBase || item_id >= kNonMaterialItemMax) {
        return nullptr;
    }
    uint32_t idx = static_cast<uint32_t>(item_id - kNonMaterialItemBase);
    if (idx >= kNonMaterialItemCount) return nullptr;
    return kNonMaterialItemKeys[idx];
}

// Look up the display name of a non-material item.
inline const char* get_non_material_item_name(ItemId item_id) {
    if (item_id >= ENCODED_PATTERN_BASE) {
        // Check PatternDataCache for encoded pattern names.
        // Forward-declared; the cache has its own name lookup.
        return nullptr; // Handled by ItemRegistry fallback.
    }
    if (item_id < kNonMaterialItemBase || item_id >= kNonMaterialItemMax) {
        return nullptr;
    }
    uint32_t idx = static_cast<uint32_t>(item_id - kNonMaterialItemBase);
    if (idx >= kNonMaterialItemCount) return nullptr;
    return kNonMaterialItemNames[idx];
}

// Look up a non-material item by stable key.
inline ItemId get_non_material_item_id_by_key(const char* key) {
    if (key == nullptr || key[0] == '\0') return kInvalidItemId;
    for (uint32_t i = 0; i < kNonMaterialItemCount; ++i) {
        const char* item_key = kNonMaterialItemKeys[i];
        if (item_key != nullptr && std::strcmp(item_key, key) == 0) {
            ItemId item_id = kNonMaterialItemBase + i;
            return get_non_material_item_name(item_id) != nullptr
                ? item_id
                : kInvalidItemId;
        }
    }
    return kInvalidItemId;
}

} // namespace science_and_theology::gt
