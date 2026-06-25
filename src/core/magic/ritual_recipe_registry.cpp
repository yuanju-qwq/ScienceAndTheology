#include "ritual_recipe_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::magic {

static std::vector<RitualRecipe> g_ritual_recipes;
static std::vector<std::string> g_ritual_name_storage;
static std::vector<std::string> g_ritual_title_key_storage;
static std::unordered_map<std::string, RitualRecipeId> g_ritual_id_map;

void RitualRecipeRegistry::initialize() {
    g_ritual_recipes.clear();
    g_ritual_name_storage.clear();
    g_ritual_title_key_storage.clear();
    g_ritual_id_map.clear();

    // Reserve ID 0 as invalid.
    g_ritual_recipes.push_back({});
    g_ritual_name_storage.push_back("__invalid__");
    g_ritual_title_key_storage.push_back("__invalid__");

    // Built-in recipes are now registered from GDScript
    // (see BuiltinRitualRecipes.gd via GDRitualRecipeRegistry).
}

const RitualRecipe* RitualRecipeRegistry::get_by_id(RitualRecipeId id) {
    if (id == kInvalidRitualRecipeId || id >= g_ritual_recipes.size()) return nullptr;
    return &g_ritual_recipes[id];
}

const RitualRecipe* RitualRecipeRegistry::get_by_id_str(const char* id) {
    auto it = g_ritual_id_map.find(id);
    if (it == g_ritual_id_map.end()) return nullptr;
    return get_by_id(it->second);
}

size_t RitualRecipeRegistry::count() {
    return g_ritual_recipes.size() > 0 ? g_ritual_recipes.size() - 1 : 0;
}

RitualRecipeId RitualRecipeRegistry::register_recipe(const RitualRecipe& recipe) {
    RitualRecipeId id = static_cast<RitualRecipeId>(g_ritual_recipes.size());

    g_ritual_name_storage.push_back(recipe.id);
    g_ritual_title_key_storage.push_back(recipe.title_key);

    RitualRecipe stored = recipe;
    stored.id = g_ritual_name_storage.back().c_str();
    stored.title_key = g_ritual_title_key_storage.back().c_str();

    g_ritual_recipes.push_back(stored);
    g_ritual_id_map[recipe.id] = id;
    return id;
}

bool RitualRecipeRegistry::matches_slots(
        const RitualRecipe& recipe,
        const std::vector<RuneElement>& elements,
        const std::vector<RuneTier>& tiers) {

    size_t count = elements.size();
    if (count != tiers.size()) return false;
    if (count < recipe.pedestals.size()) return false;

    for (size_t i = 0; i < recipe.pedestals.size(); ++i) {
        const auto& slot = recipe.pedestals[i];

        if (slot.strict_element && elements[i] != slot.element) {
            return false;
        }

        if (tiers[i] < slot.min_tier) {
            return false;
        }
    }

    return true;
}

const RitualRecipe* RitualRecipeRegistry::match(
        const std::vector<RuneElement>& pedestal_elements,
        const std::vector<RuneTier>& pedestal_tiers) {

    for (size_t i = 1; i < g_ritual_recipes.size(); ++i) {
        if (matches_slots(g_ritual_recipes[i], pedestal_elements, pedestal_tiers)) {
            return &g_ritual_recipes[i];
        }
    }
    return nullptr;
}

void RitualRecipeRegistry::register_builtin_recipes() {
    // Built-in recipes are now registered from GDScript
    // (see BuiltinRitualRecipes.gd via GDRitualRecipeRegistry).
}

} // namespace science_and_theology::magic
