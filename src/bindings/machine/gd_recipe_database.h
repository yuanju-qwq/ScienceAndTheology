#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/machine/recipe.hpp"

namespace science_and_theology {

class GDRecipeDatabase : public godot::Object {
    GDCLASS(GDRecipeDatabase, godot::Object)

public:
    GDRecipeDatabase() = default;
    ~GDRecipeDatabase() override = default;

    static void clear();
    static bool register_recipe(const godot::Dictionary& recipe);
    static int register_recipes(const godot::Array& recipes);

    static int get_total_recipe_count();
    static godot::PackedStringArray get_machine_types();
    static godot::Array get_recipes_for_machine(const godot::String& machine_type);
    static godot::Dictionary find_recipe(const godot::String& machine_type,
                                         const godot::String& recipe_name);

    static godot::Array get_load_report();
    static void clear_load_report();

protected:
    static void _bind_methods();

private:
    static godot::Dictionary _recipe_to_dict(const gt::Recipe& recipe);
    static godot::Dictionary _stack_to_dict(const gt::ResourceStack& stack);
    static godot::Dictionary _output_to_dict(const gt::RecipeOutput& output);
};

} // namespace science_and_theology
