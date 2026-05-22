#include "gd_crafting.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/gt/material/material_item.hpp"
#include "core/gt/material/tool_items.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

// ============================================================
// GDCraftingGrid
// ============================================================

GDCraftingGrid::GDCraftingGrid() = default;
GDCraftingGrid::~GDCraftingGrid() = default;

void GDCraftingGrid::_bind_methods() {
    ClassDB::bind_method(D_METHOD("init_grid", "width", "height"),
                         &GDCraftingGrid::init_grid);
    ClassDB::bind_method(D_METHOD("get_width"),
                         &GDCraftingGrid::get_width);
    ClassDB::bind_method(D_METHOD("get_height"),
                         &GDCraftingGrid::get_height);
    ClassDB::bind_method(D_METHOD("get_slot_count"),
                         &GDCraftingGrid::get_slot_count);
    ClassDB::bind_method(D_METHOD("get_slot", "row", "col"),
                         &GDCraftingGrid::get_slot);
    ClassDB::bind_method(D_METHOD("set_slot", "row", "col", "item_id", "count"),
                         &GDCraftingGrid::set_slot);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDCraftingGrid::clear);
    ClassDB::bind_method(D_METHOD("is_empty"),
                         &GDCraftingGrid::is_empty);
    ClassDB::bind_method(D_METHOD("count_item", "item_id"),
                         &GDCraftingGrid::count_item);
}

void GDCraftingGrid::init_grid(int width, int height) {
    grid_ = CraftingGrid(width, height);
}

int GDCraftingGrid::get_width() const {
    return grid_.width();
}

int GDCraftingGrid::get_height() const {
    return grid_.height();
}

int GDCraftingGrid::get_slot_count() const {
    return grid_.slot_count();
}

godot::Dictionary GDCraftingGrid::get_slot(int row, int col) const {
    godot::Dictionary d;
    const ResourceStack& s = grid_.slot(row, col);
    d["item_id"] = static_cast<int64_t>(s.item_id());
    d["count"] = s.amount;
    return d;
}

void GDCraftingGrid::set_slot(int row, int col, int64_t item_id, int64_t count) {
    if (count > 0 && item_id != static_cast<int64_t>(kInvalidItemId)) {
        grid_.slot(row, col) = ResourceStack::item(
            static_cast<ItemId>(item_id), count);
    } else {
        grid_.slot(row, col) = ResourceStack{};
    }
}

void GDCraftingGrid::clear() {
    grid_.clear();
}

bool GDCraftingGrid::is_empty() const {
    return grid_.is_empty();
}

int64_t GDCraftingGrid::count_item(int64_t item_id) const {
    return grid_.count_item(static_cast<ItemId>(item_id));
}

// ============================================================
// GDCraftingManager
// ============================================================

void GDCraftingManager::_bind_methods() {
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("initialize"),
        &GDCraftingManager::initialize);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("find_match", "grid"),
        &GDCraftingManager::find_match);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("matches_grid", "grid", "recipe_name"),
        &GDCraftingManager::matches_grid);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("craft", "grid", "recipe_name"),
        &GDCraftingManager::craft);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_by_category", "category"),
        &GDCraftingManager::get_by_category);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_recipe_count"),
        &GDCraftingManager::get_recipe_count);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_item_display_name", "item_id"),
        &GDCraftingManager::get_item_display_name);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("is_valid_item", "item_id"),
        &GDCraftingManager::is_valid_item);
}

void GDCraftingManager::initialize() {
    CraftingManager::initialize();
    // ItemRegistry must be initialized before recipes.
    if (ItemRegistry::get_material_item_count() == 0) {
        ItemRegistry::initialize();
    }
    CraftingManager::register_basic_recipes();
}

godot::Dictionary GDCraftingManager::find_match(
        const godot::Ref<GDCraftingGrid>& grid) {
    if (grid.is_null()) return {};

    const CraftingRecipe* recipe = CraftingManager::find_match(grid->get_grid());
    if (recipe == nullptr) return {};

    return _recipe_to_dict(*recipe);
}

bool GDCraftingManager::matches_grid(
        const godot::Ref<GDCraftingGrid>& grid,
        const godot::String& recipe_name) {
    if (grid.is_null()) return false;

    const CraftingRecipe* recipe = _find_recipe(recipe_name);
    if (recipe == nullptr) return false;

    return CraftingManager::matches_grid(grid->get_grid(), *recipe);
}

godot::Dictionary GDCraftingManager::craft(
        const godot::Ref<GDCraftingGrid>& grid,
        const godot::String& recipe_name) {
    if (grid.is_null()) return {};

    const CraftingRecipe* recipe = _find_recipe(recipe_name);
    if (recipe == nullptr) return {};

    ResourceStack result = CraftingManager::craft(grid->get_grid(), *recipe);
    return _item_to_dict(result);
}

godot::Array GDCraftingManager::get_by_category(
        const godot::String& category) {
    godot::Array result;
    auto recipes = CraftingManager::get_by_category(category.utf8().get_data());
    for (const auto* recipe : recipes) {
        result.append(_recipe_to_dict(*recipe));
    }
    return result;
}

int GDCraftingManager::get_recipe_count() {
    return static_cast<int>(CraftingManager::get_recipe_count());
}

godot::String GDCraftingManager::get_item_display_name(int64_t item_id) {
    return ItemRegistry::get_item_display_name(static_cast<ItemId>(item_id));
}

bool GDCraftingManager::is_valid_item(int64_t item_id) {
    return ItemRegistry::is_valid_item(static_cast<ItemId>(item_id));
}

// ============================================================
// Private helpers
// ============================================================

godot::Dictionary GDCraftingManager::_recipe_to_dict(
        const CraftingRecipe& recipe) {
    godot::Dictionary d;
    d["name"] = recipe.name;
    d["category"] = recipe.category;
    d["grid_width"] = recipe.grid_width;
    d["grid_height"] = recipe.grid_height;
    d["is_shaped"] = recipe.is_shaped();
    d["output_item_id"] = static_cast<int64_t>(recipe.output.item_id());
    d["output_count"] = recipe.output.amount;

    if (recipe.required_tool != nullptr) {
        d["required_tool"] = recipe.required_tool;
    } else {
        d["required_tool"] = godot::String("");
    }

    // Shaped: expose pattern as flat arrays.
    if (recipe.is_shaped()) {
        godot::Array pattern_arr;
        godot::Array counts_arr;
        int size = recipe.grid_width * recipe.grid_height;
        for (int i = 0; i < size; ++i) {
            pattern_arr.append(static_cast<int64_t>(recipe.pattern[i]));
            counts_arr.append(recipe.pattern_counts[i]);
        }
        d["pattern"] = pattern_arr;
        d["pattern_counts"] = counts_arr;
    } else {
        godot::Array inputs_arr;
        for (const auto& input : recipe.shapeless_inputs) {
            godot::Dictionary in;
            in["item_id"] = static_cast<int64_t>(input.item_id());
            in["count"] = input.amount;
            inputs_arr.append(in);
        }
        d["shapeless_inputs"] = inputs_arr;
    }

    return d;
}

godot::Dictionary GDCraftingManager::_item_to_dict(const ResourceStack& stack) {
    godot::Dictionary d;
    if (stack.is_valid()) {
        d["item_id"] = static_cast<int64_t>(stack.item_id());
        d["count"] = stack.amount;
    }
    return d;
}

const CraftingRecipe* GDCraftingManager::_find_recipe(
        const godot::String& name) {
    // Scan the registry for a recipe by name.
    // This is a linear scan; consider a name→recipe map for hot paths.
    // For crafting (manual, not hot-path), linear scan is acceptable.
    std::string name_str = name.utf8().get_data();

    // Scan all categories to find the recipe.
    // We iterate through the registry via the public API.
    // Alternative: use get_by_category for each known category.
    // For simplicity, scan all recipes via the static registry.
    const char* categories[] = {
        "materials", "tools", "parts", "wires", "cables",
        "circuits", "machines", "misc"
    };
    for (const char* cat : categories) {
        auto recipes = CraftingManager::get_by_category(cat);
        for (const auto* r : recipes) {
            if (name_str == r->name) return r;
        }
    }
    return nullptr;
}

} // namespace science_and_theology
