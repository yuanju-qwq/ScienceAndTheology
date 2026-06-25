#include "elixir_registry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace science_and_theology::source_law {

static std::vector<ElixirRecipe> g_elixir_recipes;
static std::vector<std::string> g_elixir_name_storage;
static std::unordered_map<std::string, ElixirId> g_elixir_name_map;

void ElixirRegistry::initialize() {
    g_elixir_recipes.clear();
    g_elixir_name_storage.clear();
    g_elixir_name_map.clear();

    // Reserve ID 0 as invalid.
    g_elixir_recipes.push_back({});
    g_elixir_name_storage.push_back("__invalid__");

    // Built-in recipes are now registered from GDScript via
    // GDElixirRegistry (see BuiltinElixirs.gd).
}

const ElixirRecipe* ElixirRegistry::get_by_id(ElixirId id) {
    if (id == kInvalidElixirId || id >= g_elixir_recipes.size()) return nullptr;
    return &g_elixir_recipes[id];
}

const ElixirRecipe* ElixirRegistry::get_by_name(const char* name) {
    auto it = g_elixir_name_map.find(name);
    if (it == g_elixir_name_map.end()) return nullptr;
    return get_by_id(it->second);
}

std::vector<ElixirId> ElixirRegistry::find_initiation_elixirs(SublimationPath path) {
    std::vector<ElixirId> result;
    for (size_t i = 1; i < g_elixir_recipes.size(); ++i) {
        if (g_elixir_recipes[i].type == ElixirType::INITIATION &&
            g_elixir_recipes[i].target_path == path) {
            result.push_back(static_cast<ElixirId>(i));
        }
    }
    return result;
}

size_t ElixirRegistry::count() {
    return g_elixir_recipes.size() > 0 ? g_elixir_recipes.size() - 1 : 0;
}

ElixirId ElixirRegistry::register_recipe(const ElixirRecipe& recipe) {
    g_elixir_name_storage.push_back(recipe.id);
    ElixirRecipe stored = recipe;
    stored.id = g_elixir_name_storage.back().c_str();

    ElixirId id = static_cast<ElixirId>(g_elixir_recipes.size());
    g_elixir_recipes.push_back(stored);
    g_elixir_name_map[stored.id] = id;
    return id;
}

void ElixirRegistry::register_builtin_recipes() {
    // Migrated to GDScript (see scripts/content/BuiltinElixirs.gd).
    // Retained as a no-op for ABI compatibility.
}

} // namespace science_and_theology::source_law
