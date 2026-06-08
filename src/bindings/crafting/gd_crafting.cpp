#include "gd_crafting.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/material/material_item.hpp"
#include "core/material/tool_items.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

// ============================================================
// GDCraftingManager
// ============================================================

void GDCraftingManager::_bind_methods() {
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("initialize"),
        &GDCraftingManager::initialize);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("find_recipe", "name"),
        &GDCraftingManager::find_recipe);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_by_category", "category"),
        &GDCraftingManager::get_by_category);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_recipes_for_station", "station"),
        &GDCraftingManager::get_recipes_for_station);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_all_recipes"),
        &GDCraftingManager::get_all_recipes);
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
    if (ItemRegistry::get_material_item_count() == 0) {
        ItemRegistry::initialize();
    }
    CraftingManager::register_basic_recipes();
}

godot::Dictionary GDCraftingManager::find_recipe(const godot::String& name) {
    std::string name_str = name.utf8().get_data();
    const CraftingRecipe* recipe = CraftingManager::find_recipe(name_str.c_str());
    if (recipe == nullptr) return {};
    return _recipe_to_dict(*recipe);
}

godot::Array GDCraftingManager::get_by_category(const godot::String& category) {
    godot::Array result;
    auto recipes = CraftingManager::get_by_category(category.utf8().get_data());
    for (const auto* recipe : recipes) {
        result.append(_recipe_to_dict(*recipe));
    }
    return result;
}

godot::Array GDCraftingManager::get_recipes_for_station(const godot::String& station) {
    godot::Array result;
    auto recipes = CraftingManager::get_recipes_for_station(station.utf8().get_data());
    for (const auto* recipe : recipes) {
        result.append(_recipe_to_dict(*recipe));
    }
    return result;
}

godot::Array GDCraftingManager::get_all_recipes() {
    godot::Array result;
    auto recipes = CraftingManager::get_all_recipes();
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
    d["required_station"] = recipe.required_station;
    d["output_item_id"] = static_cast<int64_t>(recipe.output.item_id());
    d["output_count"] = recipe.output.amount;

    if (recipe.required_tool != nullptr) {
        d["required_tool"] = recipe.required_tool;
    } else {
        d["required_tool"] = godot::String("");
    }

    godot::Array inputs_arr;
    for (const auto& input : recipe.inputs) {
        godot::Dictionary in;
        in["item_id"] = static_cast<int64_t>(input.item_id());
        in["count"] = input.amount;
        inputs_arr.append(in);
    }
    d["inputs"] = inputs_arr;

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

} // namespace science_and_theology
