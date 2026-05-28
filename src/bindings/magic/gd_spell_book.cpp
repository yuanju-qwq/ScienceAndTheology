#include "gd_spell_book.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "core/magic/glyph_registry.hpp"

using namespace godot;

namespace science_and_theology {

using namespace magic;

GDSpellBook::GDSpellBook() = default;

void GDSpellBook::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "tier_level"),
                         &GDSpellBook::configure);

    ClassDB::bind_method(D_METHOD("get_tier"),
                         &GDSpellBook::get_tier);
    ClassDB::bind_method(D_METHOD("get_max_presets"),
                         &GDSpellBook::get_max_presets);
    ClassDB::bind_method(D_METHOD("get_max_augments"),
                         &GDSpellBook::get_max_augments);
    ClassDB::bind_method(D_METHOD("get_mana_multiplier"),
                         &GDSpellBook::get_mana_multiplier);
    ClassDB::bind_method(D_METHOD("get_active_preset"),
                         &GDSpellBook::get_active_preset);

    ClassDB::bind_method(D_METHOD("set_active_preset", "index"),
                         &GDSpellBook::set_active_preset);
    ClassDB::bind_method(D_METHOD("save_current_preset", "slot"),
                         &GDSpellBook::save_current_preset);

    ClassDB::bind_method(D_METHOD("set_form", "preset_index", "glyph_name"),
                         &GDSpellBook::set_form);
    ClassDB::bind_method(D_METHOD("set_effect", "preset_index", "glyph_name"),
                         &GDSpellBook::set_effect);
    ClassDB::bind_method(D_METHOD("add_augment", "preset_index", "glyph_name"),
                         &GDSpellBook::add_augment);
    ClassDB::bind_method(D_METHOD("clear_preset", "preset_index"),
                         &GDSpellBook::clear_preset);

    ClassDB::bind_method(D_METHOD("compile_spell", "preset_index"),
                         &GDSpellBook::compile_spell, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("can_cast", "mana_dict", "last_cast_tick"),
                         &GDSpellBook::can_cast);

    ClassDB::bind_method(D_METHOD("get_preset_info", "preset_index"),
                         &GDSpellBook::get_preset_info);
    ClassDB::bind_method(D_METHOD("get_glyph_info", "preset_index", "slot"),
                         &GDSpellBook::get_glyph_info);
}

void GDSpellBook::configure(int tier_level) {
    book_.tier = static_cast<SpellBookTier>(tier_level);
}

int GDSpellBook::get_tier() const {
    return static_cast<int>(book_.tier);
}

int GDSpellBook::get_max_presets() const {
    return book_.max_presets();
}

int GDSpellBook::get_max_augments() const {
    return book_.max_augments();
}

float GDSpellBook::get_mana_multiplier() const {
    return book_.mana_multiplier();
}

int GDSpellBook::get_active_preset() const {
    return book_.active_preset;
}

bool GDSpellBook::set_active_preset(int index) {
    return book_.switch_preset(index);
}

bool GDSpellBook::save_current_preset(int slot) {
    if (slot < 0 || slot >= book_.max_presets()) return false;
    book_.presets[slot] = book_.current_preset();
    return true;
}

bool GDSpellBook::set_form(int preset_index, const godot::String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::FORM) return false;

    book_.presets[preset_index].form = *glyph;
    return true;
}

bool GDSpellBook::set_effect(int preset_index, const godot::String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::EFFECT) return false;

    book_.presets[preset_index].effect = *glyph;
    return true;
}

bool GDSpellBook::add_augment(int preset_index, const godot::String& glyph_name) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return false;
    const GlyphDef* glyph = GlyphRegistry::get_by_name(glyph_name.utf8().get_data());
    if (glyph == nullptr || glyph->slot_type != GlyphSlotType::AUGMENT) return false;

    auto& augs = book_.presets[preset_index].augments;
    if (static_cast<int>(augs.size()) >= book_.max_augments()) return false;

    augs.push_back(*glyph);
    return true;
}

void GDSpellBook::clear_preset(int preset_index) {
    if (preset_index < 0 || preset_index >= book_.max_presets()) return;
    book_.presets[preset_index] = SpellPreset{};
}

godot::Dictionary GDSpellBook::spell_to_dict(const SpellDef& spell) {
    godot::Dictionary dict;
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

godot::Dictionary GDSpellBook::compile_spell(int preset_index) const {
    int idx = (preset_index < 0) ? book_.active_preset : preset_index;
    if (idx < 0 || idx >= book_.max_presets()) return {};

    SpellDef spell = SpellCompiler::compile(book_.presets[idx]);
    return spell_to_dict(spell);
}

bool GDSpellBook::can_cast(const godot::Dictionary& mana_dict, int64_t last_cast_tick) const {
    const SpellPreset& preset = book_.current_preset();
    if (!preset.can_cast()) return false;

    SpellDef spell = SpellCompiler::compile(preset);

    int current = mana_dict.get("current", godot::Variant(0));
    ManaPool pool;
    pool.current_mana = current;

    return caster_.can_cast(spell, pool,
                            static_cast<uint64_t>(0),
                            static_cast<uint64_t>(last_cast_tick));
}

godot::Dictionary GDSpellBook::get_preset_info(int preset_index) const {
    godot::Dictionary dict;
    if (preset_index < 0 || preset_index >= book_.max_presets()) return dict;

    const auto& preset = book_.presets[preset_index];
    dict["can_cast"] = preset.can_cast();

    if (preset.form.has_value()) {
        dict["form_name"] = preset.form->name;
    }
    if (preset.effect.has_value()) {
        dict["effect_name"] = preset.effect->name;
    }

    godot::Array aug_names;
    for (const auto& aug : preset.augments) {
        aug_names.append(aug.name);
    }
    dict["augments"] = aug_names;

    return dict;
}

godot::Dictionary GDSpellBook::get_glyph_info(int preset_index, int slot) const {
    godot::Dictionary dict;
    if (preset_index < 0 || preset_index >= book_.max_presets()) return dict;

    const auto& preset = book_.presets[preset_index];

    // slot 0 = form, 1 = effect, 2+ = augment
    if (slot == 0 && preset.form.has_value()) {
        dict["name"] = preset.form->name;
        dict["slot_type"] = static_cast<int>(GlyphSlotType::FORM);
        dict["element"] = static_cast<int>(preset.form->element);
        dict["tier"] = static_cast<int>(preset.form->tier);
        dict["potency"] = preset.form->potency;
    } else if (slot == 1 && preset.effect.has_value()) {
        dict["name"] = preset.effect->name;
        dict["slot_type"] = static_cast<int>(GlyphSlotType::EFFECT);
        dict["element"] = static_cast<int>(preset.effect->element);
        dict["tier"] = static_cast<int>(preset.effect->tier);
        dict["potency"] = preset.effect->potency;
    } else {
        int aug_idx = slot - 2;
        if (aug_idx >= 0 && aug_idx < static_cast<int>(preset.augments.size())) {
            const auto& aug = preset.augments[aug_idx];
            dict["name"] = aug.name;
            dict["slot_type"] = static_cast<int>(GlyphSlotType::AUGMENT);
            dict["element"] = static_cast<int>(aug.element);
            dict["tier"] = static_cast<int>(aug.tier);
            dict["potency"] = aug.potency;
        }
    }

    return dict;
}

} // namespace science_and_theology
