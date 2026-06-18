#include "gd_player_source_law_data.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "core/source_law/dropped_organ_registry.hpp"
#include "core/source_law/elixir_registry.hpp"
#include "core/source_law/sublimation_path_registry.hpp"

using namespace godot;

namespace science_and_theology {

void GDPlayerSourceLawData::_bind_methods() {
    // Source reserve
    ClassDB::bind_method(D_METHOD("get_source_current"),
                         &GDPlayerSourceLawData::get_source_current);
    ClassDB::bind_method(D_METHOD("get_source_max"),
                         &GDPlayerSourceLawData::get_source_max);
    ClassDB::bind_method(D_METHOD("get_source_regen"),
                         &GDPlayerSourceLawData::get_source_regen);
    ClassDB::bind_method(D_METHOD("set_source_max", "max_val"),
                         &GDPlayerSourceLawData::set_source_max);
    ClassDB::bind_method(D_METHOD("add_source", "amount"),
                         &GDPlayerSourceLawData::add_source);
    ClassDB::bind_method(D_METHOD("consume_source", "amount"),
                         &GDPlayerSourceLawData::consume_source);

    // Stability
    ClassDB::bind_method(D_METHOD("get_stability"),
                         &GDPlayerSourceLawData::get_stability);
    ClassDB::bind_method(D_METHOD("set_stability", "val"),
                         &GDPlayerSourceLawData::set_stability);
    ClassDB::bind_method(D_METHOD("modify_stability", "delta"),
                         &GDPlayerSourceLawData::modify_stability);

    // Mutation
    ClassDB::bind_method(D_METHOD("get_mutation"),
                         &GDPlayerSourceLawData::get_mutation);
    ClassDB::bind_method(D_METHOD("set_mutation", "val"),
                         &GDPlayerSourceLawData::set_mutation);
    ClassDB::bind_method(D_METHOD("modify_mutation", "delta"),
                         &GDPlayerSourceLawData::modify_mutation);

    // Psionic level
    ClassDB::bind_method(D_METHOD("get_psionic_level"),
                         &GDPlayerSourceLawData::get_psionic_level);
    ClassDB::bind_method(D_METHOD("set_psionic_level", "level"),
                         &GDPlayerSourceLawData::set_psionic_level);

    // Mental load
    ClassDB::bind_method(D_METHOD("get_mental_load"),
                         &GDPlayerSourceLawData::get_mental_load);
    ClassDB::bind_method(D_METHOD("set_mental_load", "load"),
                         &GDPlayerSourceLawData::set_mental_load);
    ClassDB::bind_method(D_METHOD("modify_mental_load", "delta"),
                         &GDPlayerSourceLawData::modify_mental_load);

    // Sublimation
    ClassDB::bind_method(D_METHOD("get_sublimation_level"),
                         &GDPlayerSourceLawData::get_sublimation_level);
    ClassDB::bind_method(D_METHOD("get_path_id"),
                         &GDPlayerSourceLawData::get_path_id);
    ClassDB::bind_method(D_METHOD("set_sublimation_level", "level"),
                         &GDPlayerSourceLawData::set_sublimation_level);
    ClassDB::bind_method(D_METHOD("set_path_id", "path"),
                         &GDPlayerSourceLawData::set_path_id);

    // Initiation
    ClassDB::bind_method(D_METHOD("is_initiated"),
                         &GDPlayerSourceLawData::is_initiated);

    // Source cost modifier
    ClassDB::bind_method(D_METHOD("compute_source_cost_multiplier", "new_element", "exclude_slot"),
                         &GDPlayerSourceLawData::compute_source_cost_multiplier);

    // Mana pool
    ClassDB::bind_method(D_METHOD("get_mana_current"),
                         &GDPlayerSourceLawData::get_mana_current);
    ClassDB::bind_method(D_METHOD("get_mana_max"),
                         &GDPlayerSourceLawData::get_mana_max);
    ClassDB::bind_method(D_METHOD("get_mana_fill_percent"),
                         &GDPlayerSourceLawData::get_mana_fill_percent);
    ClassDB::bind_method(D_METHOD("consume_mana", "amount"),
                         &GDPlayerSourceLawData::consume_mana);
    ClassDB::bind_method(D_METHOD("add_mana", "amount"),
                         &GDPlayerSourceLawData::add_mana);

    // Organs
    ClassDB::bind_method(D_METHOD("get_organ", "slot"),
                         &GDPlayerSourceLawData::get_organ);
    ClassDB::bind_method(D_METHOD("transform_organ", "slot", "element", "path", "source_cost"),
                         &GDPlayerSourceLawData::transform_organ);
    ClassDB::bind_method(D_METHOD("purify_organ", "slot"),
                         &GDPlayerSourceLawData::purify_organ);
    ClassDB::bind_method(D_METHOD("purify_all_organs"),
                         &GDPlayerSourceLawData::purify_all_organs);
    ClassDB::bind_method(D_METHOD("tune_organ", "slot", "degree_delta"),
                         &GDPlayerSourceLawData::tune_organ);

    // Elixir
    ClassDB::bind_method(D_METHOD("apply_elixir", "elixir_id"),
                         &GDPlayerSourceLawData::apply_elixir);

    // Bloodline
    ClassDB::bind_method(D_METHOD("devour_organ", "dropped_organ_id"),
                         &GDPlayerSourceLawData::devour_organ);

    // Rejection
    ClassDB::bind_method(D_METHOD("is_rejecting"),
                         &GDPlayerSourceLawData::is_rejecting);
    ClassDB::bind_method(D_METHOD("get_rejection"),
                         &GDPlayerSourceLawData::get_rejection);
    ClassDB::bind_method(D_METHOD("get_rejection_progress"),
                         &GDPlayerSourceLawData::get_rejection_progress);
    ClassDB::bind_method(D_METHOD("handle_rejection_death"),
                         &GDPlayerSourceLawData::handle_rejection_death);
    ClassDB::bind_method(D_METHOD("complete_rejection"),
                         &GDPlayerSourceLawData::complete_rejection);

    ClassDB::bind_method(D_METHOD("get_available_skills"),
                         &GDPlayerSourceLawData::get_available_skills);

    // Affinities
    ClassDB::bind_method(D_METHOD("get_affinity", "element"),
                         &GDPlayerSourceLawData::get_affinity);
    ClassDB::bind_method(D_METHOD("set_affinity", "element", "value"),
                         &GDPlayerSourceLawData::set_affinity);

    // Network report
    ClassDB::bind_method(D_METHOD("compute_network_report"),
                         &GDPlayerSourceLawData::compute_network_report);

    // Combat attributes
    ClassDB::bind_method(D_METHOD("compute_combat_attributes"),
                         &GDPlayerSourceLawData::compute_combat_attributes);

    // Tick & reset
    ClassDB::bind_method(D_METHOD("tick"),
                         &GDPlayerSourceLawData::tick);
    ClassDB::bind_method(D_METHOD("reset"),
                         &GDPlayerSourceLawData::reset);

    // Serialization
    ClassDB::bind_method(D_METHOD("to_dict"),
                         &GDPlayerSourceLawData::to_dict);
    ClassDB::bind_method(D_METHOD("from_dict", "data"),
                         &GDPlayerSourceLawData::from_dict);
}

// ============================================================
// Source reserve
// ============================================================

int GDPlayerSourceLawData::get_source_current() const {
    return data_.source_current();
}

int GDPlayerSourceLawData::get_source_max() const {
    return data_.source_max();
}

float GDPlayerSourceLawData::get_source_regen() const {
    return data_.source_regen();
}

void GDPlayerSourceLawData::set_source_max(int max_val) {
    data_.set_source_max(max_val);
}

void GDPlayerSourceLawData::add_source(int amount) {
    data_.add_source(amount);
}

bool GDPlayerSourceLawData::consume_source(int amount) {
    return data_.consume_source(amount);
}

// ============================================================
// Stability
// ============================================================

float GDPlayerSourceLawData::get_stability() const {
    return data_.stability();
}

void GDPlayerSourceLawData::set_stability(float val) {
    data_.set_stability(val);
}

void GDPlayerSourceLawData::modify_stability(float delta) {
    data_.modify_stability(delta);
}

// ============================================================
// Mutation
// ============================================================

float GDPlayerSourceLawData::get_mutation() const {
    return data_.mutation();
}

void GDPlayerSourceLawData::set_mutation(float val) {
    data_.set_mutation(val);
}

void GDPlayerSourceLawData::modify_mutation(float delta) {
    data_.modify_mutation(delta);
}

// ============================================================
// Psionic level
// ============================================================

int GDPlayerSourceLawData::get_psionic_level() const {
    return data_.psionic_level();
}

void GDPlayerSourceLawData::set_psionic_level(int level) {
    data_.set_psionic_level(level);
}

// ============================================================
// Mental load
// ============================================================

int GDPlayerSourceLawData::get_mental_load() const {
    return data_.mental_load();
}

void GDPlayerSourceLawData::set_mental_load(int load) {
    data_.set_mental_load(load);
}

void GDPlayerSourceLawData::modify_mental_load(int delta) {
    data_.modify_mental_load(delta);
}

// ============================================================
// Sublimation
// ============================================================

int GDPlayerSourceLawData::get_sublimation_level() const {
    return data_.sublimation_level();
}

int GDPlayerSourceLawData::get_path_id() const {
    return static_cast<int>(data_.path());
}

void GDPlayerSourceLawData::set_sublimation_level(int level) {
    data_.set_sublimation_level(level);
}

void GDPlayerSourceLawData::set_path_id(int path) {
    data_.set_path(static_cast<source_law::SublimationPath>(path));
}

// ============================================================
// Initiation
// ============================================================

bool GDPlayerSourceLawData::is_initiated() const {
    return data_.is_initiated();
}

float GDPlayerSourceLawData::compute_source_cost_multiplier(
    int new_element, int exclude_slot) const {
    return data_.compute_source_cost_multiplier(
        static_cast<magic::RuneElement>(new_element),
        static_cast<source_law::OrganSlot>(exclude_slot));
}

// ============================================================
// Mana pool
// ============================================================

int GDPlayerSourceLawData::get_mana_current() const {
    return data_.mana_pool().current_mana;
}

int GDPlayerSourceLawData::get_mana_max() const {
    return data_.mana_pool().max_mana;
}

float GDPlayerSourceLawData::get_mana_fill_percent() const {
    return data_.mana_pool().fill_percent();
}

bool GDPlayerSourceLawData::consume_mana(int amount) {
    return data_.mana_pool().consume(amount);
}

void GDPlayerSourceLawData::add_mana(int amount) {
    data_.mana_pool().add(amount);
}

// ============================================================
// Organs
// ============================================================

Dictionary GDPlayerSourceLawData::get_organ(int slot) const {
    Dictionary dict;
    if (slot < 0 || slot >= source_law::kOrganSlotCount) return dict;

    const auto& organ = data_.get_organ(static_cast<source_law::OrganSlot>(slot));
    dict["slot"] = static_cast<int>(organ.slot);
    dict["sublimation_degree"] = organ.sublimation_degree;
    dict["path_id"] = static_cast<int>(organ.path_id);
    dict["transform_type"] = static_cast<int>(organ.transform_type);
    dict["power_multiplier"] = organ.power_multiplier;
    dict["bloodline_source"] = static_cast<int>(organ.bloodline_source);
    dict["source_required"] = organ.source_required;
    dict["source_paid"] = organ.source_paid;
    dict["primary_element"] = static_cast<int>(organ.primary_element);
    dict["quality"] = static_cast<int>(organ.quality);
    dict["level"] = organ.level;
    dict["stability_modifier"] = organ.stability_modifier;
    dict["mutation_risk"] = organ.mutation_risk;
    dict["psionic_modifier"] = organ.psionic_modifier;
    dict["mental_load_modifier"] = organ.mental_load_modifier;
    dict["is_sublimated"] = organ.is_sublimated();
    dict["is_bloodline"] = organ.is_bloodline();
    dict["is_partial"] = organ.is_partial();
    dict["source_ratio"] = organ.source_ratio();
    return dict;
}

bool GDPlayerSourceLawData::transform_organ(int slot, int element, int path, int source_cost) {
    return data_.transform_organ(
        static_cast<source_law::OrganSlot>(slot),
        static_cast<magic::RuneElement>(element),
        static_cast<source_law::SublimationPath>(path),
        source_cost);
}

void GDPlayerSourceLawData::purify_organ(int slot) {
    data_.purify_organ(static_cast<source_law::OrganSlot>(slot));
}

void GDPlayerSourceLawData::purify_all_organs() {
    data_.purify_all_organs();
}

void GDPlayerSourceLawData::tune_organ(int slot, int degree_delta) {
    data_.tune_organ(static_cast<source_law::OrganSlot>(slot), degree_delta);
}

bool GDPlayerSourceLawData::apply_elixir(const String& elixir_id) {
    const auto* recipe = source_law::ElixirRegistry::get_by_name(
        elixir_id.utf8().get_data());
    if (!recipe) return false;
    return data_.apply_elixir(*recipe);
}

bool GDPlayerSourceLawData::devour_organ(const String& dropped_organ_id) {
    const auto* dropped = source_law::DroppedOrganRegistry::get_by_name(
        dropped_organ_id.utf8().get_data());
    if (!dropped) return false;
    return data_.devour_organ(*dropped);
}

// ============================================================
// Transform rejection (排异掉血)
// ============================================================

bool GDPlayerSourceLawData::is_rejecting() const {
    return data_.is_rejecting();
}

Dictionary GDPlayerSourceLawData::get_rejection() const {
    Dictionary dict;
    const auto* rej = data_.rejection();
    if (!rej) return dict;
    dict["slot"] = static_cast<int>(rej->slot);
    dict["source_type"] = static_cast<int>(rej->source_type);
    dict["ticks_remaining"] = rej->ticks_remaining;
    dict["total_ticks"] = rej->total_ticks;
    dict["damage_per_tick"] = rej->damage_per_tick;
    dict["active"] = rej->active;
    return dict;
}

float GDPlayerSourceLawData::get_rejection_progress() const {
    return data_.rejection_progress();
}

void GDPlayerSourceLawData::handle_rejection_death() {
    data_.handle_rejection_death();
}

void GDPlayerSourceLawData::complete_rejection() {
    data_.complete_rejection();
}

Array GDPlayerSourceLawData::get_available_skills() const {
    Array result;
    auto skills = source_law::SublimationPathRegistry::get_available_skills(
        data_.organs());
    for (const auto* skill : skills) {
        Dictionary dict;
        dict["id"] = skill->id;
        dict["title_key"] = skill->title_key;
        dict["required_slot"] = static_cast<int>(skill->required_slot);
        dict["required_path"] = static_cast<int>(skill->required_path);
        dict["min_organ_level"] = skill->min_organ_level;
        dict["mana_cost"] = skill->mana_cost;
        dict["cooldown_ticks"] = skill->cooldown_ticks;
        dict["effect_type"] = skill->effect_type;
        dict["effect_param_1"] = skill->effect_param_1;
        dict["effect_param_2"] = skill->effect_param_2;
        result.append(dict);
    }
    return result;
}

// ============================================================
// Element affinities
// ============================================================

int GDPlayerSourceLawData::get_affinity(int element) const {
    return data_.get_affinity(static_cast<magic::RuneElement>(element));
}

void GDPlayerSourceLawData::set_affinity(int element, int value) {
    data_.set_affinity(static_cast<magic::RuneElement>(element), value);
}

// ============================================================
// Network affinity report
// ============================================================

Dictionary GDPlayerSourceLawData::compute_network_report() const {
    auto report = data_.compute_network_report();
    Dictionary dict;
    dict["element_stability_modifier"] = report.element_stability_modifier;
    dict["element_mutation_modifier"] = report.element_mutation_modifier;
    dict["has_severe_conflict"] = report.has_severe_conflict;

    Array conflict_pairs;
    for (const auto& pair : report.severe_conflict_pairs) {
        Array arr;
        arr.append(static_cast<int>(pair.first));
        arr.append(static_cast<int>(pair.second));
        conflict_pairs.append(arr);
    }
    dict["severe_conflict_pairs"] = conflict_pairs;

    Dictionary weights;
    for (const auto& [elem, weight] : report.element_weights) {
        weights[static_cast<int>(elem)] = weight;
    }
    dict["element_weights"] = weights;

    return dict;
}

// ============================================================
// Combat attributes
// ============================================================

Dictionary GDPlayerSourceLawData::compute_combat_attributes() const {
    auto attrs = data_.compute_combat_attributes();
    Dictionary dict;
    dict["health_max"] = attrs.health_max;
    dict["mana_max"] = attrs.mana_max;
    dict["physical_attack"] = attrs.physical_attack;
    dict["magic_power"] = attrs.magic_power;
    dict["physical_defense"] = attrs.physical_defense;
    dict["element_resistance"] = attrs.element_resistance;
    dict["move_speed"] = attrs.move_speed;
    dict["attack_speed"] = attrs.attack_speed;
    dict["cast_speed"] = attrs.cast_speed;
    dict["crit_rate"] = attrs.crit_rate;
    dict["crit_damage"] = attrs.crit_damage;
    dict["dodge_rate"] = attrs.dodge_rate;
    dict["health_regen"] = attrs.health_regen;
    dict["mana_regen"] = attrs.mana_regen;
    return dict;
}

// ============================================================
// Tick & reset
// ============================================================

void GDPlayerSourceLawData::tick() {
    data_.tick();
}

void GDPlayerSourceLawData::reset() {
    data_.reset();
}

// ============================================================
// Serialization
// ============================================================

Dictionary GDPlayerSourceLawData::to_dict() const {
    auto s = data_.to_serialized();
    Dictionary dict;
    dict["sublimation_level"] = s.sublimation_level;
    dict["path_id"] = s.path_id;
    dict["source_current"] = s.source_current;
    dict["source_max"] = s.source_max;
    dict["source_regen"] = s.source_regen;
    dict["mana_current"] = s.mana_current;
    dict["mana_max"] = s.mana_max;
    dict["mana_regen"] = s.mana_regen;
    dict["stability"] = s.stability;
    dict["mutation"] = s.mutation;
    dict["psionic_level"] = s.psionic_level;
    dict["mental_load"] = s.mental_load;
    dict["affinity_count"] = s.affinity_count;

    // Rejection state
    dict["rejection_slot"] = s.rejection_slot;
    dict["rejection_source_type"] = s.rejection_source_type;
    dict["rejection_ticks_remaining"] = s.rejection_ticks_remaining;
    dict["rejection_total_ticks"] = s.rejection_total_ticks;
    dict["rejection_damage_per_tick"] = s.rejection_damage_per_tick;
    dict["rejection_active"] = s.rejection_active;

    Array organs_arr;
    for (int i = 0; i < source_law::kOrganSlotCount; ++i) {
        const auto& so = s.organs[i];
        Dictionary od;
        od["slot"] = so.slot;
        od["sublimation_degree"] = so.sublimation_degree;
        od["path_id"] = so.path_id;
        od["transform_type"] = so.transform_type;
        od["power_multiplier"] = so.power_multiplier;
        od["bloodline_source"] = so.bloodline_source;
        od["source_required"] = so.source_required;
        od["source_paid"] = so.source_paid;
        od["primary_element"] = so.primary_element;
        od["quality"] = so.quality;
        od["level"] = so.level;
        od["stability_modifier"] = so.stability_modifier;
        od["mutation_risk"] = so.mutation_risk;
        od["psionic_modifier"] = so.psionic_modifier;
        od["mental_load_modifier"] = so.mental_load_modifier;
        organs_arr.append(od);
    }
    dict["organs"] = organs_arr;

    return dict;
}

void GDPlayerSourceLawData::from_dict(const Dictionary& data) {
    source_law::PlayerSourceLawData::SerializedData s;
    if (data.has("sublimation_level")) s.sublimation_level = static_cast<int>(data["sublimation_level"]);
    if (data.has("path_id")) s.path_id = static_cast<int>(data["path_id"]);
    if (data.has("source_current")) s.source_current = static_cast<int>(data["source_current"]);
    if (data.has("source_max")) s.source_max = static_cast<int>(data["source_max"]);
    if (data.has("source_regen")) s.source_regen = static_cast<float>(data["source_regen"]);
    if (data.has("mana_current")) s.mana_current = static_cast<int>(data["mana_current"]);
    if (data.has("mana_max")) s.mana_max = static_cast<int>(data["mana_max"]);
    if (data.has("mana_regen")) s.mana_regen = static_cast<float>(data["mana_regen"]);
    if (data.has("stability")) s.stability = static_cast<float>(data["stability"]);
    if (data.has("mutation")) s.mutation = static_cast<float>(data["mutation"]);
    if (data.has("psionic_level")) s.psionic_level = static_cast<int>(data["psionic_level"]);
    if (data.has("mental_load")) s.mental_load = static_cast<int>(data["mental_load"]);
    if (data.has("affinity_count")) s.affinity_count = static_cast<int>(data["affinity_count"]);

    if (data.has("organs")) {
        Array organs_arr = data["organs"];
        for (int i = 0; i < source_law::kOrganSlotCount && i < organs_arr.size(); ++i) {
            Dictionary od = organs_arr[i];
            auto& so = s.organs[i];
            if (od.has("slot")) so.slot = static_cast<int>(od["slot"]);
            if (od.has("sublimation_degree")) so.sublimation_degree = static_cast<int>(od["sublimation_degree"]);
            if (od.has("path_id")) so.path_id = static_cast<int>(od["path_id"]);
            if (od.has("transform_type")) so.transform_type = static_cast<int>(od["transform_type"]);
            if (od.has("power_multiplier")) so.power_multiplier = static_cast<float>(od["power_multiplier"]);
            if (od.has("bloodline_source")) so.bloodline_source = static_cast<int>(od["bloodline_source"]);
            if (od.has("source_required")) so.source_required = static_cast<int>(od["source_required"]);
            if (od.has("source_paid")) so.source_paid = static_cast<int>(od["source_paid"]);
            if (od.has("primary_element")) so.primary_element = static_cast<int>(od["primary_element"]);
            if (od.has("quality")) so.quality = static_cast<int>(od["quality"]);
            if (od.has("level")) so.level = static_cast<int>(od["level"]);
            if (od.has("stability_modifier")) so.stability_modifier = static_cast<float>(od["stability_modifier"]);
            if (od.has("mutation_risk")) so.mutation_risk = static_cast<float>(od["mutation_risk"]);
            if (od.has("psionic_modifier")) so.psionic_modifier = static_cast<int>(od["psionic_modifier"]);
            if (od.has("mental_load_modifier")) so.mental_load_modifier = static_cast<int>(od["mental_load_modifier"]);
        }
    }

    // Rejection state
    if (data.has("rejection_slot")) s.rejection_slot = static_cast<int>(data["rejection_slot"]);
    if (data.has("rejection_source_type")) s.rejection_source_type = static_cast<int>(data["rejection_source_type"]);
    if (data.has("rejection_ticks_remaining")) s.rejection_ticks_remaining = static_cast<int>(data["rejection_ticks_remaining"]);
    if (data.has("rejection_total_ticks")) s.rejection_total_ticks = static_cast<int>(data["rejection_total_ticks"]);
    if (data.has("rejection_damage_per_tick")) s.rejection_damage_per_tick = static_cast<float>(data["rejection_damage_per_tick"]);
    if (data.has("rejection_active")) s.rejection_active = static_cast<int>(data["rejection_active"]);

    data_.from_serialized(s);
}

} // namespace science_and_theology
