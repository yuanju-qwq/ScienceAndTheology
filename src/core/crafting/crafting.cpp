#include "crafting.hpp"
#include "material/material.hpp"
#include "material/tool_items.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace science_and_theology::gt {

// ============================================================
// CraftingManager — internal registry
// ============================================================

std::vector<CraftingRecipe>& CraftingManager::registry() {
    static std::vector<CraftingRecipe> recipes;
    return recipes;
}

void CraftingManager::initialize() {
    registry().clear();
}

void CraftingManager::add_recipe(const CraftingRecipe& recipe) {
    registry().push_back(recipe);
}

const CraftingRecipe* CraftingManager::find_recipe(const char* name) {
    for (const auto& recipe : registry()) {
        if (std::strcmp(recipe.name, name) == 0) {
            return &recipe;
        }
    }
    return nullptr;
}

std::vector<const CraftingRecipe*> CraftingManager::get_by_category(
        const char* category) {
    std::vector<const CraftingRecipe*> result;
    for (const auto& recipe : registry()) {
        if (std::strcmp(recipe.category, category) == 0) {
            result.push_back(&recipe);
        }
    }
    return result;
}

std::vector<const CraftingRecipe*> CraftingManager::get_recipes_for_station(
        const char* station) {
    std::vector<const CraftingRecipe*> result;
    for (const auto& recipe : registry()) {
        if (std::strcmp(recipe.required_station, station) == 0) {
            result.push_back(&recipe);
        }
    }
    return result;
}

std::vector<const CraftingRecipe*> CraftingManager::get_all_recipes() {
    std::vector<const CraftingRecipe*> result;
    for (const auto& recipe : registry()) {
        result.push_back(&recipe);
    }
    return result;
}

const std::vector<CraftingRecipe>& CraftingManager::get_registry() {
    return registry();
}

size_t CraftingManager::get_recipe_count() {
    return registry().size();
}

// ============================================================
// Basic recipe registration
// ============================================================

void CraftingManager::register_basic_recipes() {
    using R = CraftingRecipe;

    // ---- Helpers ----

    // Get ItemId from material name + form.
    auto id = [](const char* mat_name, MaterialForm form) -> ItemId {
        const Material* mat = get_material(mat_name);
        if (mat == nullptr) return kInvalidItemId;
        return ItemRegistry::get_item_id(mat, form);
    };

    // Non-material item shorthand.
    auto nm = [](ItemId item_id, int64_t count = 1) -> RecipeInput {
        return ResourceStack::item(item_id, count);
    };

    // Create a recipe.
    auto recipe = [](const char* name, const char* category,
                     const char* station,
                     std::initializer_list<RecipeInput> inputs,
                     ItemId output_id, int64_t output_count,
                     const char* tool = nullptr) -> R {
        R r;
        r.name = name;
        r.category = category;
        r.required_station = station;
        for (const auto& in : inputs) r.inputs.push_back(in);
        r.output = ResourceStack::item(output_id, output_count);
        r.required_tool = tool;
        return r;
    };

    // Station helpers
    auto hand = [&](const char* name, const char* category,
                    std::initializer_list<RecipeInput> inputs,
                    ItemId output_id, int64_t output_count,
                    const char* tool = nullptr) -> R {
        return recipe(name, category, "", inputs, output_id, output_count, tool);
    };
    auto bench = [&](const char* name, const char* category,
                     std::initializer_list<RecipeInput> inputs,
                     ItemId output_id, int64_t output_count,
                     const char* tool = nullptr) -> R {
        return recipe(name, category, "workbench", inputs, output_id, output_count, tool);
    };

    // ========================================================
    // Category: "materials" — compression / decompression (HAND)
    // ========================================================

    const char* compressible_metals[] = {
        "iron", "copper", "tin", "lead", "bronze", "steel",
        "gold", "silver", "nickel", "zinc", "aluminum", "brass",
        "invar", "electrum", "wrought_iron", "bismuth", "antimony",
    };
    const int num_metals =
        sizeof(compressible_metals) / sizeof(compressible_metals[0]);

    for (int i = 0; i < num_metals; ++i) {
        const char* mat = compressible_metals[i];
        ItemId nugget = id(mat, MaterialForm::NUGGET);
        ItemId ingot  = id(mat, MaterialForm::INGOT);
        if (nugget != kInvalidItemId && ingot != kInvalidItemId) {
            add_recipe(hand("compress_nugget_to_ingot", "materials",
                            {nm(nugget, 9)}, ingot, 1));
            add_recipe(hand("decompress_ingot_to_nugget", "materials",
                            {nm(ingot, 1)}, nugget, 9));
        }
    }

    for (int i = 0; i < num_metals; ++i) {
        const char* mat = compressible_metals[i];
        ItemId ingot = id(mat, MaterialForm::INGOT);
        ItemId block = id(mat, MaterialForm::BLOCK);
        if (ingot != kInvalidItemId && block != kInvalidItemId) {
            add_recipe(hand("compress_ingot_to_block", "materials",
                            {nm(ingot, 9)}, block, 1));
            add_recipe(hand("decompress_block_to_ingot", "materials",
                            {nm(block, 1)}, ingot, 9));
        }
    }

    const char* dust_materials[] = {
        "coal", "diamond", "emerald", "lapis", "redstone",
        "glowstone", "sulfur", "saltpeter",
    };
    const int num_dust =
        sizeof(dust_materials) / sizeof(dust_materials[0]);
    for (int i = 0; i < num_dust; ++i) {
        const char* mat = dust_materials[i];
        ItemId dust  = id(mat, MaterialForm::DUST);
        ItemId block = id(mat, MaterialForm::BLOCK);
        if (dust != kInvalidItemId && block != kInvalidItemId) {
            add_recipe(hand("compress_dust_to_block", "materials",
                            {nm(dust, 4)}, block, 1));
        }
    }

    // Coal block decompression (hand).
    {
        ItemId coal_block = id("coal", MaterialForm::BLOCK);
        ItemId coal_gem   = id("coal", MaterialForm::GEM);
        if (coal_block != kInvalidItemId && coal_gem != kInvalidItemId) {
            add_recipe(hand("decompress_coal_block", "materials",
                            {nm(coal_block, 1)}, coal_gem, 9));
        }
    }

    // ========================================================
    // Category: "tools" — basic GT tools (WORKBENCH)
    // ========================================================

    ItemId iron_ingot  = id("iron", MaterialForm::INGOT);
    ItemId iron_plate  = id("iron", MaterialForm::PLATE);
    ItemId iron_rod    = id("iron", MaterialForm::ROD);
    ItemId iron_block  = id("iron", MaterialForm::BLOCK);
    ItemId iron_screw  = id("iron", MaterialForm::SCREW);
    ItemId wood_plank  = id("wood", MaterialForm::PLATE);
    ItemId stick       = id("wood", MaterialForm::ROD);

    if (iron_ingot != kInvalidItemId && stick != kInvalidItemId) {
        // Hammer: 4 iron ingots + 2 sticks (was 3×2 shaped, differentiated from wrench by count).
        add_recipe(bench("craft_hammer", "tools",
                         {nm(iron_ingot, 4), nm(stick, 2)},
                         GT_HAMMER, 1));

        // Wrench: 3 iron ingots + 1 stick.
        add_recipe(bench("craft_wrench", "tools",
                         {nm(iron_ingot, 3), nm(stick, 1)},
                         GT_WRENCH, 1));

        // Screwdriver: 1 iron ingot + 1 stick.
        add_recipe(bench("craft_screwdriver", "tools",
                         {nm(iron_ingot, 1), nm(stick, 1)},
                         GT_SCREWDRIVER, 1));

        // Crowbar: 2 iron ingots + 1 stick.
        add_recipe(bench("craft_crowbar", "tools",
                         {nm(iron_ingot, 2), nm(stick, 1)},
                         GT_CROWBAR, 1));
    }

    if (iron_plate != kInvalidItemId && stick != kInvalidItemId) {
        // Saw: 2 iron plates + 1 iron ingot + 1 stick.
        add_recipe(bench("craft_saw", "tools",
                         {nm(iron_plate, 2), nm(iron_ingot, 1), nm(stick, 1)},
                         GT_SAW, 1));

        // File: 2 iron plates + 1 stick.
        add_recipe(bench("craft_file", "tools",
                         {nm(iron_plate, 2), nm(stick, 1)},
                         GT_FILE, 1));

        // Wire Cutter: 3 iron plates + 1 stick.
        add_recipe(bench("craft_wire_cutter", "tools",
                         {nm(iron_plate, 3), nm(stick, 1)},
                         GT_WIRE_CUTTER, 1));
    }

    // Soft Mallet: 3 wood planks + 1 stick.
    if (wood_plank != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(bench("craft_soft_mallet", "tools",
                         {nm(wood_plank, 3), nm(stick, 1)},
                         GT_SOFT_MALLET, 1));
    }

    // Hard Hammer: 3 iron blocks + 1 stick.
    if (iron_block != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(bench("craft_hard_hammer", "tools",
                         {nm(iron_block, 3), nm(stick, 1)},
                         GT_HARD_HAMMER, 1));
    }

    // ========================================================
    // Category: "parts" — basic component parts (WORKBENCH)
    // ========================================================

    if (iron_ingot != kInvalidItemId && iron_rod != kInvalidItemId) {
        add_recipe(bench("craft_iron_rod", "parts",
                         {nm(iron_ingot, 1)}, iron_rod, 2,
                         "file"));

        add_recipe(bench("craft_iron_plate", "parts",
                         {nm(iron_ingot, 1)}, iron_plate, 2,
                         "hammer"));

        if (iron_screw != kInvalidItemId) {
            add_recipe(bench("craft_iron_screw", "parts",
                             {nm(iron_rod, 1)}, iron_screw, 4,
                             "file"));
        }
    }

    // ========================================================
    // Category: "wires" — wire cutting (WORKBENCH)
    // ========================================================

    const char* wire_metals[] = {"copper", "tin", "gold", "silver", "aluminum",
                                 "nickel", "lead", "zinc", "iron", "steel"};
    const int num_wire = sizeof(wire_metals) / sizeof(wire_metals[0]);
    for (int i = 0; i < num_wire; ++i) {
        const char* mat = wire_metals[i];
        ItemId ingot = id(mat, MaterialForm::INGOT);
        ItemId wire  = id(mat, MaterialForm::WIRE);
        if (ingot != kInvalidItemId && wire != kInvalidItemId) {
            add_recipe(bench("craft_wire", "wires",
                             {nm(ingot, 1)}, wire, 2,
                             "wire_cutter"));
        }
    }

    // ========================================================
    // Category: "cables" — insulated cables (WORKBENCH)
    // ========================================================

    ItemId rubber_sheet = id("rubber", MaterialForm::PLATE);
    if (rubber_sheet != kInvalidItemId) {
        const char* cable_metals[] = {"copper", "tin", "gold", "silver",
                                      "aluminum", "nickel", "iron", "steel"};
        const int num_cable = sizeof(cable_metals) / sizeof(cable_metals[0]);
        for (int i = 0; i < num_cable; ++i) {
            const char* mat = cable_metals[i];
            ItemId wire = id(mat, MaterialForm::WIRE);
            if (wire == kInvalidItemId) continue;
            add_recipe(bench("craft_cable", "cables",
                             {nm(wire, 1), nm(rubber_sheet, 1)},
                             wire, 1));
        }
    }

    // ========================================================
    // Category: "circuits" — electronic components (WORKBENCH)
    // ========================================================

    ItemId copper_wire  = id("copper", MaterialForm::WIRE);
    ItemId redstone_dust = id("redstone", MaterialForm::DUST);
    ItemId glass_plate  = id("glass", MaterialForm::PLATE);
    ItemId steel_plate  = id("steel", MaterialForm::PLATE);
    ItemId gold_wire    = id("gold", MaterialForm::WIRE);
    ItemId lapis_dust   = id("lapis", MaterialForm::DUST);
    ItemId plastic_plate = id("polyethylene", MaterialForm::PLATE);

    // Vacuum Tube: 3 glass plates + 4 copper wire + 1 iron rod + 2 redstone.
    if (glass_plate != kInvalidItemId && copper_wire != kInvalidItemId &&
        iron_rod != kInvalidItemId && redstone_dust != kInvalidItemId) {
        add_recipe(bench("craft_vacuum_tube", "circuits",
                         {nm(glass_plate, 3), nm(copper_wire, 4),
                          nm(iron_rod, 1), nm(redstone_dust, 2)},
                         VACUUM_TUBE, 1));
    }

    // Primitive Circuit: 4 copper wire + 4 redstone + 1 stone plate + 1 iron rod.
    if (copper_wire != kInvalidItemId && redstone_dust != kInvalidItemId &&
        iron_rod != kInvalidItemId) {
        add_recipe(bench("craft_primitive_circuit", "circuits",
                         {nm(copper_wire, 4), nm(redstone_dust, 4),
                          nm(STONE_PLATE, 1), nm(iron_rod, 1)},
                         CIRCUIT_PRIMITIVE, 1));
    }

    // Basic Circuit: 4 gold wire + 4 redstone + 1 primitive circuit + 1 plastic plate.
    if (gold_wire != kInvalidItemId && plastic_plate != kInvalidItemId &&
        redstone_dust != kInvalidItemId) {
        add_recipe(bench("craft_basic_circuit", "circuits",
                         {nm(gold_wire, 4), nm(redstone_dust, 4),
                          nm(CIRCUIT_PRIMITIVE, 1), nm(plastic_plate, 1)},
                         CIRCUIT_BASIC, 1));
    }

    // Good Circuit: 4 copper wire + 4 lapis + 1 basic circuit + 1 steel plate.
    if (lapis_dust != kInvalidItemId && redstone_dust != kInvalidItemId &&
        steel_plate != kInvalidItemId && copper_wire != kInvalidItemId) {
        add_recipe(bench("craft_good_circuit", "circuits",
                         {nm(copper_wire, 4), nm(lapis_dust, 4),
                          nm(CIRCUIT_BASIC, 1), nm(steel_plate, 1)},
                         CIRCUIT_GOOD, 1));
    }

    // ========================================================
    // Category: "machines" — machine blocks & components (WORKBENCH)
    // ========================================================

    // Basic Machine Hull: 8 iron plates.
    if (iron_plate != kInvalidItemId) {
        add_recipe(bench("craft_machine_hull_basic", "machines",
                         {nm(iron_plate, 8)},
                         MACHINE_HULL_BASIC, 1));
    }

    // Advanced Machine Hull: 8 steel plates.
    if (steel_plate != kInvalidItemId) {
        add_recipe(bench("craft_machine_hull_advanced", "machines",
                         {nm(steel_plate, 8)},
                         MACHINE_HULL_ADVANCED, 1));
    }

    // LV Electric Motor: 2 copper wire + 2 iron rods + 1 iron ingot.
    if (iron_rod != kInvalidItemId && copper_wire != kInvalidItemId &&
        iron_ingot != kInvalidItemId) {
        add_recipe(bench("craft_electric_motor_lv", "machines",
                         {nm(copper_wire, 2), nm(iron_rod, 2), nm(iron_ingot, 1)},
                         ELECTRIC_MOTOR_LV, 1));
    }

    // LV Electric Piston: 1 iron plate + 1 iron rod + 1 motor.
    if (iron_plate != kInvalidItemId && iron_rod != kInvalidItemId) {
        add_recipe(bench("craft_electric_piston_lv", "machines",
                         {nm(iron_plate, 1), nm(iron_rod, 1), nm(ELECTRIC_MOTOR_LV, 1)},
                         ELECTRIC_PISTON_LV, 1));
    }

    // LV Robot Arm: 1 piston + 1 motor + 1 primitive circuit + 3 iron rods.
    if (iron_rod != kInvalidItemId) {
        add_recipe(bench("craft_robot_arm_lv", "machines",
                         {nm(ELECTRIC_PISTON_LV, 1), nm(ELECTRIC_MOTOR_LV, 1),
                          nm(CIRCUIT_PRIMITIVE, 1), nm(iron_rod, 3)},
                         ROBOT_ARM_LV, 1));
    }

    // LV Conveyor Module: 2 rubber sheets + 1 motor.
    if (rubber_sheet != kInvalidItemId) {
        add_recipe(bench("craft_conveyor_lv", "machines",
                         {nm(rubber_sheet, 2), nm(ELECTRIC_MOTOR_LV, 1)},
                         CONVEYOR_MODULE_LV, 1));
    }

    // LV Pump: 2 iron plates + 1 rubber sheet + 1 motor.
    if (iron_plate != kInvalidItemId && rubber_sheet != kInvalidItemId) {
        add_recipe(bench("craft_pump_lv", "machines",
                         {nm(iron_plate, 2), nm(rubber_sheet, 1), nm(ELECTRIC_MOTOR_LV, 1)},
                         PUMP_LV, 1));
    }

    // Empty Fluid Cell: 4 tin plates → 4 fluid cells.
    ItemId tin_plate = id("tin", MaterialForm::PLATE);
    if (tin_plate != kInvalidItemId) {
        add_recipe(bench("craft_fluid_cell", "machines",
                         {nm(tin_plate, 4)},
                         EMPTY_FLUID_CELL, 4));
    }

    // ========================================================
    // Category: "misc" — miscellaneous
    // ========================================================

    // Stone Plate: hammer + stone → 1 stone plate (hand).
    ItemId stone = id("stone", MaterialForm::DUST);
    if (stone != kInvalidItemId) {
        add_recipe(hand("craft_stone_plate", "misc",
                        {nm(stone, 1)}, STONE_PLATE, 1,
                        "hammer"));
    }

    // Wood Planks (hand, no tool): 1 wood log → 2 planks.
    ItemId wood_log = id("wood", MaterialForm::DUST);
    if (wood_log != kInvalidItemId && wood_plank != kInvalidItemId) {
        add_recipe(hand("craft_wood_plank_hand", "misc",
                        {nm(wood_log, 1)}, wood_plank, 2));

        // Wood Planks (workbench, with saw): 1 wood log → 4 planks.
        add_recipe(bench("craft_wood_plank", "misc",
                         {nm(wood_log, 1)}, wood_plank, 4,
                         "saw"));
    }

    // Workbench: 4 wood planks (hand).
    if (wood_plank != kInvalidItemId) {
        add_recipe(hand("craft_workbench", "misc",
                        {nm(wood_plank, 4)}, WORKBENCH_ITEM, 1));
    }

    // Sticks: 2 wood planks → 4 sticks (hand).
    if (wood_plank != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(hand("craft_stick", "misc",
                        {nm(wood_plank, 2)}, stick, 4));
    }

    // Coal Block: 9 coal → 1 coal block (hand).
    ItemId coal_gem = id("coal", MaterialForm::GEM);
    ItemId coal_block = id("coal", MaterialForm::BLOCK);
    if (coal_gem != kInvalidItemId && coal_block != kInvalidItemId) {
        add_recipe(hand("craft_coal_block", "misc",
                        {nm(coal_gem, 9)}, coal_block, 1));
    }

    // Firebrick: 4 brick + 1 coal dust → 1 firebrick (hand).
    ItemId brick = id("brick", MaterialForm::INGOT);
    ItemId coal_dust = id("coal", MaterialForm::DUST);
    if (brick != kInvalidItemId && coal_dust != kInvalidItemId) {
        add_recipe(hand("craft_firebrick", "misc",
                        {nm(brick, 4), nm(coal_dust, 1)},
                        FIREBRICK, 1));
    }

    // Stone Furnace: 8 stone dust → 1 furnace (workbench).
    ItemId stone_dust = id("stone", MaterialForm::DUST);
    if (stone_dust != kInvalidItemId) {
        add_recipe(bench("craft_furnace", "misc",
                         {nm(stone_dust, 8)},
                         FURNACE_ITEM, 1));
    }

    // Ladder: 4 sticks → 1 ladder (hand).
    if (stick != kInvalidItemId) {
        add_recipe(hand("craft_ladder", "misc",
                        {nm(stick, 4)},
                        LADDER_ITEM, 1));
    }
}

} // namespace science_and_theology::gt
