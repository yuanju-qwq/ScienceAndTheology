#include "crafting.hpp"

#include <cstring>

namespace science_and_theology::gt {

// ============================================================
// CraftingManager — internal registry
// ============================================================

std::deque<CraftingRecipe>& CraftingManager::registry() {
    static std::deque<CraftingRecipe> recipes;
    return recipes;
}

// ============================================================
// CraftingManager — index structures
// ============================================================

std::unordered_map<std::string, const CraftingRecipe*>& CraftingManager::name_index() {
    static std::unordered_map<std::string, const CraftingRecipe*> index;
    return index;
}

std::unordered_map<std::string, std::vector<const CraftingRecipe*>>&
CraftingManager::category_index() {
    static std::unordered_map<std::string, std::vector<const CraftingRecipe*>> index;
    return index;
}

std::unordered_map<std::string, std::vector<const CraftingRecipe*>>&
CraftingManager::station_index() {
    static std::unordered_map<std::string, std::vector<const CraftingRecipe*>> index;
    return index;
}

void CraftingManager::update_indices(const CraftingRecipe& recipe) {
    // Name index — O(1) lookup by recipe name.
    if (recipe.name != nullptr && recipe.name[0] != '\0') {
        name_index()[recipe.name] = &registry().back();
    }

    // Category index — group recipes by category.
    if (recipe.category != nullptr && recipe.category[0] != '\0') {
        category_index()[recipe.category].push_back(&registry().back());
    }

    // Station index — group recipes by required station.
    // Empty string key = hand crafting (no station required).
    std::string station_key = (recipe.required_station != nullptr)
        ? recipe.required_station : "";
    station_index()[station_key].push_back(&registry().back());
}

// ============================================================
// CraftingManager — lifecycle
// ============================================================

void CraftingManager::initialize() {
    registry().clear();
    name_index().clear();
    category_index().clear();
    station_index().clear();
}

void CraftingManager::add_recipe(const CraftingRecipe& recipe) {
    registry().push_back(recipe);
    update_indices(recipe);
}

// ============================================================
// CraftingManager — queries (indexed)
// ============================================================

const CraftingRecipe* CraftingManager::find_recipe(const char* name) {
    if (name == nullptr || name[0] == '\0') return nullptr;

    auto it = name_index().find(name);
    return it != name_index().end() ? it->second : nullptr;
}

std::vector<const CraftingRecipe*> CraftingManager::get_by_category(
        const char* category) {
    if (category == nullptr || category[0] == '\0') return {};

    auto it = category_index().find(category);
    return it != category_index().end()
        ? it->second
        : std::vector<const CraftingRecipe*>();
}

std::vector<const CraftingRecipe*> CraftingManager::get_recipes_for_station(
        const char* station) {
    std::string key = (station != nullptr) ? station : "";

    auto it = station_index().find(key);
    return it != station_index().end()
        ? it->second
        : std::vector<const CraftingRecipe*>();
}

std::vector<const CraftingRecipe*> CraftingManager::get_all_recipes() {
    std::vector<const CraftingRecipe*> result;
    result.reserve(registry().size());
    for (const auto& recipe : registry()) {
        result.push_back(&recipe);
    }
    return result;
}

const std::deque<CraftingRecipe>& CraftingManager::get_registry() {
    return registry();
}

size_t CraftingManager::get_recipe_count() {
    return registry().size();
}

} // namespace science_and_theology::gt
