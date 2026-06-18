#include "gd_source_law_weapon.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "core/magic/glyph_registry.hpp"
#include "core/magic/mana_pool.hpp"

VARIANT_ENUM_CAST(science_and_theology::GDSourceLawWeapon::WeaponTierConst);

using namespace godot;

namespace science_and_theology {

using namespace magic;

GDSourceLawWeapon::GDSourceLawWeapon() {
    weapon_def_.weapon_tier = SourceLawWeaponTier::BASIC_GUIDE_TOOL;
    book_ = weapon_def_.create_spell_book();
}

void GDSourceLawWeapon::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "tier_level"),
                         &GDSourceLawWeapon::configure);

    ClassDB::bind_method(D_METHOD("get_tier"),
                         &GDSourceLawWeapon::get_tier);
    ClassDB::bind_method(D_METHOD("get_total_glyph_slots"),
                         &GDSourceLawWeapon::get_total_glyph_slots);
    ClassDB::bind_method(D_METHOD("get_max_augment_slots"),
                         &GDSourceLawWeapon::get_max_augment_slots);
    ClassDB::bind_method(D_METHOD("get_max_presets"),
                         &GDSourceLawWeapon::get_max_presets);
    ClassDB::bind_method(D_METHOD("get_mana_multiplier"),
                         &GDSourceLawWeapon::get_mana_multiplier);

    ClassDB::bind_method(D_METHOD("get_active_preset"),
                         &GDSourceLawWeapon::get_active_preset);
    ClassDB::bind_method(D_METHOD("set_active_preset", "index"),
                         &GDSourceLawWeapon::set_active_preset);

    ClassDB::bind_method(D_METHOD("set_form", "preset_index", "glyph_name"),
                         &GDSourceLawWeapon::set_form);
    ClassDB::bind_method(D_METHOD("set_effect", "preset_index", "glyph_name"),
                         &GDSourceLawWeapon::set_effect);
    ClassDB::bind_method(D_METHOD("add_augment", "preset_index", "glyph_name"),
                         &GDSourceLawWeapon::add_augment);
    ClassDB::bind_method(D_METHOD("clear_preset", "preset_index"),
                         &GDSourceLawWeapon::clear_preset);

    ClassDB::bind_method(D_METHOD("compile_spell", "preset_index"),
                         &GDSourceLawWeapon::compile_spell, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("can_cast", "mana_dict", "last_cast_tick"),
                         &GDSourceLawWeapon::can_cast);

    ClassDB::bind_method(D_METHOD("get_preset_info", "preset_index"),
                         &GDSourceLawWeapon::get_preset_info);

    BIND_ENUM_CONSTANT(TIER_BASIC_GUIDE_TOOL);
    BIND_ENUM_CONSTANT(TIER_ARM_GUARD);
    BIND_ENUM_CONSTANT(TIER_AETHER_WEAPON);
}

void GDSourceLawWeapon::configure(int tier_level) {
    weapon_def_.weapon_tier = static_cast<SourceLawWeaponTier>(tier_level);
    book_ = weapon_def_.create_spell_book();
}

int GDSourceLawWeapon::get_tier() const {
    return static_cast<int>(weapon_def_.weapon_tier);
}

int GDSourceLawWeapon::get_total_glyph_slots() const {
    return weapon_def_.total_glyph_slots();
}

int GDSourceLawWeapon::get_max_augment_slots() const {
    return weapon_def_.max_augment_slots();
}

int GDSourceLawWeapon::get_max_presets() const {
    return weapon_def_.max_presets();
}

float GDSourceLawWeapon::get_mana_multiplier() const {
    return weapon_def_.mana_multiplier();
}

int GDSourceLawWeapon::get_active_preset() const {
    return book_.active_preset;
}

bool GDSourceLawWeapon::set_active_preset(int index) {
    return book_.switch_preset(index);
}

bool GDSourceLawWeapon::set_form(int preset_index, const String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::FORM) return false;

    book_.presets[preset_index].form = *glyph;
    return true;
}

bool GDSourceLawWeapon::set_effect(int preset_index, const String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::EFFECT) return false;

    book_.presets[preset_index].effect = *glyph;
    return true;
}

bool GDSourceLawWeapon::add_augment(int preset_index, const String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::AUGMENT) return false;

    auto& augs = book_.presets[preset_index].augments;
    if (static_cast<int>(augs.size()) >= weapon_def_.max_augment_slots()) return false;

    augs.push_back(*glyph);
    return true;
}

void GDSourceLawWeapon::clear_preset(int preset_index) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return;
    book_.presets[preset_index] = SpellPreset{};
}

Dictionary GDSourceLawWeapon::spell_to_dict(const SpellDef& spell) {
    Dictionary dict;
    dict["form"] = static_cast<int>(spell.form);
    dict["element"] = static_cast<int>(spell.element);
    dict["potency"] = spell.potency;
    dict["damage_mult"] = spell.damage_mult;
    dict["range"] = spell.range;
    dict["cast_time_sec"] = spell.cast_time_sec;
    dict["mana_cost"] = spell.mana_cost;
    dict["cooldown_ticks"] = spell.cooldown_ticks;
    dict["homing"] = spell.homing;
    dict["pierce_ratio"] = spell.pierce_ratio;
    dict["chaos_factor"] = spell.chaos_factor;
    return dict;
}

Dictionary GDSourceLawWeapon::compile_spell(int preset_index) const {
    int idx = (preset_index < 0) ? book_.active_preset : preset_index;
    if (idx < 0 || idx >= book_.max_presets()) return {};

    SpellDef spell = SpellCompiler::compile(book_.presets[idx]);

    // Apply weapon mana multiplier.
    spell.mana_cost = static_cast<int>(
        static_cast<float>(spell.mana_cost) * weapon_def_.mana_multiplier());
    if (spell.mana_cost < 1) spell.mana_cost = 1;

    return spell_to_dict(spell);
}

bool GDSourceLawWeapon::can_cast(const Dictionary& mana_dict, int64_t last_cast_tick) const {
    const SpellPreset& preset = book_.current_preset();
    if (!preset.can_cast()) return false;

    SpellDef spell = SpellCompiler::compile(preset);
    spell.mana_cost = static_cast<int>(
        static_cast<float>(spell.mana_cost) * weapon_def_.mana_multiplier());
    if (spell.mana_cost < 1) spell.mana_cost = 1;

    int current = mana_dict.get("current", Variant(0));
    ManaPool pool;
    pool.current_mana = current;

    return caster_.can_cast(spell, pool,
                            static_cast<uint64_t>(0),
                            static_cast<uint64_t>(last_cast_tick));
}

Dictionary GDSourceLawWeapon::get_preset_info(int preset_index) const {
    Dictionary dict;
    if (preset_index < 0 || preset_index >= book_.max_presets()) return dict;

    const auto& preset = book_.presets[preset_index];
    dict["can_cast"] = preset.can_cast();

    if (preset.form.has_value()) {
        dict["form_name"] = preset.form->name;
    }
    if (preset.effect.has_value()) {
        dict["effect_name"] = preset.effect->name;
    }

    Array aug_names;
    for (const auto& aug : preset.augments) {
        aug_names.append(aug.name);
    }
    dict["augments"] = aug_names;

    return dict;
}

} // namespace science_and_theology
