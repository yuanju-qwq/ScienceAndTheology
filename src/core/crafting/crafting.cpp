#include "crafting.hpp"
#include "material/material.hpp"
#include "material/tool_items.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace science_and_theology::gt {

// ============================================================
// CraftingGrid
// ============================================================

CraftingGrid::CraftingGrid(int width, int height)
    : width_(std::min(width, kMaxSize))
    , height_(std::min(height, kMaxSize)) {
    clear();
}

ResourceStack& CraftingGrid::slot(int row, int col) {
    assert(row >= 0 && row < height_);
    assert(col >= 0 && col < width_);
    return slots_[row * width_ + col];
}

const ResourceStack& CraftingGrid::slot(int row, int col) const {
    assert(row >= 0 && row < height_);
    assert(col >= 0 && col < width_);
    return slots_[row * width_ + col];
}

ResourceStack& CraftingGrid::slot_at(int index) {
    assert(index >= 0 && index < kMaxSlots);
    return slots_[index];
}

const ResourceStack& CraftingGrid::slot_at(int index) const {
    assert(index >= 0 && index < kMaxSlots);
    return slots_[index];
}

void CraftingGrid::clear() {
    for (int i = 0; i < kMaxSlots; ++i) {
        slots_[i] = ResourceStack{};
    }
}

bool CraftingGrid::is_empty() const {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[i].is_valid()) return false;
    }
    return true;
}

int64_t CraftingGrid::count_item(ItemId item_id) const {
    int64_t total = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[i].item_id() == item_id) {
            total += slots_[i].amount;
        }
    }
    return total;
}

bool CraftingGrid::contains_items(
        const std::vector<RecipeInput>& inputs) const {
    for (const auto& input : inputs) {
        if (!input.is_valid()) continue;
        if (count_item(input.item_id()) < input.amount) return false;
    }
    return true;
}

void CraftingGrid::consume_items(const std::vector<RecipeInput>& inputs) {
    for (const auto& input : inputs) {
        if (!input.is_valid()) continue;

        int64_t remaining = input.amount;
        for (int i = 0; i < kMaxSlots && remaining > 0; ++i) {
            if (slots_[i].item_id() == input.item_id()) {
                int64_t take = std::min(remaining, slots_[i].amount);
                slots_[i].amount -= take;
                remaining -= take;
                if (slots_[i].amount <= 0) {
                    slots_[i] = ResourceStack{};
                }
            }
        }
    }
}

void CraftingGrid::consume_shaped(int pattern_width, int pattern_height,
                                   const ItemId* pattern,
                                   const int64_t* counts,
                                   int offset_row, int offset_col) {
    for (int r = 0; r < pattern_height; ++r) {
        for (int c = 0; c < pattern_width; ++c) {
            int pat_idx = r * pattern_width + c;
            ItemId pat_item = pattern[pat_idx];
            if (pat_item == kInvalidItemId) continue;

            int grid_r = offset_row + r;
            int grid_c = offset_col + c;
            ResourceStack& s = slot(grid_r, grid_c);
            s.amount -= counts[pat_idx];
            if (s.amount <= 0) {
                s = ResourceStack{};
            }
        }
    }
}

// ============================================================
// CraftingManager â€?internal registry
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

// ============================================================
// Shaped matching
// ============================================================

bool CraftingManager::match_shaped_at(const CraftingGrid& grid,
                                       const CraftingRecipe& recipe,
                                       int offset_row, int offset_col) {
    int pw = recipe.grid_width;
    int ph = recipe.grid_height;

    // Recipe must fit within the grid at this offset.
    if (offset_row + ph > grid.height()) return false;
    if (offset_col + pw > grid.width()) return false;

    // Check each cell of the recipe against the grid.
    for (int r = 0; r < grid.height(); ++r) {
        for (int c = 0; c < grid.width(); ++c) {
            bool in_pattern = (r >= offset_row && r < offset_row + ph &&
                               c >= offset_col && c < offset_col + pw);
            int pat_r = r - offset_row;
            int pat_c = c - offset_col;
            int pat_idx = pat_r * pw + pat_c;

            const ResourceStack& slot = grid.slot(r, c);

            if (in_pattern) {
                // Grid slot must match the pattern slot.
                ItemId expected = recipe.pattern[pat_idx];
                int64_t expected_count = recipe.pattern_counts[pat_idx];

                if (expected == kInvalidItemId) {
                    // Pattern expects empty slot.
                    if (slot.is_valid()) return false;
                } else {
                    // Pattern expects this item.
                    if (slot.item_id() != expected) return false;
                    if (slot.amount < expected_count) return false;
                }
            } else {
                // Outside pattern area â€?must be empty.
                if (slot.is_valid()) return false;
            }
        }
    }

    return true;
}

// ============================================================
// Recipe matching
// ============================================================

bool CraftingManager::matches_grid(const CraftingGrid& grid,
                                    const CraftingRecipe& recipe) {
    if (recipe.is_shaped()) {
        // Try all offsets.
        int max_row = grid.height() - recipe.grid_height;
        int max_col = grid.width() - recipe.grid_width;
        for (int r = 0; r <= max_row; ++r) {
            for (int c = 0; c <= max_col; ++c) {
                if (match_shaped_at(grid, recipe, r, c)) {
                    return true;
                }
            }
        }
        return false;
    } else {
        // Shapeless: check all inputs are present.
        return grid.contains_items(recipe.shapeless_inputs);
    }
}

const CraftingRecipe* CraftingManager::find_match(const CraftingGrid& grid) {
    for (const auto& recipe : registry()) {
        if (matches_grid(grid, recipe)) {
            return &recipe;
        }
    }
    return nullptr;
}

// ============================================================
// Craft execution
// ============================================================

ResourceStack CraftingManager::craft(CraftingGrid& grid,
                                      const CraftingRecipe& recipe) {
    if (!matches_grid(grid, recipe)) {
        return ResourceStack{};
    }

    if (recipe.is_shaped()) {
        // Find the offset where it matches, then consume.
        int max_row = grid.height() - recipe.grid_height;
        int max_col = grid.width() - recipe.grid_width;
        for (int r = 0; r <= max_row; ++r) {
            for (int c = 0; c <= max_col; ++c) {
                if (match_shaped_at(grid, recipe, r, c)) {
                    grid.consume_shaped(recipe.grid_width, recipe.grid_height,
                                        recipe.pattern, recipe.pattern_counts,
                                        r, c);
                    return recipe.output;
                }
            }
        }
    } else {
        grid.consume_items(recipe.shapeless_inputs);
        return recipe.output;
    }

    return ResourceStack{};
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

    // Create a shaped recipe.
    auto shaped = [](const char* name, const char* category,
                     int w, int h, std::initializer_list<ItemId> pat,
                     std::initializer_list<int64_t> counts,
                     ItemId output_id, int64_t output_count,
                     const char* tool = nullptr) -> R {
        R r;
        r.name = name;
        r.category = category;
        r.grid_width = w;
        r.grid_height = h;
        int i = 0;
        for (auto id_val : pat)   r.pattern[i] = id_val, ++i;
        i = 0;
        for (auto c : counts) r.pattern_counts[i] = c, ++i;
        r.output = ResourceStack::item(output_id, output_count);
        r.required_tool = tool;
        return r;
    };

    // Create a shapeless recipe.
    auto shapeless = [](const char* name, const char* category,
                        std::initializer_list<RecipeInput> inputs,
                        ItemId output_id, int64_t output_count,
                        const char* tool = nullptr) -> R {
        R r;
        r.name = name;
        r.category = category;
        for (const auto& in : inputs) r.shapeless_inputs.push_back(in);
        r.output = ResourceStack::item(output_id, output_count);
        r.required_tool = tool;
        return r;
    };

    // Non-material item shorthand.
    auto nm = [](ItemId item_id, int64_t count = 1) -> ResourceStack {
        return ResourceStack::item(item_id, count);
    };

    // ========================================================
    // Category: "materials" â€?compression / decompression
    // ========================================================

    // Early-game metals for compression recipes.
    const char* compressible_metals[] = {
        "iron", "copper", "tin", "lead", "bronze", "steel",
        "gold", "silver", "nickel", "zinc", "aluminum", "brass",
        "invar", "electrum", "wrought_iron", "bismuth", "antimony",
    };
    const int num_metals =
        sizeof(compressible_metals) / sizeof(compressible_metals[0]);

    // Nugget â†?Ingot (9 nuggets = 1 ingot).
    for (int i = 0; i < num_metals; ++i) {
        const char* mat = compressible_metals[i];
        ItemId nugget = id(mat, MaterialForm::NUGGET);
        ItemId ingot  = id(mat, MaterialForm::INGOT);
        if (nugget != kInvalidItemId && ingot != kInvalidItemId) {
            add_recipe(shapeless("compress_nugget_to_ingot", "materials",
                                 {nm(nugget, 9)}, ingot, 1));
            add_recipe(shapeless("decompress_ingot_to_nugget", "materials",
                                 {nm(ingot, 1)}, nugget, 9));
        }
    }

    // Ingot â†?Block (9 ingots = 1 block).
    for (int i = 0; i < num_metals; ++i) {
        const char* mat = compressible_metals[i];
        ItemId ingot = id(mat, MaterialForm::INGOT);
        ItemId block = id(mat, MaterialForm::BLOCK);
        if (ingot != kInvalidItemId && block != kInvalidItemId) {
            add_recipe(shapeless("compress_ingot_to_block", "materials",
                                 {nm(ingot, 9)}, block, 1));
            add_recipe(shapeless("decompress_block_to_ingot", "materials",
                                 {nm(block, 1)}, ingot, 9));
        }
    }

    // Dust â†?Block (4 dust = 1 block, for gem/dust materials).
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
            add_recipe(shapeless("compress_dust_to_block", "materials",
                                 {nm(dust, 4)}, block, 1));
        }
    }

    // Coal block decompression.
    {
        ItemId coal_block = id("coal", MaterialForm::BLOCK);
        ItemId coal_gem   = id("coal", MaterialForm::GEM);
        if (coal_block != kInvalidItemId && coal_gem != kInvalidItemId) {
            add_recipe(shapeless("decompress_coal_block", "materials",
                                 {nm(coal_block, 1)}, coal_gem, 9));
        }
    }

    // ========================================================
    // Category: "tools" â€?basic GT tools
    // ========================================================

    ItemId iron_ingot  = id("iron", MaterialForm::INGOT);
    ItemId iron_plate  = id("iron", MaterialForm::PLATE);
    ItemId iron_rod    = id("iron", MaterialForm::ROD);
    ItemId iron_block  = id("iron", MaterialForm::BLOCK);
    ItemId iron_screw  = id("iron", MaterialForm::SCREW);
    ItemId wood_plank  = id("wood", MaterialForm::PLATE);
    ItemId stick       = id("wood", MaterialForm::ROD);

    if (iron_ingot != kInvalidItemId && stick != kInvalidItemId) {
        // Hammer: 3 iron ingots + 2 sticks (3Ã—2).
        add_recipe(shaped("craft_hammer", "tools", 3, 2,
            {iron_ingot, iron_ingot, iron_ingot,
             kInvalidItemId, stick, kInvalidItemId},
            {1, 1, 1, 0, 1, 0},
            GT_HAMMER, 1));

        // Wrench: 3 iron ingots in L-shape (2Ã—2).
        add_recipe(shaped("craft_wrench", "tools", 2, 2,
            {iron_ingot, iron_ingot,
             iron_ingot, stick},
            {1, 1, 1, 1},
            GT_WRENCH, 1));

        // Screwdriver: iron ingot + stick (2Ã—1).
        add_recipe(shaped("craft_screwdriver", "tools", 1, 2,
            {iron_ingot, stick},
            {1, 1},
            GT_SCREWDRIVER, 1));

        // Crowbar: 2 iron ingots + stick (3Ã—2).
        add_recipe(shaped("craft_crowbar", "tools", 3, 2,
            {iron_ingot, kInvalidItemId, kInvalidItemId,
             iron_ingot, kInvalidItemId, stick},
            {1, 0, 0, 1, 0, 1},
            GT_CROWBAR, 1));

        // Saw: iron ingot + iron plate + stick (3Ã—2).
        if (iron_plate != kInvalidItemId) {
            add_recipe(shaped("craft_saw", "tools", 3, 2,
                {iron_plate, iron_plate, iron_ingot,
                 kInvalidItemId, kInvalidItemId, stick},
                {1, 1, 1, 0, 0, 1},
                GT_SAW, 1));
        }
    }

    if (iron_plate != kInvalidItemId && stick != kInvalidItemId) {
        // File: 2 iron plates + stick (3Ã—2).
        add_recipe(shaped("craft_file", "tools", 3, 2,
            {iron_plate, iron_plate, kInvalidItemId,
             kInvalidItemId, kInvalidItemId, stick},
            {1, 1, 0, 0, 0, 1},
            GT_FILE, 1));

        // Wire Cutter: 3 iron plates (3Ã—2).
        add_recipe(shaped("craft_wire_cutter", "tools", 3, 2,
            {iron_plate, iron_plate, iron_plate,
             kInvalidItemId, stick, kInvalidItemId},
            {1, 1, 1, 0, 1, 0},
            GT_WIRE_CUTTER, 1));
    }

    // Soft Mallet: 3 wood planks + 2 sticks (3Ã—2).
    if (wood_plank != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(shaped("craft_soft_mallet", "tools", 3, 2,
            {wood_plank, wood_plank, wood_plank,
             kInvalidItemId, stick, kInvalidItemId},
            {1, 1, 1, 0, 1, 0},
            GT_SOFT_MALLET, 1));
    }

    // Hard Hammer: iron block + 2 sticks (3Ã—2).
    if (iron_block != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(shaped("craft_hard_hammer", "tools", 3, 2,
            {iron_block, iron_block, iron_block,
             kInvalidItemId, stick, kInvalidItemId},
            {1, 1, 1, 0, 1, 0},
            GT_HARD_HAMMER, 1));
    }

    // ========================================================
    // Category: "parts" â€?basic component parts
    // ========================================================

    // Iron rod: file + iron ingot â†?2 iron rods (shapeless, requires file).
    if (iron_ingot != kInvalidItemId && iron_rod != kInvalidItemId) {
        add_recipe(shapeless("craft_iron_rod", "parts",
                             {nm(iron_ingot, 1)}, iron_rod, 2,
                             "file"));

        // Iron plate: hammer + iron ingot â†?2 iron plates.
        add_recipe(shapeless("craft_iron_plate", "parts",
                             {nm(iron_ingot, 1)}, iron_plate, 2,
                             "hammer"));

        // Iron screw: file + iron rod â†?4 iron screws.
        if (iron_screw != kInvalidItemId) {
            add_recipe(shapeless("craft_iron_screw", "parts",
                                 {nm(iron_rod, 1)}, iron_screw, 4,
                                 "file"));
        }
    }

    // ========================================================
    // Category: "wires" â€?wire cutting
    // ========================================================

    // Copper wire: wire_cutter + copper ingot â†?2 copper wire.
    const char* wire_metals[] = {"copper", "tin", "gold", "silver", "aluminum",
                                 "nickel", "lead", "zinc", "iron", "steel"};
    const int num_wire = sizeof(wire_metals) / sizeof(wire_metals[0]);
    for (int i = 0; i < num_wire; ++i) {
        const char* mat = wire_metals[i];
        ItemId ingot = id(mat, MaterialForm::INGOT);
        ItemId wire  = id(mat, MaterialForm::WIRE);
        if (ingot != kInvalidItemId && wire != kInvalidItemId) {
            add_recipe(shapeless("craft_wire", "wires",
                                 {nm(ingot, 1)}, wire, 2,
                                 "wire_cutter"));
        }
    }

    // ========================================================
    // Category: "cables" â€?insulated cables
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
            // 1 wire + 1 rubber sheet â†?1 insulated cable.
            add_recipe(shapeless("craft_cable", "cables",
                                 {nm(wire, 1),
                                  nm(rubber_sheet, 1)},
                                 wire, 1));
        }
    }

    // ========================================================
    // Category: "circuits" â€?electronic components
    // ========================================================

    ItemId copper_wire  = id("copper", MaterialForm::WIRE);
    ItemId redstone_dust = id("redstone", MaterialForm::DUST);
    ItemId glass_plate  = id("glass", MaterialForm::PLATE);
    ItemId steel_plate  = id("steel", MaterialForm::PLATE);
    ItemId gold_wire    = id("gold", MaterialForm::WIRE);
    ItemId lapis_dust   = id("lapis", MaterialForm::DUST);
    ItemId plastic_plate = id("polyethylene", MaterialForm::PLATE);  // plastic

    // Vacuum Tube: glass plate + 2 copper wire + iron rod + redstone.
    if (glass_plate != kInvalidItemId && copper_wire != kInvalidItemId &&
        iron_rod != kInvalidItemId && redstone_dust != kInvalidItemId) {
        add_recipe(shaped("craft_vacuum_tube", "circuits", 3, 3,
            {glass_plate, glass_plate, glass_plate,
             copper_wire, iron_rod, copper_wire,
             kInvalidItemId, redstone_dust, kInvalidItemId},
            {1, 1, 1, 2, 1, 2, 0, 2, 0},
            VACUUM_TUBE, 1));
    }

    // Primitive Circuit: stone plate + 2 copper wire + 2 redstone + iron rod.
    if (copper_wire != kInvalidItemId && redstone_dust != kInvalidItemId &&
        iron_rod != kInvalidItemId) {
        add_recipe(shaped("craft_primitive_circuit", "circuits", 3, 3,
            {copper_wire, redstone_dust, copper_wire,
             redstone_dust, STONE_PLATE, redstone_dust,
             copper_wire, iron_rod, copper_wire},
            {1, 1, 1, 1, 1, 1, 1, 1, 1},
            CIRCUIT_PRIMITIVE, 1));
    }

    // Basic Circuit: primitive circuit + 2 gold wire + plastic plate + 2 redstone + lapis.
    if (gold_wire != kInvalidItemId && plastic_plate != kInvalidItemId &&
        redstone_dust != kInvalidItemId && lapis_dust != kInvalidItemId) {
        add_recipe(shaped("craft_basic_circuit", "circuits", 3, 3,
            {gold_wire, redstone_dust, gold_wire,
             redstone_dust, CIRCUIT_PRIMITIVE, redstone_dust,
             gold_wire, plastic_plate, gold_wire},
            {1, 1, 1, 1, 1, 1, 1, 1, 1},
            CIRCUIT_BASIC, 1));
    }

    // Good Circuit: basic circuit + 2 lapis + 2 red alloy wire + steel plate.
    if (lapis_dust != kInvalidItemId && redstone_dust != kInvalidItemId &&
        steel_plate != kInvalidItemId && copper_wire != kInvalidItemId) {
        add_recipe(shaped("craft_good_circuit", "circuits", 3, 3,
            {copper_wire, lapis_dust, copper_wire,
             lapis_dust, CIRCUIT_BASIC, lapis_dust,
             copper_wire, steel_plate, copper_wire},
            {1, 1, 1, 1, 1, 1, 1, 1, 1},
            CIRCUIT_GOOD, 1));
    }

    // ========================================================
    // Category: "machines" â€?machine blocks & components
    // ========================================================

    // Basic Machine Hull: 8 iron plates in a ring (3Ã—3).
    if (iron_plate != kInvalidItemId) {
        add_recipe(shaped("craft_machine_hull_basic", "machines", 3, 3,
            {iron_plate, iron_plate, iron_plate,
             iron_plate, kInvalidItemId, iron_plate,
             iron_plate, iron_plate, iron_plate},
            {1, 1, 1, 1, 0, 1, 1, 1, 1},
            MACHINE_HULL_BASIC, 1));
    }

    // Advanced Machine Hull: 8 steel plates in a ring (3Ã—3).
    if (steel_plate != kInvalidItemId) {
        add_recipe(shaped("craft_machine_hull_advanced", "machines", 3, 3,
            {steel_plate, steel_plate, steel_plate,
             steel_plate, kInvalidItemId, steel_plate,
             steel_plate, steel_plate, steel_plate},
            {1, 1, 1, 1, 0, 1, 1, 1, 1},
            MACHINE_HULL_ADVANCED, 1));
    }

    // LV Electric Motor: 2 iron rod + 2 copper wire + iron (2Ã—2).
    if (iron_rod != kInvalidItemId && copper_wire != kInvalidItemId &&
        iron_ingot != kInvalidItemId) {
        add_recipe(shaped("craft_electric_motor_lv", "machines", 2, 2,
            {copper_wire, iron_rod,
             iron_rod, iron_ingot},
            {2, 1, 1, 1},
            ELECTRIC_MOTOR_LV, 1));
    }

    // LV Electric Piston: iron plate + iron rod + electric motor (shapeless).
    if (iron_plate != kInvalidItemId && iron_rod != kInvalidItemId) {
        add_recipe(shapeless("craft_electric_piston_lv", "machines",
                             {nm(iron_plate, 1),
                              nm(iron_rod, 1),
                              nm(ELECTRIC_MOTOR_LV, 1)},
                             ELECTRIC_PISTON_LV, 1));
    }

    // LV Robot Arm: piston + motor + primitive circuit + 3 iron rod (shapeless).
    if (iron_rod != kInvalidItemId) {
        add_recipe(shapeless("craft_robot_arm_lv", "machines",
                             {nm(ELECTRIC_PISTON_LV, 1),
                              nm(ELECTRIC_MOTOR_LV, 1),
                              nm(CIRCUIT_PRIMITIVE, 1),
                              nm(iron_rod, 3)},
                             ROBOT_ARM_LV, 1));
    }

    // LV Conveyor Module: rubber sheet + electric motor (shapeless).
    if (rubber_sheet != kInvalidItemId) {
        add_recipe(shapeless("craft_conveyor_lv", "machines",
                             {nm(rubber_sheet, 2),
                              nm(ELECTRIC_MOTOR_LV, 1)},
                             CONVEYOR_MODULE_LV, 1));
    }

    // LV Pump: iron ring + motor + rubber sheet (shapeless).
    if (iron_plate != kInvalidItemId && rubber_sheet != kInvalidItemId) {
        add_recipe(shapeless("craft_pump_lv", "machines",
                             {nm(iron_plate, 2),
                              nm(rubber_sheet, 1),
                              nm(ELECTRIC_MOTOR_LV, 1)},
                             PUMP_LV, 1));
    }

    // Empty Fluid Cell: 4 tin plates (2Ã—2).
    ItemId tin_plate = id("tin", MaterialForm::PLATE);
    if (tin_plate != kInvalidItemId) {
        add_recipe(shaped("craft_fluid_cell", "machines", 2, 2,
            {tin_plate, tin_plate,
             tin_plate, tin_plate},
            {1, 1, 1, 1},
            EMPTY_FLUID_CELL, 4));
    }

    // ========================================================
    // Category: "misc" â€?miscellaneous
    // ========================================================

    // Stone Plate: hammer + stone â†?stone plate.
    ItemId stone = id("stone", MaterialForm::DUST);
    if (stone != kInvalidItemId) {
        add_recipe(shapeless("craft_stone_plate", "misc",
                             {nm(stone, 1)}, STONE_PLATE, 1,
                             "hammer"));
    }

    // Wood Planks: saw + wood log â†?4 wood planks.
    ItemId wood_log = id("wood", MaterialForm::DUST);
    if (wood_log != kInvalidItemId && wood_plank != kInvalidItemId) {
        add_recipe(shapeless("craft_wood_plank", "misc",
                             {nm(wood_log, 1)}, wood_plank, 4,
                             "saw"));
    }

    // Sticks: 2 wood planks â†?4 sticks (2Ã—2 shaped).
    if (wood_plank != kInvalidItemId && stick != kInvalidItemId) {
        add_recipe(shaped("craft_stick", "misc", 2, 1,
            {wood_plank, wood_plank},
            {1, 1},
            stick, 4));
    }

    // Coal Block: 9 coal â†?1 coal block (3Ã—3 shaped).
    ItemId coal_gem = id("coal", MaterialForm::GEM);
    ItemId coal_block = id("coal", MaterialForm::BLOCK);
    if (coal_gem != kInvalidItemId && coal_block != kInvalidItemId) {
        add_recipe(shaped("craft_coal_block", "misc", 3, 3,
            {coal_gem, coal_gem, coal_gem,
             coal_gem, coal_gem, coal_gem,
             coal_gem, coal_gem, coal_gem},
            {1, 1, 1, 1, 1, 1, 1, 1, 1},
            coal_block, 1));
    }

    // Firebrick: 4 brick + 1 coal dust â†?1 firebrick (shapeless).
    ItemId brick = id("brick", MaterialForm::INGOT);
    ItemId coal_dust = id("coal", MaterialForm::DUST);
    if (brick != kInvalidItemId && coal_dust != kInvalidItemId) {
        add_recipe(shapeless("craft_firebrick", "misc",
                             {nm(brick, 4),
                              nm(coal_dust, 1)},
                             FIREBRICK, 1));
    }
}

} // namespace science_and_theology::gt
