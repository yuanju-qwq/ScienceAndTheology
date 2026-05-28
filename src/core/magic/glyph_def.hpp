#pragma once

#include <cstdint>

#include "rune_def.hpp"

namespace science_and_theology::magic {

enum class GlyphSlotType : uint8_t {
    FORM = 0,
    EFFECT,
    AUGMENT,
    COUNT
};

enum class SpellForm : uint8_t {
    PROJECTILE = 0,
    SELF,
    AREA,
    BEAM,
    TOUCH,
    COUNT
};

struct GlyphDef {
    const char* name = "";
    GlyphSlotType slot_type = GlyphSlotType::EFFECT;
    RuneElement element = RuneElement::FIRE;
    RuneTier tier = RuneTier::COMMON;
    int potency = 1;
    SpellForm form = SpellForm::PROJECTILE;   // only valid for FORM glyphs
};

inline const char* glyph_slot_type_name(GlyphSlotType type) {
    switch (type) {
        case GlyphSlotType::FORM:    return "form";
        case GlyphSlotType::EFFECT:  return "effect";
        case GlyphSlotType::AUGMENT: return "augment";
        default:                     return "unknown";
    }
}

inline const char* spell_form_name(SpellForm form) {
    switch (form) {
        case SpellForm::PROJECTILE: return "projectile";
        case SpellForm::SELF:       return "self";
        case SpellForm::AREA:       return "area";
        case SpellForm::BEAM:       return "beam";
        case SpellForm::TOUCH:      return "touch";
        default:                    return "unknown";
    }
}

} // namespace science_and_theology::magic
