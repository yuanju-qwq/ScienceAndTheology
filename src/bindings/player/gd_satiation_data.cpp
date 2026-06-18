#include "player/gd_satiation_data.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

using namespace godot;

namespace science_and_theology {

void GDSatiationData::_bind_methods() {
    // Satiation value
    ClassDB::bind_method(D_METHOD("get_satiation_current"),
                         &GDSatiationData::get_satiation_current);
    ClassDB::bind_method(D_METHOD("get_satiation_max"),
                         &GDSatiationData::get_satiation_max);
    ClassDB::bind_method(D_METHOD("get_decay_rate"),
                         &GDSatiationData::get_decay_rate);
    ClassDB::bind_method(D_METHOD("set_satiation_max", "max_val"),
                         &GDSatiationData::set_satiation_max);
    ClassDB::bind_method(D_METHOD("set_decay_rate", "rate"),
                         &GDSatiationData::set_decay_rate);
    ClassDB::bind_method(D_METHOD("eat", "food_value"),
                         &GDSatiationData::eat);
    ClassDB::bind_method(D_METHOD("eat_food", "food_dict"),
                         &GDSatiationData::eat_food);
    ClassDB::bind_method(D_METHOD("set_satiation", "value"),
                         &GDSatiationData::set_satiation);

    // Hunger level
    ClassDB::bind_method(D_METHOD("get_hunger_level"),
                         &GDSatiationData::get_hunger_level);
    ClassDB::bind_method(D_METHOD("get_is_starving"),
                         &GDSatiationData::get_is_starving);

    // Effect modifiers
    ClassDB::bind_method(D_METHOD("get_speed_modifier"),
                         &GDSatiationData::get_speed_modifier);
    ClassDB::bind_method(D_METHOD("get_health_regen_modifier"),
                         &GDSatiationData::get_health_regen_modifier);
    ClassDB::bind_method(D_METHOD("get_attack_modifier"),
                         &GDSatiationData::get_attack_modifier);
    ClassDB::bind_method(D_METHOD("get_starvation_damage_per_tick"),
                         &GDSatiationData::get_starvation_damage_per_tick);

    // Thresholds
    ClassDB::bind_method(D_METHOD("get_peckish_threshold"),
                         &GDSatiationData::get_peckish_threshold);
    ClassDB::bind_method(D_METHOD("get_hungry_threshold"),
                         &GDSatiationData::get_hungry_threshold);
    ClassDB::bind_method(D_METHOD("get_starving_threshold"),
                         &GDSatiationData::get_starving_threshold);
    ClassDB::bind_method(D_METHOD("set_peckish_threshold", "val"),
                         &GDSatiationData::set_peckish_threshold);
    ClassDB::bind_method(D_METHOD("set_hungry_threshold", "val"),
                         &GDSatiationData::set_hungry_threshold);
    ClassDB::bind_method(D_METHOD("set_starving_threshold", "val"),
                         &GDSatiationData::set_starving_threshold);

    // Source essence pool
    ClassDB::bind_method(D_METHOD("get_source_essence", "element"),
                         &GDSatiationData::get_source_essence);
    ClassDB::bind_method(D_METHOD("get_total_source_essence"),
                         &GDSatiationData::get_total_source_essence);
    ClassDB::bind_method(D_METHOD("get_source_essence_cap"),
                         &GDSatiationData::get_source_essence_cap);
    ClassDB::bind_method(D_METHOD("set_source_essence_cap", "max_val"),
                         &GDSatiationData::set_source_essence_cap);
    ClassDB::bind_method(D_METHOD("get_source_essence_decay_rate"),
                         &GDSatiationData::get_source_essence_decay_rate);
    ClassDB::bind_method(D_METHOD("set_source_essence_decay_rate", "rate"),
                         &GDSatiationData::set_source_essence_decay_rate);
    ClassDB::bind_method(D_METHOD("get_source_essence_all"),
                         &GDSatiationData::get_source_essence_all);

    // Food helper constructors
    ClassDB::bind_static_method("GDSatiationData",
                                D_METHOD("make_plant_food", "food_value", "total_essence"),
                                &GDSatiationData::make_plant_food);
    ClassDB::bind_static_method("GDSatiationData",
                                D_METHOD("make_creature_food", "food_value", "total_essence", "creature_element"),
                                &GDSatiationData::make_creature_food);

    // Tick & reset
    ClassDB::bind_method(D_METHOD("tick"),
                         &GDSatiationData::tick);
    ClassDB::bind_method(D_METHOD("reset"),
                         &GDSatiationData::reset);

    // Serialization
    ClassDB::bind_method(D_METHOD("to_dict"),
                         &GDSatiationData::to_dict);
    ClassDB::bind_method(D_METHOD("from_dict", "data"),
                         &GDSatiationData::from_dict);

    // Property bindings
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "satiation_max", PROPERTY_HINT_RANGE, "1,1000,0.1"),
                 "set_satiation_max", "get_satiation_max");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "decay_rate", PROPERTY_HINT_RANGE, "0,10,0.001"),
                 "set_decay_rate", "get_decay_rate");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "peckish_threshold", PROPERTY_HINT_RANGE, "0,100,0.1"),
                 "set_peckish_threshold", "get_peckish_threshold");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hungry_threshold", PROPERTY_HINT_RANGE, "0,100,0.1"),
                 "set_hungry_threshold", "get_hungry_threshold");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "starving_threshold", PROPERTY_HINT_RANGE, "0,100,0.1"),
                 "set_starving_threshold", "get_starving_threshold");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "source_essence_cap", PROPERTY_HINT_RANGE, "0,1000,0.1"),
                 "set_source_essence_cap", "get_source_essence_cap");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "source_essence_decay_rate", PROPERTY_HINT_RANGE, "0,10,0.001"),
                 "set_source_essence_decay_rate", "get_source_essence_decay_rate");
}

// ============================================================
// Satiation value
// ============================================================

float GDSatiationData::get_satiation_current() const {
    return data_.satiation_current();
}

float GDSatiationData::get_satiation_max() const {
    return data_.satiation_max();
}

float GDSatiationData::get_decay_rate() const {
    return data_.decay_rate();
}

void GDSatiationData::set_satiation_max(float max_val) {
    data_.set_satiation_max(max_val);
}

void GDSatiationData::set_decay_rate(float rate) {
    data_.set_decay_rate(rate);
}

float GDSatiationData::eat(float food_value) {
    return data_.eat(food_value);
}

float GDSatiationData::eat_food(const Dictionary& food_dict) {
    FoodDef food;
    if (food_dict.has("food_value")) {
        food.food_value = static_cast<float>(food_dict["food_value"]);
    }
    if (food_dict.has("elements")) {
        Array elements = food_dict["elements"];
        for (int i = 0; i < elements.size(); ++i) {
            Array pair = elements[i];
            if (pair.size() >= 2) {
                int elem_id = static_cast<int>(pair[0]);
                float value = static_cast<float>(pair[1]);
                food.elements.emplace_back(
                    static_cast<magic::RuneElement>(elem_id), value);
            }
        }
    }
    return data_.eat(food);
}

void GDSatiationData::set_satiation(float value) {
    data_.set_satiation(value);
}

// ============================================================
// Hunger level
// ============================================================

int GDSatiationData::get_hunger_level() const {
    return static_cast<int>(data_.hunger_level());
}

bool GDSatiationData::get_is_starving() const {
    return data_.is_starving();
}

// ============================================================
// Effect modifiers
// ============================================================

float GDSatiationData::get_speed_modifier() const {
    return data_.speed_modifier();
}

float GDSatiationData::get_health_regen_modifier() const {
    return data_.health_regen_modifier();
}

float GDSatiationData::get_attack_modifier() const {
    return data_.attack_modifier();
}

float GDSatiationData::get_starvation_damage_per_tick() const {
    return data_.starvation_damage_per_tick();
}

// ============================================================
// Thresholds
// ============================================================

float GDSatiationData::get_peckish_threshold() const {
    return data_.peckish_threshold();
}

float GDSatiationData::get_hungry_threshold() const {
    return data_.hungry_threshold();
}

float GDSatiationData::get_starving_threshold() const {
    return data_.starving_threshold();
}

void GDSatiationData::set_peckish_threshold(float val) {
    data_.set_peckish_threshold(val);
}

void GDSatiationData::set_hungry_threshold(float val) {
    data_.set_hungry_threshold(val);
}

void GDSatiationData::set_starving_threshold(float val) {
    data_.set_starving_threshold(val);
}

// ============================================================
// Source essence pool
// ============================================================

float GDSatiationData::get_source_essence(int element) const {
    return data_.get_source_essence(static_cast<magic::RuneElement>(element));
}

float GDSatiationData::get_total_source_essence() const {
    return data_.get_total_source_essence();
}

float GDSatiationData::get_source_essence_cap() const {
    return data_.source_essence_cap();
}

void GDSatiationData::set_source_essence_cap(float max_val) {
    data_.set_source_essence_cap(max_val);
}

float GDSatiationData::get_source_essence_decay_rate() const {
    return data_.source_essence().decay_rate();
}

void GDSatiationData::set_source_essence_decay_rate(float rate) {
    data_.source_essence().set_decay_rate(rate);
}

Dictionary GDSatiationData::get_source_essence_all() const {
    Dictionary dict;
    auto& vals = data_.source_essence().values();
    for (int i = 0; i < static_cast<int>(magic::RuneElement::COUNT); ++i) {
        dict[i] = vals[i];
    }
    return dict;
}

// ============================================================
// Food helper constructors
// ============================================================

Dictionary GDSatiationData::make_plant_food(float food_value, float total_essence) {
    FoodDef food = science_and_theology::make_plant_food(food_value, total_essence);
    Dictionary dict;
    dict["food_value"] = food.food_value;
    Array elements;
    for (const auto& [elem, val] : food.elements) {
        Array pair;
        pair.append(static_cast<int>(elem));
        pair.append(val);
        elements.append(pair);
    }
    dict["elements"] = elements;
    return dict;
}

Dictionary GDSatiationData::make_creature_food(float food_value, float total_essence,
                                                int creature_element) {
    FoodDef food = science_and_theology::make_creature_food(
        food_value, total_essence,
        static_cast<magic::RuneElement>(creature_element));
    Dictionary dict;
    dict["food_value"] = food.food_value;
    Array elements;
    for (const auto& [elem, val] : food.elements) {
        Array pair;
        pair.append(static_cast<int>(elem));
        pair.append(val);
        elements.append(pair);
    }
    dict["elements"] = elements;
    return dict;
}

// ============================================================
// Tick & reset
// ============================================================

int GDSatiationData::tick() {
    return static_cast<int>(data_.tick());
}

void GDSatiationData::reset() {
    data_.reset();
}

// ============================================================
// Serialization
// ============================================================

Dictionary GDSatiationData::to_dict() const {
    auto s = data_.to_serialized();
    Dictionary dict;
    dict["satiation_current"] = s.satiation_current;
    dict["satiation_max"] = s.satiation_max;
    dict["decay_rate"] = s.decay_rate;
    dict["peckish_threshold"] = s.peckish_threshold;
    dict["hungry_threshold"] = s.hungry_threshold;
    dict["starving_threshold"] = s.starving_threshold;
    dict["source_essence_cap"] = s.source_essence_cap;
    dict["source_essence_decay_rate"] = s.source_essence_decay_rate;

    Array essence_arr;
    for (int i = 0; i < static_cast<int>(magic::RuneElement::COUNT); ++i) {
        essence_arr.append(s.source_essence_values[i]);
    }
    dict["source_essence_values"] = essence_arr;

    return dict;
}

void GDSatiationData::from_dict(const Dictionary& data) {
    SatiationData::SerializedData s;
    if (data.has("satiation_current")) s.satiation_current = static_cast<float>(data["satiation_current"]);
    if (data.has("satiation_max")) s.satiation_max = static_cast<float>(data["satiation_max"]);
    if (data.has("decay_rate")) s.decay_rate = static_cast<float>(data["decay_rate"]);
    if (data.has("peckish_threshold")) s.peckish_threshold = static_cast<float>(data["peckish_threshold"]);
    if (data.has("hungry_threshold")) s.hungry_threshold = static_cast<float>(data["hungry_threshold"]);
    if (data.has("starving_threshold")) s.starving_threshold = static_cast<float>(data["starving_threshold"]);
    if (data.has("source_essence_cap")) s.source_essence_cap = static_cast<float>(data["source_essence_cap"]);
    if (data.has("source_essence_decay_rate")) s.source_essence_decay_rate = static_cast<float>(data["source_essence_decay_rate"]);

    if (data.has("source_essence_values")) {
        Array arr = data["source_essence_values"];
        for (int i = 0; i < static_cast<int>(magic::RuneElement::COUNT) && i < arr.size(); ++i) {
            s.source_essence_values[i] = static_cast<float>(arr[i]);
        }
    }

    data_.from_serialized(s);
}

} // namespace science_and_theology
