#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/crafting/crafting.hpp"

namespace science_and_theology {

// GDExtension wrapper for the GT crafting manager.
// All methods are static — there is one global recipe registry.
//
// Usage in GDScript:
//   var recipes = GDCraftingManager.get_all_recipes()
//   var bench_recipes = GDCraftingManager.get_recipes_for_station("workbench")
//   var recipe = GDCraftingManager.find_recipe("craft_hammer")
class GDCraftingManager : public godot::Object {
    GDCLASS(GDCraftingManager, godot::Object)

public:
    GDCraftingManager() = default;
    ~GDCraftingManager() override = default;

    // Initialize or clear the recipe registry. Content is registered from GD.
    static void initialize();
    static void clear();

    // Register content-authored recipes from GDScript.
    static bool register_recipe(const godot::Dictionary& recipe);
    static int register_recipes(const godot::Array& recipes);
    static godot::Array get_load_report();
    static void clear_load_report();

    // Find a recipe by name. Returns Dictionary with recipe info, or empty dict.
    static godot::Dictionary find_recipe(const godot::String& name);

    // Get all recipes in a category (e.g. "tools", "machines", "circuits").
    static godot::Array get_by_category(const godot::String& category);

    // Get all recipes for a given station type (e.g. "", "workbench").
    static godot::Array get_recipes_for_station(const godot::String& station);

    // Get all registered recipes.
    static godot::Array get_all_recipes();

    // Get total number of registered crafting recipes.
    static int get_recipe_count();

    // Get the display name for any item ID (material or non-material).
    static godot::String get_item_title_key(int64_t item_id);

    // Resolve a stable content key such as "ingot.copper" or "gt_hammer".
    static int64_t get_item_id_by_key(const godot::String& item_key);

    // Returns true if the item ID is a valid registered item.
    static bool is_valid_item(int64_t item_id);

protected:
    static void _bind_methods();

private:
    // Convert a C++ CraftingRecipe to a Godot Dictionary.
    static godot::Dictionary _recipe_to_dict(const gt::CraftingRecipe& recipe);

    // Convert a C++ ResourceStack to a Godot Dictionary.
    static godot::Dictionary _item_to_dict(const gt::ResourceStack& stack);
};

} // namespace science_and_theology
