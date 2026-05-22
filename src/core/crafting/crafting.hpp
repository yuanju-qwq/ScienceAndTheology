#pragma once

#include <cstdint>
#include <vector>

#include "common/resource_key.hpp"
#include "gt/machine/recipe.hpp"

namespace science_and_theology::gt {

// ============================================================
// Crafting recipe — manual, no power, instant
// ============================================================

// A manual crafting recipe for workbench (3×3 grid) or inventory (2×2).
// Two types:
//   Shaped: items must be in specific grid positions
//   Shapeless: items in any position, only count matters
struct CraftingRecipe {
    const char* name = "";
    const char* category = "";          // "tools", "cables", "machines", "materials"

    // Shaped recipe fields.
    int grid_width = 0;                 // 0 = shapeless; 1,2,3 = shaped
    int grid_height = 0;
    ItemId pattern[9] = {};             // flattened 3×3 grid (row-major)
    int64_t pattern_counts[9] = {};     // count per pattern slot

    // Shapeless recipe fields (used when grid_width == 0).
    std::vector<RecipeInput> shapeless_inputs;

    // Output.
    ResourceStack output;

    // Optional tool that must be in player inventory/hotbar.
    const char* required_tool = nullptr; // e.g. "hammer", "wire_cutter", "file"
    int tool_durability_cost = 0;        // durability consumed per craft

    bool is_shaped() const { return grid_width > 0; }
    bool is_shapeless() const { return grid_width == 0; }
};

// ============================================================
// Crafting grid — 3×3 workbench or 2×2 inventory grid
// ============================================================

class CraftingGrid {
public:
    static constexpr int kMaxSize = 3;
    static constexpr int kMaxSlots = kMaxSize * kMaxSize; // 9

    CraftingGrid() = default;
    explicit CraftingGrid(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    int slot_count() const { return width_ * height_; }

    // Access a specific slot (row-major: row * width + col).
    ResourceStack& slot(int row, int col);
    const ResourceStack& slot(int row, int col) const;
    ResourceStack& slot_at(int index);
    const ResourceStack& slot_at(int index) const;

    // Set all slots to empty.
    void clear();

    // Returns true if all slots are empty.
    bool is_empty() const;

    // Returns the count of a specific item across all slots.
    int64_t count_item(ItemId item_id) const;

    // Returns true if the grid contains at least the given items
    // (for shapeless recipe validation).
    bool contains_items(const std::vector<RecipeInput>& inputs) const;

    // Consume items from the grid. Assumes validation already passed.
    void consume_items(const std::vector<RecipeInput>& inputs);

    // Consume shaped pattern from grid. Assumes validation already passed.
    void consume_shaped(int pattern_width, int pattern_height,
                        const ItemId* pattern, const int64_t* counts,
                        int grid_offset_row, int grid_offset_col);

private:
    int width_ = kMaxSize;
    int height_ = kMaxSize;
    ResourceStack slots_[kMaxSlots];
};

// ============================================================
// Crafting manager — recipe registry + matching
// ============================================================

class CraftingManager {
public:
    static void initialize();

    // Register a recipe.
    static void add_recipe(const CraftingRecipe& recipe);

    // Find a recipe matching the given grid contents.
    // For shaped recipes, scans all possible offsets.
    // Returns nullptr if no match.
    static const CraftingRecipe* find_match(const CraftingGrid& grid);

    // Check if a specific recipe matches the grid (with tool check).
    static bool matches_grid(const CraftingGrid& grid,
                             const CraftingRecipe& recipe);

    // Execute a craft: validate, consume inputs, return output.
    // Returns empty ResourceStack if craft fails.
    static ResourceStack craft(CraftingGrid& grid,
                                const CraftingRecipe& recipe);

    // Returns all recipes in a category.
    static std::vector<const CraftingRecipe*> get_by_category(const char* category);

    // Returns total recipe count.
    static size_t get_recipe_count();

    // Pre-register basic recipes (workbench, tools, cables, etc.).
    static void register_basic_recipes();

private:
    // Try to match a shaped recipe in the grid at a specific offset.
    static bool match_shaped_at(const CraftingGrid& grid,
                                 const CraftingRecipe& recipe,
                                 int offset_row, int offset_col);

    static std::vector<CraftingRecipe>& registry();
};

} // namespace science_and_theology::gt