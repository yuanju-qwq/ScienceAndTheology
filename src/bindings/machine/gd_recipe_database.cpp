#include "gd_recipe_database.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <deque>
#include <string>
#include <vector>

#include "core/fluid/fluid_registry.hpp"
#include "core/material/material_item.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

namespace {

std::vector<String> g_machine_recipe_load_report;
std::deque<std::string> g_machine_recipe_string_storage;

void report_machine_recipe_issue(const String& message) {
    g_machine_recipe_load_report.push_back(message);
    UtilityFunctions::push_warning("[GDRecipeDatabase] " + message);
}

const char* store_machine_recipe_string(const String& value) {
    g_machine_recipe_string_storage.push_back(std::string(value.utf8().get_data()));
    return g_machine_recipe_string_storage.back().c_str();
}

bool dict_has(const Dictionary& dict, const char* key) {
    return dict.has(StringName(key)) || dict.has(String(key));
}

Variant dict_get(const Dictionary& dict, const char* key, const Variant& fallback = Variant()) {
    if (dict.has(StringName(key))) return dict.get(StringName(key), fallback);
    return dict.get(String(key), fallback);
}

bool valid_tier(int64_t tier) {
    return tier >= 0 && tier < static_cast<int64_t>(VoltageTier::COUNT);
}

ItemId item_id_from_dict(const Dictionary& dict, const String& context) {
    if (dict_has(dict, "item_id")) {
        ItemId id = static_cast<ItemId>(static_cast<int64_t>(
            dict_get(dict, "item_id", static_cast<int64_t>(kInvalidItemId))));
        if (ItemRegistry::is_valid_item(id)) return id;
        if (!dict_has(dict, "item_key") || id != kInvalidItemId) {
            report_machine_recipe_issue(context + String(": invalid item_id ") + String::num_int64(id));
        }
    }

    if (dict_has(dict, "item_key")) {
        String key = dict_get(dict, "item_key", String(""));
        std::string key_str = key.utf8().get_data();
        ItemId id = ItemRegistry::get_item_id_by_key(key_str.c_str());
        if (id != kInvalidItemId) return id;
        report_machine_recipe_issue(context + String(": unknown item_key '") + key + String("'"));
        return kInvalidItemId;
    }

    report_machine_recipe_issue(context + String(": missing item_id or item_key"));
    return kInvalidItemId;
}

FluidId fluid_id_from_dict(const Dictionary& dict, const String& context) {
    if (dict_has(dict, "fluid_id")) {
        FluidId id = static_cast<FluidId>(static_cast<int64_t>(
            dict_get(dict, "fluid_id", static_cast<int64_t>(kInvalidFluidId))));
        if (FluidRegistry::get_fluid(id) != nullptr) return id;
        if (!dict_has(dict, "fluid_name") || id != kInvalidFluidId) {
            report_machine_recipe_issue(context + String(": invalid fluid_id ") + String::num_int64(id));
        }
    }

    if (dict_has(dict, "fluid_name")) {
        String name = dict_get(dict, "fluid_name", String(""));
        std::string name_str = name.utf8().get_data();
        FluidId id = FluidRegistry::get_fluid_id(name_str.c_str());
        if (id != kInvalidFluidId) return id;
        report_machine_recipe_issue(context + String(": unknown fluid_name '") + name + String("'"));
        return kInvalidFluidId;
    }

    report_machine_recipe_issue(context + String(": missing fluid_id or fluid_name"));
    return kInvalidFluidId;
}

bool stack_from_dict(const Dictionary& dict, const String& context, ResourceStack& out_stack) {
    String type = dict_get(dict, "type", String(""));
    if (type.is_empty()) {
        type = (dict_has(dict, "fluid_id") || dict_has(dict, "fluid_name")) ? "fluid" : "item";
    }

    int64_t amount = static_cast<int64_t>(
        dict_get(dict, "amount", dict_get(dict, "count", static_cast<int64_t>(1))));
    if (amount <= 0) {
        report_machine_recipe_issue(context + String(": amount/count must be positive"));
        return false;
    }

    if (type == "fluid") {
        FluidId fluid_id = fluid_id_from_dict(dict, context);
        if (fluid_id == kInvalidFluidId) return false;
        out_stack = ResourceStack::fluid(fluid_id, amount);
        return true;
    }

    if (type != "item") {
        report_machine_recipe_issue(context + String(": unknown resource type '") + type + String("'"));
        return false;
    }

    ItemId item_id = item_id_from_dict(dict, context);
    if (item_id == kInvalidItemId) return false;
    out_stack = ResourceStack::item(item_id, amount);
    return true;
}

bool output_from_dict(const Dictionary& dict, const String& context, RecipeOutput& out_output) {
    ResourceStack stack;
    if (!stack_from_dict(dict, context, stack)) return false;
    double probability = static_cast<double>(
        dict_get(dict, "probability", dict_get(dict, "chance", 1.0)));
    if (probability <= 0.0 || probability > 1.0) {
        report_machine_recipe_issue(context + String(": probability must be in (0, 1]"));
        return false;
    }
    out_output = RecipeOutput{stack, static_cast<float>(probability)};
    return true;
}

bool parse_machine_recipe(const Dictionary& dict, Recipe& out_recipe) {
    const String name = dict_get(dict, "name", String(""));
    const String machine_type = dict_get(dict, "machine_type", String(""));
    if (name.is_empty()) {
        report_machine_recipe_issue("machine recipe is missing name");
        return false;
    }
    if (machine_type.is_empty()) {
        report_machine_recipe_issue(String("machine recipe '") + name + String("' is missing machine_type"));
        return false;
    }

    RecipeMap* map = RecipeDatabase::find_map(machine_type.utf8().get_data());
    if (map != nullptr && map->find_by_name(name.utf8().get_data()) != nullptr) {
        report_machine_recipe_issue(String("machine recipe '") + name +
                                    String("' duplicates an existing recipe in '") +
                                    machine_type + String("'"));
        return false;
    }

    out_recipe.name = store_machine_recipe_string(name);
    out_recipe.machine_type = store_machine_recipe_string(machine_type);
    out_recipe.category = store_machine_recipe_string(dict_get(dict, "category", String("")));

    int64_t tier = static_cast<int64_t>(dict_get(dict, "min_tier", static_cast<int64_t>(0)));
    if (!valid_tier(tier)) {
        report_machine_recipe_issue(String("machine recipe '") + name +
                                    String("' has invalid min_tier ") +
                                    String::num_int64(tier));
        return false;
    }
    out_recipe.min_tier = static_cast<VoltageTier>(tier);

    out_recipe.eu_per_tick = static_cast<int64_t>(
        dict_get(dict, "eu_per_tick", static_cast<int64_t>(0)));
    out_recipe.duration_ticks = static_cast<int64_t>(
        dict_get(dict, "duration_ticks", static_cast<int64_t>(0)));
    if (out_recipe.eu_per_tick < 0) {
        report_machine_recipe_issue(String("machine recipe '") + name +
                                    String("' has negative eu_per_tick"));
        return false;
    }
    if (out_recipe.duration_ticks <= 0) {
        report_machine_recipe_issue(String("machine recipe '") + name +
                                    String("' must have positive duration_ticks"));
        return false;
    }

    Array inputs = dict_get(dict, "inputs", Array());
    if (inputs.is_empty()) {
        report_machine_recipe_issue(String("machine recipe '") + name + String("' has no inputs"));
        return false;
    }
    for (int64_t i = 0; i < inputs.size(); ++i) {
        Dictionary input = inputs[i];
        ResourceStack stack;
        if (!stack_from_dict(input,
                String("machine recipe '") + name + String("' input ") + String::num_int64(i),
                stack)) {
            return false;
        }
        out_recipe.inputs.push_back(stack);
    }

    Array outputs = dict_get(dict, "outputs", Array());
    if (outputs.is_empty()) {
        Dictionary output = dict_get(dict, "output", Dictionary());
        if (!output.is_empty()) outputs.append(output);
    }
    if (outputs.is_empty()) {
        report_machine_recipe_issue(String("machine recipe '") + name + String("' has no outputs"));
        return false;
    }
    for (int64_t i = 0; i < outputs.size(); ++i) {
        Dictionary output = outputs[i];
        RecipeOutput recipe_output;
        if (!output_from_dict(output,
                String("machine recipe '") + name + String("' output ") + String::num_int64(i),
                recipe_output)) {
            return false;
        }
        out_recipe.outputs.push_back(recipe_output);
    }

    return true;
}

} // namespace

void GDRecipeDatabase::_bind_methods() {
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("clear"),
        &GDRecipeDatabase::clear);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("register_recipe", "recipe"),
        &GDRecipeDatabase::register_recipe);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("register_recipes", "recipes"),
        &GDRecipeDatabase::register_recipes);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("get_total_recipe_count"),
        &GDRecipeDatabase::get_total_recipe_count);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("get_machine_types"),
        &GDRecipeDatabase::get_machine_types);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("get_recipes_for_machine", "machine_type"),
        &GDRecipeDatabase::get_recipes_for_machine);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("find_recipe", "machine_type", "recipe_name"),
        &GDRecipeDatabase::find_recipe);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("get_load_report"),
        &GDRecipeDatabase::get_load_report);
    ClassDB::bind_static_method("GDRecipeDatabase",
        D_METHOD("clear_load_report"),
        &GDRecipeDatabase::clear_load_report);
}

void GDRecipeDatabase::clear() {
    RecipeDatabase::initialize();
    g_machine_recipe_string_storage.clear();
    clear_load_report();
}

bool GDRecipeDatabase::register_recipe(const godot::Dictionary& recipe) {
    Recipe parsed;
    if (!parse_machine_recipe(recipe, parsed)) {
        return false;
    }
    RecipeDatabase::add_recipe(parsed);
    return true;
}

int GDRecipeDatabase::register_recipes(const godot::Array& recipes) {
    int registered = 0;
    for (int64_t i = 0; i < recipes.size(); ++i) {
        Dictionary recipe = recipes[i];
        if (register_recipe(recipe)) {
            ++registered;
        }
    }
    return registered;
}

int GDRecipeDatabase::get_total_recipe_count() {
    return static_cast<int>(RecipeDatabase::get_total_recipe_count());
}

godot::PackedStringArray GDRecipeDatabase::get_machine_types() {
    godot::PackedStringArray result;
    for (const char* type : RecipeDatabase::get_machine_types()) {
        result.append(type);
    }
    return result;
}

godot::Array GDRecipeDatabase::get_recipes_for_machine(const godot::String& machine_type) {
    godot::Array result;
    std::string type = machine_type.utf8().get_data();
    RecipeMap* map = RecipeDatabase::find_map(type.c_str());
    if (map == nullptr) return result;
    for (const auto& recipe : map->recipes()) {
        result.append(_recipe_to_dict(recipe));
    }
    return result;
}

godot::Dictionary GDRecipeDatabase::find_recipe(
        const godot::String& machine_type,
        const godot::String& recipe_name) {
    std::string type = machine_type.utf8().get_data();
    std::string name = recipe_name.utf8().get_data();
    RecipeMap* map = RecipeDatabase::find_map(type.c_str());
    if (map == nullptr) return {};
    const Recipe* recipe = map->find_by_name(name.c_str());
    return recipe != nullptr ? _recipe_to_dict(*recipe) : godot::Dictionary();
}

godot::Array GDRecipeDatabase::get_load_report() {
    godot::Array result;
    for (const auto& entry : g_machine_recipe_load_report) {
        result.append(entry);
    }
    return result;
}

void GDRecipeDatabase::clear_load_report() {
    g_machine_recipe_load_report.clear();
}

godot::Dictionary GDRecipeDatabase::_recipe_to_dict(const gt::Recipe& recipe) {
    godot::Dictionary d;
    d["name"] = recipe.name;
    d["machine_type"] = recipe.machine_type;
    d["category"] = recipe.category;
    d["min_tier"] = static_cast<int>(recipe.min_tier);
    d["eu_per_tick"] = recipe.eu_per_tick;
    d["duration_ticks"] = recipe.duration_ticks;

    godot::Array inputs;
    for (const auto& input : recipe.inputs) {
        inputs.append(_stack_to_dict(input));
    }
    d["inputs"] = inputs;

    godot::Array outputs;
    for (const auto& output : recipe.outputs) {
        outputs.append(_output_to_dict(output));
    }
    d["outputs"] = outputs;
    return d;
}

godot::Dictionary GDRecipeDatabase::_stack_to_dict(const gt::ResourceStack& stack) {
    godot::Dictionary d;
    if (!stack.is_valid()) return d;
    d["amount"] = stack.amount;
    if (stack.is_fluid()) {
        d["type"] = "fluid";
        d["fluid_id"] = static_cast<int64_t>(stack.fluid_id());
        const FluidDefinition* fluid = FluidRegistry::get_fluid(stack.fluid_id());
        if (fluid != nullptr) {
            d["fluid_name"] = fluid->name;
        }
    } else {
        d["type"] = "item";
        d["item_id"] = static_cast<int64_t>(stack.item_id());
    }
    return d;
}

godot::Dictionary GDRecipeDatabase::_output_to_dict(const gt::RecipeOutput& output) {
    godot::Dictionary d = _stack_to_dict(output.stack);
    d["probability"] = output.probability;
    return d;
}

} // namespace science_and_theology
