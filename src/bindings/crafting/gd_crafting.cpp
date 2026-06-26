#include "gd_crafting.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/common/string_pool.hpp"
#include "core/material/material_item.hpp"

#include <string>
#include <vector>

namespace science_and_theology {

using namespace godot;
using namespace gt;

namespace {

std::vector<String> g_crafting_load_report;

void report_crafting_issue(const String& message) {
    g_crafting_load_report.push_back(message);
    UtilityFunctions::push_warning("[GDCraftingManager] " + message);
}

bool dict_has(const Dictionary& dict, const char* key) {
    return dict.has(StringName(key)) || dict.has(String(key));
}

Variant dict_get(const Dictionary& dict, const char* key, const Variant& fallback = Variant()) {
    if (dict.has(StringName(key))) return dict.get(StringName(key), fallback);
    return dict.get(String(key), fallback);
}

ItemId item_id_from_dict(const Dictionary& dict, const String& context) {
    if (dict_has(dict, "item_id")) {
        ItemId id = static_cast<ItemId>(static_cast<int64_t>(
            dict_get(dict, "item_id", static_cast<int64_t>(kInvalidItemId))));
        if (ItemRegistry::is_valid_item(id)) return id;
        if (!dict_has(dict, "item_key") || id != kInvalidItemId) {
            report_crafting_issue(context + String(": invalid item_id ") + String::num_int64(id));
        }
    }

    if (dict_has(dict, "item_key")) {
        String key = dict_get(dict, "item_key", String(""));
        std::string key_str = key.utf8().get_data();
        ItemId id = ItemRegistry::get_item_id_by_key(key_str.c_str());
        if (id != kInvalidItemId) return id;
        report_crafting_issue(context + String(": unknown item_key '") + key + String("'"));
        return kInvalidItemId;
    }

    report_crafting_issue(context + String(": missing item_id or item_key"));
    return kInvalidItemId;
}

bool stack_from_dict(const Dictionary& dict, const String& context, ResourceStack& out_stack) {
    ItemId item_id = item_id_from_dict(dict, context);
    int64_t count = static_cast<int64_t>(dict_get(dict, "count", static_cast<int64_t>(1)));
    if (count <= 0) {
        report_crafting_issue(context + String(": count must be positive"));
        return false;
    }
    if (item_id == kInvalidItemId) return false;
    out_stack = ResourceStack::item(item_id, count);
    return true;
}

bool parse_crafting_recipe(const Dictionary& dict, CraftingRecipe& out_recipe) {
    const String name = dict_get(dict, "name", String(""));
    if (name.is_empty()) {
        report_crafting_issue("crafting recipe is missing name");
        return false;
    }
    if (CraftingManager::find_recipe(name.utf8().get_data()) != nullptr) {
        report_crafting_issue(String("crafting recipe '") + name + String("' duplicates an existing recipe"));
        return false;
    }

    const String category = dict_get(dict, "category", String(""));
    const String station = dict_get(dict, "required_station", String(""));
    out_recipe.name = gt::intern_string(name.utf8().get_data());
    out_recipe.category = gt::intern_string(category.utf8().get_data());
    out_recipe.required_station = gt::intern_string(station.utf8().get_data());

    Dictionary output = dict_get(dict, "output", Dictionary());
    if (output.is_empty()) {
        if (dict_has(dict, "output_item_id")) {
            output["item_id"] = dict_get(dict, "output_item_id", static_cast<int64_t>(kInvalidItemId));
        }
        if (dict_has(dict, "output_item_key")) {
            output["item_key"] = dict_get(dict, "output_item_key", String(""));
        }
        output["count"] = dict_get(dict, "output_count", static_cast<int64_t>(1));
    }
    if (!stack_from_dict(output, String("crafting recipe '") + name + String("' output"), out_recipe.output)) {
        return false;
    }

    Array inputs = dict_get(dict, "inputs", Array());
    if (inputs.is_empty()) {
        report_crafting_issue(String("crafting recipe '") + name + String("' has no inputs"));
        return false;
    }
    for (int64_t i = 0; i < inputs.size(); ++i) {
        Dictionary input = inputs[i];
        ResourceStack stack;
        if (!stack_from_dict(input,
                String("crafting recipe '") + name + String("' input ") + String::num_int64(i),
                stack)) {
            return false;
        }
        out_recipe.inputs.push_back(stack);
    }

    const String required_tool = dict_get(dict, "required_tool", String(""));
    out_recipe.required_tool = required_tool.is_empty()
        ? nullptr
        : gt::intern_string(required_tool.utf8().get_data());
    out_recipe.tool_durability_cost = static_cast<int>(
        static_cast<int64_t>(dict_get(dict, "tool_durability_cost", static_cast<int64_t>(0))));
    return true;
}

} // namespace

// ============================================================
// GDCraftingManager
// ============================================================

void GDCraftingManager::_bind_methods() {
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("initialize"),
        &GDCraftingManager::initialize);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("clear"),
        &GDCraftingManager::clear);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("register_recipe", "recipe"),
        &GDCraftingManager::register_recipe);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("register_recipes", "recipes"),
        &GDCraftingManager::register_recipes);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_load_report"),
        &GDCraftingManager::get_load_report);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("clear_load_report"),
        &GDCraftingManager::clear_load_report);
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
        D_METHOD("get_item_title_key", "item_id"),
        &GDCraftingManager::get_item_title_key);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("get_item_id_by_key", "item_key"),
        &GDCraftingManager::get_item_id_by_key);
    ClassDB::bind_static_method("GDCraftingManager",
        D_METHOD("is_valid_item", "item_id"),
        &GDCraftingManager::is_valid_item);
}

void GDCraftingManager::initialize() {
    clear();
    if (ItemRegistry::get_material_item_count() == 0) {
        ItemRegistry::initialize();
    }
}

void GDCraftingManager::clear() {
    CraftingManager::reset();
    clear_load_report();
}

bool GDCraftingManager::register_recipe(const godot::Dictionary& recipe) {
    CraftingRecipe parsed;
    if (!parse_crafting_recipe(recipe, parsed)) {
        return false;
    }
    CraftingManager::add_recipe(parsed);
    return true;
}

int GDCraftingManager::register_recipes(const godot::Array& recipes) {
    int registered = 0;
    for (int64_t i = 0; i < recipes.size(); ++i) {
        Dictionary recipe = recipes[i];
        if (register_recipe(recipe)) {
            ++registered;
        }
    }
    return registered;
}

godot::Array GDCraftingManager::get_load_report() {
    godot::Array result;
    for (const auto& entry : g_crafting_load_report) {
        result.append(entry);
    }
    return result;
}

void GDCraftingManager::clear_load_report() {
    g_crafting_load_report.clear();
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

godot::String GDCraftingManager::get_item_title_key(int64_t item_id) {
    return ItemRegistry::get_item_title_key(static_cast<ItemId>(item_id));
}

int64_t GDCraftingManager::get_item_id_by_key(const godot::String& item_key) {
    std::string key = item_key.utf8().get_data();
    return static_cast<int64_t>(ItemRegistry::get_item_id_by_key(key.c_str()));
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
    d["tool_durability_cost"] = recipe.tool_durability_cost;

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
