#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/gt/crafting/crafting.hpp"

namespace science_and_theology {

// GDExtension wrapper for the GT crafting grid (2x2 or 3x3).
//
// Usage in GDScript:
//   var grid = GDCraftingGrid.new(3, 3)
//   grid.set_slot(0, 0, iron_ingot_id, 1)
//   grid.set_slot(0, 1, iron_ingot_id, 1)
//   grid.set_slot(1, 0, iron_ingot_id, 1)
//   grid.set_slot(1, 1, stick_id, 1)
//   var match = GDCraftingManager.find_match(grid)
class GDCraftingGrid : public godot::Resource {
    GDCLASS(GDCraftingGrid, godot::Resource)

public:
    GDCraftingGrid();
    ~GDCraftingGrid() override;

    // Initialize grid size (2x2 or 3x3). Must be called before use.
    void init_grid(int width, int height);

    int get_width() const;
    int get_height() const;
    int get_slot_count() const;

    // Get a slot's contents: {"item_id": int, "count": int}
    godot::Dictionary get_slot(int row, int col) const;

    // Set a slot's contents.
    void set_slot(int row, int col, int64_t item_id, int64_t count);

    // Remove all items from the grid.
    void clear();

    // Returns true if all slots are empty.
    bool is_empty() const;

    // Count how many of a given item are in the grid.
    int64_t count_item(int64_t item_id) const;

    // Internal accessor for GDCraftingManager.
    gt::CraftingGrid& get_grid() { return grid_; }
    const gt::CraftingGrid& get_grid() const { return grid_; }

protected:
    static void _bind_methods();

private:
    gt::CraftingGrid grid_{3, 3};
};

// GDExtension wrapper for the GT crafting manager.
// All methods are static — there is one global recipe registry.
//
// Usage in GDScript:
//   var match = GDCraftingManager.find_match(grid)
//   if match:
//       var result = GDCraftingManager.craft(grid, match["name"])
//       if result:
//           print("Crafted item ", result["item_id"])
class GDCraftingManager : public godot::Object {
    GDCLASS(GDCraftingManager, godot::Object)

public:
    GDCraftingManager() = default;
    ~GDCraftingManager() override = default;

    // Initialize the recipe registry and register basic recipes.
    // Must be called once at game startup after ItemRegistry.initialize().
    static void initialize();

    // Find a recipe matching the given crafting grid.
    // Returns Dictionary with recipe info, or empty dict if no match.
    static godot::Dictionary find_match(const godot::Ref<GDCraftingGrid>& grid);

    // Check if a named recipe matches the grid.
    static bool matches_grid(const godot::Ref<GDCraftingGrid>& grid,
                             const godot::String& recipe_name);

    // Execute a craft: validate, consume inputs, return output.
    // Returns {"item_id": int, "count": int} or empty dict on failure.
    static godot::Dictionary craft(const godot::Ref<GDCraftingGrid>& grid,
                                    const godot::String& recipe_name);

    // Get all recipes in a category (e.g. "tools", "machines", "circuits").
    static godot::Array get_by_category(const godot::String& category);

    // Get total number of registered crafting recipes.
    static int get_recipe_count();

    // Get the display name for any item ID (material or non-material).
    static godot::String get_item_display_name(int64_t item_id);

    // Returns true if the item ID is a valid registered item.
    static bool is_valid_item(int64_t item_id);

protected:
    static void _bind_methods();

private:
    // Convert a C++ CraftingRecipe to a Godot Dictionary.
    static godot::Dictionary _recipe_to_dict(const gt::CraftingRecipe& recipe);

    // Convert a C++ ItemStack to a Godot Dictionary.
    static godot::Dictionary _item_to_dict(const gt::ItemStack& stack);

    // Look up internal recipe by name.
    static const gt::CraftingRecipe* _find_recipe(const godot::String& name);
};

} // namespace science_and_theology
