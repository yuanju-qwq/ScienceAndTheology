#include "glyph_conversion.hpp"

namespace science_and_theology::magic {

std::vector<GlyphConversionRecipe> GlyphConversion::recipes_;

void GlyphConversion::initialize() {
    recipes_.clear();
    register_recipes();
}

bool GlyphConversion::can_convert(RuneElement element, RuneTier tier,
                                   GlyphSlotType output_type) {
    if (output_type == GlyphSlotType::FORM) return false;
    if (!is_basic_subset(element, tier)) return false;

    for (const auto& recipe : recipes_) {
        if (recipe.element == element
            && recipe.tier == tier
            && recipe.output_type == output_type) {
            return true;
        }
    }
    return false;
}

const GlyphDef* GlyphConversion::get_result(RuneElement element, RuneTier tier,
                                              GlyphSlotType output_type) {
    if (!can_convert(element, tier, output_type)) return nullptr;

    if (output_type == GlyphSlotType::EFFECT) {
        return GlyphRegistry::get_effect_glyph(element, tier);
    }
    if (output_type == GlyphSlotType::AUGMENT) {
        return GlyphRegistry::get_augment_glyph(element, tier);
    }
    return nullptr;
}

int GlyphConversion::get_rune_cost(RuneElement element, RuneTier tier,
                                    GlyphSlotType output_type) {
    for (const auto& recipe : recipes_) {
        if (recipe.element == element
            && recipe.tier == tier
            && recipe.output_type == output_type) {
            return recipe.rune_cost;
        }
    }
    return 0;
}

const std::vector<GlyphConversionRecipe>& GlyphConversion::get_recipes() {
    return recipes_;
}

bool GlyphConversion::is_basic_subset(RuneElement element, RuneTier tier) {
    // V0.3 basic subset: fire, water, earth + common, refined
    if (element != RuneElement::FIRE
        && element != RuneElement::WATER
        && element != RuneElement::EARTH) {
        return false;
    }
    if (tier != RuneTier::COMMON && tier != RuneTier::REFINED) {
        return false;
    }
    return true;
}

void GlyphConversion::register_recipes() {
    // V0.3: fire, water, earth × common, refined × effect, augment
    const RuneElement basic_elements[] = {
        RuneElement::FIRE,
        RuneElement::WATER,
        RuneElement::EARTH,
    };
    const RuneTier basic_tiers[] = {
        RuneTier::COMMON,
        RuneTier::REFINED,
    };

    for (auto element : basic_elements) {
        for (auto tier : basic_tiers) {
            // Effect glyph: 1 rune cost
            GlyphConversionRecipe effect_recipe;
            effect_recipe.element = element;
            effect_recipe.tier = tier;
            effect_recipe.output_type = GlyphSlotType::EFFECT;
            effect_recipe.rune_cost = 1;
            recipes_.push_back(effect_recipe);

            // Augment glyph: 1 rune cost
            GlyphConversionRecipe augment_recipe;
            augment_recipe.element = element;
            augment_recipe.tier = tier;
            augment_recipe.output_type = GlyphSlotType::AUGMENT;
            augment_recipe.rune_cost = 1;
            recipes_.push_back(augment_recipe);
        }
    }
}

} // namespace science_and_theology::magic
