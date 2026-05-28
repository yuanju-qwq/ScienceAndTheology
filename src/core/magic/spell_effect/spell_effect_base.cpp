#include "spell_effect_base.hpp"

namespace science_and_theology::magic {

SpellEffectResult SpellEffectBase::execute(const SpellDef& spell,
                                            float /*caster_x*/, float /*caster_y*/,
                                            float /*target_x*/, float /*target_y*/) {
    SpellEffectResult result;
    result.damage = spell.damage_mult * static_cast<float>(spell.potency);

    switch (spell.form) {
        case SpellForm::PROJECTILE:
            result.hit = true;
            break;
        case SpellForm::SELF:
            result.hit = true;
            if (spell.element == RuneElement::WATER) {
                result.heal_amount = 5.0f * static_cast<float>(spell.potency);
            }
            break;
        case SpellForm::AREA:
            result.hit = true;
            break;
        case SpellForm::BEAM:
            result.hit = true;
            break;
        case SpellForm::TOUCH:
            result.hit = true;
            break;
        default:
            break;
    }

    if (spell.element == RuneElement::EARTH) {
        result.tiles_broken = spell.potency;
    }
    if (spell.element == RuneElement::LIGHT) {
        result.light_radius = 3 * spell.potency;
    }

    return result;
}

} // namespace science_and_theology::magic
