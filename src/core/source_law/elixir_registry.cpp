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

    register_builtin_recipes();
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
    // --- Sand Armor Initiation Elixir ---
    register_recipe(ElixirRecipe{
        "elixir_sand_armor_initiation",
        "Sand Armor Initiation Elixir",
        ElixirType::INITIATION,
        SublimationPath::SAND_ARMOR,
        OrganSlot::BONE,
        magic::RuneElement::EARTH,
        50,
        5.0f,
        2.0f,
        0,
        {magic::RuneElement::EARTH, magic::RuneElement::ORDER}
    });

    // --- Tidal Initiation Elixir (placeholder for V0.7) ---
    register_recipe(ElixirRecipe{
        "elixir_tidal_initiation",
        "Tidal Initiation Elixir",
        ElixirType::INITIATION,
        SublimationPath::TIDAL,
        OrganSlot::LUNG,
        magic::RuneElement::WATER,
        50,
        5.0f,
        2.0f,
        0,
        {magic::RuneElement::WATER, magic::RuneElement::LIGHT}
    });

    // --- Storm Initiation Elixir (placeholder for V0.7) ---
    register_recipe(ElixirRecipe{
        "elixir_storm_initiation",
        "Storm Initiation Elixir",
        ElixirType::INITIATION,
        SublimationPath::STORM,
        OrganSlot::NERVE,
        magic::RuneElement::AIR,
        50,
        5.0f,
        2.0f,
        0,
        {magic::RuneElement::AIR, magic::RuneElement::FIRE}
    });

    // --- Furnace Initiation Elixir (placeholder for V0.7) ---
    register_recipe(ElixirRecipe{
        "elixir_furnace_initiation",
        "Furnace Initiation Elixir",
        ElixirType::INITIATION,
        SublimationPath::FURNACE,
        OrganSlot::HEART,
        magic::RuneElement::FIRE,
        50,
        5.0f,
        2.0f,
        0,
        {magic::RuneElement::FIRE, magic::RuneElement::CHAOS}
    });

    // --- Radiance Initiation Elixir (placeholder for V0.7) ---
    register_recipe(ElixirRecipe{
        "elixir_radiance_initiation",
        "Radiance Initiation Elixir",
        ElixirType::INITIATION,
        SublimationPath::RADIANCE,
        OrganSlot::EYE,
        magic::RuneElement::LIGHT,
        50,
        5.0f,
        2.0f,
        0,
        {magic::RuneElement::LIGHT, magic::RuneElement::ORDER}
    });

    // --- Basic Tuning Potion ---
    register_recipe(ElixirRecipe{
        "elixir_basic_tuning",
        "Basic Tuning Potion",
        ElixirType::TUNING,
        SublimationPath::NONE,
        OrganSlot::COUNT,
        magic::RuneElement::ORDER,
        0,
        2.0f,
        -1.0f,
        1,
        {magic::RuneElement::ORDER}
    });

    // --- Basic Purification Potion ---
    register_recipe(ElixirRecipe{
        "elixir_basic_purification",
        "Basic Purification Potion",
        ElixirType::PURIFICATION,
        SublimationPath::NONE,
        OrganSlot::COUNT,
        magic::RuneElement::LIGHT,
        0,
        5.0f,
        -3.0f,
        0,
        {magic::RuneElement::LIGHT, magic::RuneElement::WATER}
    });
}

} // namespace science_and_theology::source_law
