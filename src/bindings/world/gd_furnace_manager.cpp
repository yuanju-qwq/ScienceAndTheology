#include "gd_furnace_manager.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "core/fuel/fuel_registry.hpp"
#include "core/machine/recipe.hpp"

namespace science_and_theology {

using namespace godot;

namespace {

constexpr int32_t kMaxStackSize = 64;
constexpr double kFuelTicksPerSecond = 20.0;

double clamp01(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

} // namespace

bool GDFurnaceData::is_burning() const {
    return fuel_burn_remaining_ > 0.0;
}

double GDFurnaceData::get_progress_ratio() const {
    if (smelt_target_ <= 0.0) return 0.0;
    return clamp01(smelt_progress_ / smelt_target_);
}

double GDFurnaceData::get_fuel_ratio() const {
    if (fuel_burn_max_ <= 0.0) return 0.0;
    return clamp01(fuel_burn_remaining_ / fuel_burn_max_);
}

Dictionary GDFurnaceData::to_dictionary() const {
    Dictionary d;
    d["input_item_id"] = input_item_id_;
    d["input_count"] = input_count_;
    d["fuel_item_id"] = fuel_item_id_;
    d["fuel_burn_remaining"] = fuel_burn_remaining_;
    d["fuel_burn_max"] = fuel_burn_max_;
    d["output_item_id"] = output_item_id_;
    d["output_count"] = output_count_;
    d["smelt_progress"] = smelt_progress_;
    d["smelt_target"] = smelt_target_;
    d["progress_ratio"] = get_progress_ratio();
    d["fuel_ratio"] = get_fuel_ratio();
    return d;
}

void GDFurnaceData::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_burning"), &GDFurnaceData::is_burning);
    ClassDB::bind_method(D_METHOD("get_progress_ratio"), &GDFurnaceData::get_progress_ratio);
    ClassDB::bind_method(D_METHOD("get_fuel_ratio"), &GDFurnaceData::get_fuel_ratio);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &GDFurnaceData::to_dictionary);

    ClassDB::bind_method(D_METHOD("get_input_item_id"), &GDFurnaceData::get_input_item_id);
    ClassDB::bind_method(D_METHOD("set_input_item_id", "value"), &GDFurnaceData::set_input_item_id);
    ClassDB::bind_method(D_METHOD("get_input_count"), &GDFurnaceData::get_input_count);
    ClassDB::bind_method(D_METHOD("set_input_count", "value"), &GDFurnaceData::set_input_count);
    ClassDB::bind_method(D_METHOD("get_fuel_item_id"), &GDFurnaceData::get_fuel_item_id);
    ClassDB::bind_method(D_METHOD("set_fuel_item_id", "value"), &GDFurnaceData::set_fuel_item_id);
    ClassDB::bind_method(D_METHOD("get_fuel_burn_remaining"), &GDFurnaceData::get_fuel_burn_remaining);
    ClassDB::bind_method(D_METHOD("set_fuel_burn_remaining", "value"), &GDFurnaceData::set_fuel_burn_remaining);
    ClassDB::bind_method(D_METHOD("get_fuel_burn_max"), &GDFurnaceData::get_fuel_burn_max);
    ClassDB::bind_method(D_METHOD("set_fuel_burn_max", "value"), &GDFurnaceData::set_fuel_burn_max);
    ClassDB::bind_method(D_METHOD("get_output_item_id"), &GDFurnaceData::get_output_item_id);
    ClassDB::bind_method(D_METHOD("set_output_item_id", "value"), &GDFurnaceData::set_output_item_id);
    ClassDB::bind_method(D_METHOD("get_output_count"), &GDFurnaceData::get_output_count);
    ClassDB::bind_method(D_METHOD("set_output_count", "value"), &GDFurnaceData::set_output_count);
    ClassDB::bind_method(D_METHOD("get_smelt_progress"), &GDFurnaceData::get_smelt_progress);
    ClassDB::bind_method(D_METHOD("set_smelt_progress", "value"), &GDFurnaceData::set_smelt_progress);
    ClassDB::bind_method(D_METHOD("get_smelt_target"), &GDFurnaceData::get_smelt_target);
    ClassDB::bind_method(D_METHOD("set_smelt_target", "value"), &GDFurnaceData::set_smelt_target);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "input_item_id"), "set_input_item_id", "get_input_item_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "input_count"), "set_input_count", "get_input_count");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "fuel_item_id"), "set_fuel_item_id", "get_fuel_item_id");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fuel_burn_remaining"), "set_fuel_burn_remaining", "get_fuel_burn_remaining");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fuel_burn_max"), "set_fuel_burn_max", "get_fuel_burn_max");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "output_item_id"), "set_output_item_id", "get_output_item_id");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "output_count"), "set_output_count", "get_output_count");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smelt_progress"), "set_smelt_progress", "get_smelt_progress");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smelt_target"), "set_smelt_target", "get_smelt_target");
}

void GDFurnaceManager::_ready() {
    ensure_recipes();
}

void GDFurnaceManager::_process(double delta) {
    tick_all(delta);
}

bool GDFurnaceManager::place_furnace(const StringName& dimension, const Vector3i& cell) {
    ensure_recipes();
    const FurnaceKey key = make_key(dimension, cell);
    if (furnaces_.find(key) != furnaces_.end()) {
        return false;
    }

    Ref<GDFurnaceData> data;
    data.instantiate();
    furnaces_[key] = data;
    mark_dirty(key, "placed", true);
    emit_signal("furnace_placed", dimension, cell);
    return true;
}

bool GDFurnaceManager::remove_furnace(const StringName& dimension, const Vector3i& cell) {
    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    if (it == furnaces_.end()) {
        return false;
    }

    furnaces_.erase(it);
    mark_dirty(key, "removed", true);
    emit_signal("furnace_removed", dimension, cell);
    return true;
}

Ref<GDFurnaceData> GDFurnaceManager::get_furnace(
        const StringName& dimension, const Vector3i& cell) const {
    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    return it != furnaces_.end() ? it->second : Ref<GDFurnaceData>();
}

bool GDFurnaceManager::has_furnace(const StringName& dimension, const Vector3i& cell) const {
    return furnaces_.find(make_key(dimension, cell)) != furnaces_.end();
}

Array GDFurnaceManager::get_all_furnaces() const {
    Array result;
    for (const auto& pair : furnaces_) {
        result.append(key_to_dictionary(pair.first));
    }
    return result;
}

bool GDFurnaceManager::insert_input(
        const StringName& dimension, const Vector3i& cell, int64_t item_id, int32_t count) {
    ensure_recipes();
    if (item_id <= 0 || count <= 0) return false;
    if (recipes_.find(item_id) == recipes_.end()) return false;

    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    if (it == furnaces_.end() || it->second.is_null()) return false;

    GDFurnaceData* data = it->second.ptr();
    if (data->get_input_item_id() != 0 && data->get_input_item_id() != item_id) {
        return false;
    }
    if (data->get_input_count() + count > kMaxStackSize) {
        return false;
    }

    data->set_input_item_id(item_id);
    data->set_input_count(data->get_input_count() + count);
    const auto recipe = recipes_.find(item_id);
    if (recipe != recipes_.end()) {
        data->set_smelt_target(recipe->second.time);
    }
    mark_dirty(key, "input inserted", true);
    return true;
}

bool GDFurnaceManager::insert_fuel(
        const StringName& dimension, const Vector3i& cell, int64_t item_id) {
    if (item_id <= 0 || gt::FuelRegistry::get_item_burn_ticks(
            static_cast<gt::ItemId>(item_id)) <= 0) {
        return false;
    }

    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    if (it == furnaces_.end() || it->second.is_null()) return false;

    GDFurnaceData* data = it->second.ptr();
    if (data->get_fuel_item_id() != 0 && data->get_fuel_item_id() != item_id) {
        return false;
    }

    data->set_fuel_item_id(item_id);
    mark_dirty(key, "fuel inserted", true);
    return true;
}

bool GDFurnaceManager::take_output(
        const StringName& dimension, const Vector3i& cell, int32_t count) {
    if (count <= 0) return false;

    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    if (it == furnaces_.end() || it->second.is_null()) return false;

    GDFurnaceData* data = it->second.ptr();
    if (data->get_output_item_id() <= 0 || data->get_output_count() < count) {
        return false;
    }

    const int32_t remaining = data->get_output_count() - count;
    data->set_output_count(remaining);
    if (remaining <= 0) {
        data->set_output_item_id(0);
        data->set_output_count(0);
    }
    mark_dirty(key, "output taken", true);
    return true;
}

double GDFurnaceManager::get_fuel_burn_time(int64_t item_id) const {
    const int64_t ticks = gt::FuelRegistry::get_item_burn_ticks(
        static_cast<gt::ItemId>(item_id));
    return ticks > 0 ? static_cast<double>(ticks) / kFuelTicksPerSecond : 0.0;
}

Dictionary GDFurnaceManager::get_recipe_for(int64_t item_id) const {
    const_cast<GDFurnaceManager*>(this)->ensure_recipes();
    const auto it = recipes_.find(item_id);
    if (it == recipes_.end()) {
        return {};
    }

    Dictionary recipe;
    recipe["name"] = it->second.name.c_str();
    recipe["output_id"] = it->second.output_id;
    recipe["output_count"] = it->second.output_count;
    recipe["time"] = it->second.time;
    return recipe;
}

Dictionary GDFurnaceManager::get_furnace_snapshot(
        const StringName& dimension, const Vector3i& cell) const {
    const FurnaceKey key = make_key(dimension, cell);
    const auto it = furnaces_.find(key);
    if (it == furnaces_.end() || it->second.is_null()) {
        return {};
    }

    Dictionary snapshot = it->second->to_dictionary();
    snapshot["dimension"] = dimension;
    snapshot["cell"] = cell;
    return snapshot;
}

void GDFurnaceManager::tick_all(double delta) {
    ensure_recipes();
    if (delta <= 0.0 || furnaces_.empty()) return;

    for (auto& pair : furnaces_) {
        if (pair.second.is_null()) continue;
        tick_furnace(pair.first, pair.second.ptr(), delta);
    }
}

Array GDFurnaceManager::get_dirty_furnaces() const {
    Array result;
    for (const auto& key : dirty_furnaces_) {
        result.append(key_to_dictionary(key));
    }
    return result;
}

void GDFurnaceManager::clear_dirty_furnaces() {
    dirty_furnaces_.clear();
}

void GDFurnaceManager::clear() {
    furnaces_.clear();
    dirty_furnaces_.clear();
}

size_t GDFurnaceManager::FurnaceKeyHash::operator()(const FurnaceKey& key) const {
    size_t h = std::hash<std::string>()(key.dimension);
    h ^= std::hash<int32_t>()(key.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(key.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int32_t>()(key.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

GDFurnaceManager::FurnaceKey GDFurnaceManager::make_key(
        const StringName& dimension, const Vector3i& cell) {
    return FurnaceKey{String(dimension).utf8().get_data(), cell.x, cell.y, cell.z};
}

String GDFurnaceManager::key_to_log_text(const FurnaceKey& key) {
    return String(key.dimension.c_str()) + String("@") +
        String::num_int64(key.x) + String(",") +
        String::num_int64(key.y) + String(",") +
        String::num_int64(key.z);
}

Dictionary GDFurnaceManager::key_to_dictionary(const FurnaceKey& key) {
    Dictionary d;
    d["dimension"] = String(key.dimension.c_str());
    d["cell"] = Vector3i(key.x, key.y, key.z);
    return d;
}

void GDFurnaceManager::ensure_recipes() {
    if (recipes_initialized_) return;

    const gt::RecipeMap* map = gt::RecipeDatabase::find_map("furnace");
    if (map == nullptr || map->recipe_count() == 0) {
        if (!missing_recipes_warned_) {
            UtilityFunctions::push_warning(
                "GDFurnaceManager: no furnace recipes registered in "
                "GDRecipeDatabase machine_type='furnace'");
            missing_recipes_warned_ = true;
        }
        return;
    }

    recipes_.clear();
    int loaded = 0;
    int skipped = 0;
    String loaded_names;
    for (const gt::Recipe& recipe : map->recipes()) {
        const String recipe_name(recipe.name);
        if (recipe.inputs.size() != 1 || recipe.outputs.size() != 1) {
            UtilityFunctions::push_warning(
                "GDFurnaceManager: skipped unsupported furnace recipe '",
                recipe_name, "'; furnace recipes must have exactly one input and one output");
            ++skipped;
            continue;
        }

        const gt::ResourceStack& input = recipe.inputs[0];
        const gt::RecipeOutput& output = recipe.outputs[0];
        if (!input.is_item() || !output.stack.is_item() || !output.is_guaranteed()) {
            UtilityFunctions::push_warning(
                "GDFurnaceManager: skipped unsupported furnace recipe '",
                recipe_name, "'; furnace only supports guaranteed item -> item recipes");
            ++skipped;
            continue;
        }
        if (recipe.duration_ticks <= 0 || output.stack.amount <= 0 ||
                output.stack.amount > kMaxStackSize) {
            UtilityFunctions::push_warning(
                "GDFurnaceManager: skipped invalid furnace recipe '", recipe_name, "'");
            ++skipped;
            continue;
        }

        const int64_t input_id = static_cast<int64_t>(input.item_id());
        if (recipes_.find(input_id) != recipes_.end()) {
            UtilityFunctions::push_warning(
                "GDFurnaceManager: skipped duplicate furnace input in recipe '",
                recipe_name, "'");
            ++skipped;
            continue;
        }

        recipes_[input_id] = FurnaceRecipe{
            std::string(recipe.name),
            static_cast<int64_t>(output.stack.item_id()),
            static_cast<int32_t>(output.stack.amount),
            static_cast<double>(recipe.duration_ticks) / kFuelTicksPerSecond,
        };
        if (!loaded_names.is_empty()) {
            loaded_names += ", ";
        }
        loaded_names += recipe_name;
        ++loaded;
    }

    recipes_initialized_ = true;
    missing_recipes_warned_ = false;
    UtilityFunctions::print(
        "GDFurnaceManager: loaded ", loaded,
        " furnace recipes from GDRecipeDatabase machine_type='furnace'",
        skipped > 0 ? String(" skipped=") + String::num_int64(skipped) : String(""),
        loaded_names.is_empty() ? String("") : String(" recipes=[") + loaded_names + String("]"));
}

bool GDFurnaceManager::tick_furnace(
        const FurnaceKey& key, GDFurnaceData* data, double delta) {
    if (data == nullptr || data->get_input_item_id() == 0 ||
            data->get_input_count() <= 0) {
        return false;
    }

    const auto recipe_it = recipes_.find(data->get_input_item_id());
    if (recipe_it == recipes_.end()) return false;
    const FurnaceRecipe& recipe = recipe_it->second;

    if (data->get_output_item_id() != 0 &&
            data->get_output_item_id() != recipe.output_id) {
        return false;
    }
    if (data->get_output_item_id() == recipe.output_id &&
            data->get_output_count() >= kMaxStackSize) {
        return false;
    }
    if (data->get_fuel_burn_remaining() <= 0.0 && !try_consume_fuel(key, data)) {
        return false;
    }

    data->set_fuel_burn_remaining(
        std::max(0.0, data->get_fuel_burn_remaining() - delta));
    data->set_smelt_target(recipe.time);
    data->set_smelt_progress(data->get_smelt_progress() + delta);

    if (data->get_smelt_progress() < data->get_smelt_target()) {
        mark_dirty(key, "progress", false);
        return true;
    }

    data->set_smelt_progress(0.0);
    data->set_input_count(data->get_input_count() - 1);
    if (data->get_input_count() <= 0) {
        data->set_input_item_id(0);
        data->set_input_count(0);
    }

    if (data->get_output_item_id() == 0) {
        data->set_output_item_id(recipe.output_id);
        data->set_output_count(recipe.output_count);
    } else {
        data->set_output_count(
            std::min(kMaxStackSize, data->get_output_count() + recipe.output_count));
    }

    mark_dirty(key, "recipe completed", true);
    return true;
}

bool GDFurnaceManager::try_consume_fuel(const FurnaceKey& key, GDFurnaceData* data) {
    if (data == nullptr || data->get_fuel_item_id() <= 0) {
        return false;
    }

    const int64_t burn_ticks = gt::FuelRegistry::get_item_burn_ticks(
        static_cast<gt::ItemId>(data->get_fuel_item_id()));
    if (burn_ticks <= 0) {
        return false;
    }

    const double burn_time = static_cast<double>(burn_ticks) / kFuelTicksPerSecond;
    data->set_fuel_burn_remaining(burn_time);
    data->set_fuel_burn_max(burn_time);
    data->set_fuel_item_id(0);
    mark_dirty(key, "fuel consumed", true);
    return true;
}

void GDFurnaceManager::mark_dirty(
        const FurnaceKey& key, const char* reason, bool log_change) {
    (void)reason;
    (void)log_change;
    dirty_furnaces_.insert(key);
}

void GDFurnaceManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_furnace", "dimension", "cell"),
                         &GDFurnaceManager::place_furnace);
    ClassDB::bind_method(D_METHOD("remove_furnace", "dimension", "cell"),
                         &GDFurnaceManager::remove_furnace);
    ClassDB::bind_method(D_METHOD("get_furnace", "dimension", "cell"),
                         &GDFurnaceManager::get_furnace);
    ClassDB::bind_method(D_METHOD("has_furnace", "dimension", "cell"),
                         &GDFurnaceManager::has_furnace);
    ClassDB::bind_method(D_METHOD("get_all_furnaces"),
                         &GDFurnaceManager::get_all_furnaces);
    ClassDB::bind_method(D_METHOD("insert_input", "dimension", "cell", "item_id", "count"),
                         &GDFurnaceManager::insert_input, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("insert_fuel", "dimension", "cell", "item_id"),
                         &GDFurnaceManager::insert_fuel);
    ClassDB::bind_method(D_METHOD("take_output", "dimension", "cell", "count"),
                         &GDFurnaceManager::take_output);
    ClassDB::bind_method(D_METHOD("get_fuel_burn_time", "item_id"),
                         &GDFurnaceManager::get_fuel_burn_time);
    ClassDB::bind_method(D_METHOD("get_recipe_for", "item_id"),
                         &GDFurnaceManager::get_recipe_for);
    ClassDB::bind_method(D_METHOD("get_furnace_snapshot", "dimension", "cell"),
                         &GDFurnaceManager::get_furnace_snapshot);
    ClassDB::bind_method(D_METHOD("tick_all", "delta"),
                         &GDFurnaceManager::tick_all);
    ClassDB::bind_method(D_METHOD("get_dirty_furnaces"),
                         &GDFurnaceManager::get_dirty_furnaces);
    ClassDB::bind_method(D_METHOD("clear_dirty_furnaces"),
                         &GDFurnaceManager::clear_dirty_furnaces);
    ClassDB::bind_method(D_METHOD("clear"),
                         &GDFurnaceManager::clear);

    ADD_SIGNAL(MethodInfo("furnace_placed",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
    ADD_SIGNAL(MethodInfo("furnace_removed",
        PropertyInfo(Variant::STRING_NAME, "dimension"),
        PropertyInfo(Variant::VECTOR3I, "cell")));
}

} // namespace science_and_theology
