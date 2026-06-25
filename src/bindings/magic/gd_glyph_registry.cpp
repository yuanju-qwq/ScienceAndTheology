#include "gd_glyph_registry.hpp"

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace science_and_theology {

using namespace magic;

void GDGlyphRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDGlyphRegistry",
        D_METHOD("register_glyph", "def"),
        &GDGlyphRegistry::register_glyph);
    ClassDB::bind_method(D_METHOD("get_glyph_by_name", "name"),
                         &GDGlyphRegistry::get_glyph_by_name);
    ClassDB::bind_method(D_METHOD("get_effect_glyph", "element", "tier"),
                         &GDGlyphRegistry::get_effect_glyph);
    ClassDB::bind_method(D_METHOD("get_augment_glyph", "element", "tier"),
                         &GDGlyphRegistry::get_augment_glyph);
    ClassDB::bind_method(D_METHOD("get_form_glyph", "form"),
                         &GDGlyphRegistry::get_form_glyph);
    ClassDB::bind_method(D_METHOD("get_glyph_count"),
                         &GDGlyphRegistry::get_glyph_count);
    ClassDB::bind_method(D_METHOD("get_all_glyph_names"),
                         &GDGlyphRegistry::get_all_glyph_names);
    ClassDB::bind_method(D_METHOD("get_form_glyph_names"),
                         &GDGlyphRegistry::get_form_glyph_names);
}

bool GDGlyphRegistry::register_glyph(const Dictionary& def) {
    String name = def.get("name", "");
    if (name.is_empty()) return false;

    GlyphDef glyph;
    glyph.name = name.utf8().get_data();
    glyph.slot_type = static_cast<GlyphSlotType>(
        static_cast<int>(def.get("slot_type", 0)));
    glyph.element = static_cast<RuneElement>(
        static_cast<int>(def.get("element", 0)));
    glyph.tier = static_cast<RuneTier>(
        static_cast<int>(def.get("tier", 0)));
    glyph.potency = static_cast<int>(def.get("potency", 1));
    glyph.form = static_cast<SpellForm>(
        static_cast<int>(def.get("form", 0)));

    return GlyphRegistry::register_glyph(glyph) != kInvalidGlyphId;
}

godot::Dictionary GDGlyphRegistry::def_to_dict(const GlyphDef* def) {
    godot::Dictionary dict;
    if (def == nullptr) return dict;

    dict["name"] = def->name;
    dict["slot_type"] = static_cast<int>(def->slot_type);
    dict["element"] = static_cast<int>(def->element);
    dict["tier"] = static_cast<int>(def->tier);
    dict["potency"] = def->potency;
    dict["form"] = static_cast<int>(def->form);
    return dict;
}

godot::Dictionary GDGlyphRegistry::get_glyph_by_name(const godot::String& name) const {
    return def_to_dict(GlyphRegistry::get_by_name(name.utf8().get_data()));
}

godot::Dictionary GDGlyphRegistry::get_effect_glyph(int element, int tier) const {
    return def_to_dict(GlyphRegistry::get_effect_glyph(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier)));
}

godot::Dictionary GDGlyphRegistry::get_augment_glyph(int element, int tier) const {
    return def_to_dict(GlyphRegistry::get_augment_glyph(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier)));
}

godot::Dictionary GDGlyphRegistry::get_form_glyph(int form) const {
    return def_to_dict(GlyphRegistry::get_form_glyph(
        static_cast<SpellForm>(form)));
}

int GDGlyphRegistry::get_glyph_count() const {
    return static_cast<int>(GlyphRegistry::count());
}

godot::PackedStringArray GDGlyphRegistry::get_all_glyph_names() const {
    godot::PackedStringArray arr;
    const RuneElement elements[] = {
        RuneElement::FIRE, RuneElement::WATER, RuneElement::EARTH,
        RuneElement::AIR, RuneElement::LIGHT, RuneElement::DARK,
        RuneElement::ORDER, RuneElement::CHAOS
    };
    const RuneTier tiers[] = {
        RuneTier::COMMON, RuneTier::REFINED,
        RuneTier::SUPERIOR, RuneTier::LEGENDARY
    };

    for (int f = 0; f < static_cast<int>(SpellForm::COUNT); ++f) {
        auto form = static_cast<SpellForm>(f);
        const GlyphDef* glyph = GlyphRegistry::get_form_glyph(form);
        if (glyph) arr.append(glyph->name);
    }

    for (auto element : elements) {
        for (auto tier : tiers) {
            const GlyphDef* effect = GlyphRegistry::get_effect_glyph(element, tier);
            if (effect) arr.append(effect->name);
            const GlyphDef* augment = GlyphRegistry::get_augment_glyph(element, tier);
            if (augment) arr.append(augment->name);
        }
    }
    return arr;
}

godot::PackedStringArray GDGlyphRegistry::get_form_glyph_names() const {
    godot::PackedStringArray arr;
    for (int f = 0; f < static_cast<int>(SpellForm::COUNT); ++f) {
        auto form = static_cast<SpellForm>(f);
        const GlyphDef* glyph = GlyphRegistry::get_form_glyph(form);
        if (glyph) arr.append(glyph->name);
    }
    return arr;
}

} // namespace science_and_theology
