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

// ---- Tree Species Items ----
inline constexpr ItemId OAK_LOG          = kNonMaterialItemBase + 60;
inline constexpr ItemId OAK_PLANK        = kNonMaterialItemBase + 61;
inline constexpr ItemId OAK_SAPLING      = kNonMaterialItemBase + 62;
inline constexpr ItemId BIRCH_LOG        = kNonMaterialItemBase + 63;
inline constexpr ItemId BIRCH_PLANK      = kNonMaterialItemBase + 64;
inline constexpr ItemId BIRCH_SAPLING    = kNonMaterialItemBase + 65;
inline constexpr ItemId SPRUCE_LOG       = kNonMaterialItemBase + 66;
inline constexpr ItemId SPRUCE_PLANK     = kNonMaterialItemBase + 67;
inline constexpr ItemId SPRUCE_SAPLING   = kNonMaterialItemBase + 68;
inline constexpr ItemId ACACIA_LOG       = kNonMaterialItemBase + 69;
inline constexpr ItemId ACACIA_PLANK     = kNonMaterialItemBase + 70;
inline constexpr ItemId ACACIA_SAPLING   = kNonMaterialItemBase + 71;
inline constexpr ItemId MAPLE_LOG        = kNonMaterialItemBase + 72;
inline constexpr ItemId MAPLE_PLANK      = kNonMaterialItemBase + 73;
inline constexpr ItemId MAPLE_SAPLING    = kNonMaterialItemBase + 74;
inline constexpr ItemId SEQUOIA_LOG      = kNonMaterialItemBase + 75;
inline constexpr ItemId SEQUOIA_PLANK    = kNonMaterialItemBase + 76;
inline constexpr ItemId SEQUOIA_SAPLING  = kNonMaterialItemBase + 77;
inline constexpr ItemId CHERRY_LOG       = kNonMaterialItemBase + 78;
inline constexpr ItemId CHERRY_PLANK     = kNonMaterialItemBase + 79;
inline constexpr ItemId CHERRY_SAPLING   = kNonMaterialItemBase + 80;
inline constexpr ItemId CHERRY_FRUIT     = kNonMaterialItemBase + 81;
inline constexpr ItemId OLIVE_LOG        = kNonMaterialItemBase + 82;
inline constexpr ItemId OLIVE_PLANK      = kNonMaterialItemBase + 83;
inline constexpr ItemId OLIVE_SAPLING    = kNonMaterialItemBase + 84;
inline constexpr ItemId OLIVE_FRUIT      = kNonMaterialItemBase + 85;

// Total number of non-material items.
inline constexpr ItemId kNonMaterialItemCount = 86;

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
    // 55-59: unused
    nullptr, nullptr, nullptr, nullptr, nullptr,
    // 60-62: Oak
    "log.oak", "plank.oak", "sapling.oak",
    // 63-65: Birch
    "log.birch", "plank.birch", "sapling.birch",
    // 66-68: Spruce
    "log.spruce", "plank.spruce", "sapling.spruce",
    // 69-71: Acacia
    "log.acacia", "plank.acacia", "sapling.acacia",
    // 72-74: Maple
    "log.maple", "plank.maple", "sapling.maple",
    // 75-77: Sequoia
    "log.sequoia", "plank.sequoia", "sapling.sequoia",
    // 78-81: Cherry (with fruit)
    "log.cherry", "plank.cherry", "sapling.cherry", "fruit.cherry",
    // 82-85: Olive (with fruit)
    "log.olive", "plank.olive", "sapling.olive", "fruit.olive",
};

// Display names for non-material items.
// Indexed by (item_id - kNonMaterialItemBase).
constexpr const char* kNonMaterialItemTitleKeys[] = {
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
    // 55-59: unused
    nullptr, nullptr, nullptr, nullptr, nullptr,
    // 60-62: Oak
    "log.oak", "plank.oak", "sapling.oak",
    // 63-65: Birch
    "log.birch", "plank.birch", "sapling.birch",
    // 66-68: Spruce
    "log.spruce", "plank.spruce", "sapling.spruce",
    // 69-71: Acacia
    "log.acacia", "plank.acacia", "sapling.acacia",
    // 72-74: Maple
    "log.maple", "plank.maple", "sapling.maple",
    // 75-77: Sequoia
    "log.sequoia", "plank.sequoia", "sapling.sequoia",
    // 78-81: Cherry (with fruit)
    "log.cherry", "plank.cherry", "sapling.cherry", "fruit.cherry",
    // 82-85: Olive (with fruit)
    "log.olive", "plank.olive", "sapling.olive", "fruit.olive",
};

static_assert(sizeof(kNonMaterialItemKeys) / sizeof(kNonMaterialItemKeys[0]) ==
              kNonMaterialItemCount,
              "kNonMaterialItemKeys must have one entry per non-material item");

static_assert(sizeof(kNonMaterialItemTitleKeys) / sizeof(kNonMaterialItemTitleKeys[0]) ==
              kNonMaterialItemCount,
              "kNonMaterialItemTitleKeys must have one entry per non-material item");

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
inline const char* get_non_material_item_title_key(ItemId item_id) {
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
    return kNonMaterialItemTitleKeys[idx];
}

// Look up a non-material item by stable key.
inline ItemId get_non_material_item_id_by_key(const char* key) {
    if (key == nullptr || key[0] == '\0') return kInvalidItemId;
    for (uint32_t i = 0; i < kNonMaterialItemCount; ++i) {
        const char* item_key = kNonMaterialItemKeys[i];
        if (item_key != nullptr && std::strcmp(item_key, key) == 0) {
            ItemId item_id = kNonMaterialItemBase + i;
            return get_non_material_item_title_key(item_id) != nullptr
                ? item_id
                : kInvalidItemId;
        }
    }
    return kInvalidItemId;
}

} // namespace science_and_theology::gt
