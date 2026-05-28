#include "spell_compiler.hpp"

namespace science_and_theology::magic {

SpellDef SpellCompiler::compile(const SpellPreset& preset) {
    SpellDef def;

    if (!preset.can_cast()) return def;

    def.form = preset.form->form;
    def.element = preset.effect->element;
    def.potency = preset.effect->potency;
    def.damage_mult = 1.0f;
    def.range = 1;
    def.cast_time_sec = 0.5f;
    def.mana_cost = 10;
    def.cooldown_ticks = 20;
    def.homing = false;
    def.pierce_ratio = 0.0f;
    def.chaos_factor = 1.0f;

    // Base values by effect element.
    switch (def.element) {
        case RuneElement::FIRE:
            def.damage_mult = 1.0f;
            def.mana_cost = 12;
            break;
        case RuneElement::WATER:
            def.damage_mult = 0.0f;
            def.mana_cost = 15;
            break;
        case RuneElement::EARTH:
            def.damage_mult = 0.5f;
            def.range = 2;
            def.mana_cost = 20;
            break;
        case RuneElement::AIR:
            def.damage_mult = 0.0f;
            def.mana_cost = 10;
            break;
        case RuneElement::LIGHT:
            def.damage_mult = 0.3f;
            def.mana_cost = 8;
            break;
        case RuneElement::DARK:
            def.damage_mult = 1.2f;
            def.pierce_ratio = 0.1f;
            def.mana_cost = 18;
            break;
        case RuneElement::ORDER:
            def.damage_mult = 0.0f;
            def.mana_cost = 12;
            break;
        case RuneElement::CHAOS:
            def.damage_mult = 1.0f;
            def.chaos_factor = 1.5f;
            def.mana_cost = 15;
            break;
        default:
            break;
    }

    // Scale by potency.
    def.damage_mult *= static_cast<float>(def.potency);
    def.mana_cost = static_cast<int>(def.mana_cost * (1.0f + 0.3f * (def.potency - 1)));

    // Apply augment effects.
    for (const auto& aug : preset.augments) {
        def.damage_mult *= get_augment_damage_mult(aug.element, aug.tier);
        def.range += get_augment_range_bonus(aug.element, aug.tier);
        def.cast_time_sec *= get_augment_cast_speed(aug.element, aug.tier);
        def.mana_cost = def.mana_cost - get_augment_mana_reduction(aug.element, aug.tier);
        if (get_augment_homing(aug.element, aug.tier)) def.homing = true;
        def.pierce_ratio += get_augment_pierce(aug.element, aug.tier);
        def.chaos_factor *= get_augment_chaos(aug.element, aug.tier);
    }

    // Form-specific adjustments.
    switch (def.form) {
        case SpellForm::PROJECTILE:
            def.range = (def.range > 0 ? def.range : 1) + 3;
            break;
        case SpellForm::SELF:
            def.range = 0;
            def.mana_cost = static_cast<int>(def.mana_cost * 0.8f);
            break;
        case SpellForm::AREA:
            def.range = (def.range > 0 ? def.range : 1) + 1;
            def.mana_cost = static_cast<int>(def.mana_cost * 1.5f);
            break;
        case SpellForm::BEAM:
            def.range = 5;
            def.mana_cost = static_cast<int>(def.mana_cost * 1.3f);
            break;
        case SpellForm::TOUCH:
            def.range = 1;
            def.mana_cost = static_cast<int>(def.mana_cost * 0.6f);
            break;
        default:
            break;
    }

    // Clamp mana cost.
    if (def.mana_cost < 1) def.mana_cost = 1;

    return def;
}

float SpellCompiler::get_augment_damage_mult(RuneElement element, RuneTier tier) {
    if (element == RuneElement::FIRE) {
        return 1.0f + 0.25f * static_cast<float>(rune_tier_potency(tier));
    }
    if (element == RuneElement::DARK) {
        return 1.0f + 0.1f * static_cast<float>(rune_tier_potency(tier));
    }
    return 1.0f;
}

int SpellCompiler::get_augment_range_bonus(RuneElement element, RuneTier tier) {
    if (element == RuneElement::EARTH) {
        return rune_tier_potency(tier);
    }
    return 0;
}

float SpellCompiler::get_augment_cast_speed(RuneElement element, RuneTier tier) {
    if (element == RuneElement::AIR) {
        return 1.0f - 0.15f * static_cast<float>(rune_tier_potency(tier));
    }
    return 1.0f;
}

int SpellCompiler::get_augment_mana_reduction(RuneElement element, RuneTier tier) {
    if (element == RuneElement::ORDER) {
        return rune_tier_potency(tier) * 2;
    }
    return 0;
}

bool SpellCompiler::get_augment_homing(RuneElement element, RuneTier tier) {
    return element == RuneElement::LIGHT;
}

float SpellCompiler::get_augment_pierce(RuneElement element, RuneTier tier) {
    if (element == RuneElement::DARK) {
        return 0.15f * static_cast<float>(rune_tier_potency(tier));
    }
    return 0.0f;
}

float SpellCompiler::get_augment_chaos(RuneElement element, RuneTier tier) {
    if (element == RuneElement::CHAOS) {
        return 1.0f + 0.1f * static_cast<float>(rune_tier_potency(tier));
    }
    return 1.0f;
}

} // namespace science_and_theology::magic
