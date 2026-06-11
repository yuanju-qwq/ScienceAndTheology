#include "crafting.hpp"

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

} // namespace science_and_theology::gt
