#pragma once

#include <cstdint>
#include <vector>

#include "common/resource_key.hpp"
#include "machine/recipe.hpp"

namespace science_and_theology::gt {

// ============================================================
// Crafting recipe — manual, no power, instant
// ============================================================

// A manual crafting recipe. All recipes are shapeless — position does not
// matter. The player simply needs the required items in their inventory.
//
// required_station controls which workstation is needed:
//   ""          = hand crafting (anywhere, no station needed)
//   "workbench" = requires a placed workbench nearby
struct CraftingRecipe {
    const char* name = "";
    const char* category = "";
    const char* required_station = "";   // "" = hand, "workbench", future: "anvil"...
    std::vector<RecipeInput> inputs;     // items consumed
    ResourceStack output;                // result

    // Optional tool that must be in player inventory.
    const char* required_tool = nullptr;
    int tool_durability_cost = 0;
};

// ============================================================
// Crafting manager — recipe registry + queries
// ============================================================

class CraftingManager {
public:
    static void initialize();

    // Register a recipe.
    static void add_recipe(const CraftingRecipe& recipe);

    // Look up a recipe by name. Returns nullptr if not found.
    static const CraftingRecipe* find_recipe(const char* name);

    // Returns all recipes in a category.
    static std::vector<const CraftingRecipe*> get_by_category(const char* category);

    // Returns all recipes for a given station type.
    static std::vector<const CraftingRecipe*> get_recipes_for_station(const char* station);

    // Returns all recipes.
    static std::vector<const CraftingRecipe*> get_all_recipes();

    // Returns total recipe count.
    static size_t get_recipe_count();

    // Returns direct reference to the internal registry (for AE2 autocrafting).
    static const std::vector<CraftingRecipe>& get_registry();

private:
    static std::vector<CraftingRecipe>& registry();
};

} // namespace science_and_theology::gt
