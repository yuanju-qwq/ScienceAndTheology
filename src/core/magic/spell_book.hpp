#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "glyph_def.hpp"

namespace science_and_theology::magic {

enum class SpellBookTier : uint8_t {
    BASIC = 0,
    ADVANCED,
    MASTER,
    COUNT
};

struct SpellPreset {
    std::optional<GlyphDef> form;
    std::optional<GlyphDef> effect;
    std::vector<GlyphDef> augments;

    bool can_cast() const {
        return form.has_value() && effect.has_value();
    }
};

struct SpellDef {
    SpellForm form = SpellForm::PROJECTILE;
    RuneElement element = RuneElement::FIRE;
    int potency = 1;
    float damage_mult = 1.0f;
    int range = 1;
    float cast_time_sec = 0.5f;
    int mana_cost = 10;
    int cooldown_ticks = 20;    // default 1 second at 20TPS
    bool homing = false;
    float pierce_ratio = 0.0f;
    float chaos_factor = 1.0f;
};

struct SpellBook {
    const char* id = "";
    SpellBookTier tier = SpellBookTier::BASIC;

    int active_preset = 0;
    std::array<SpellPreset, 4> presets;

    int max_presets() const {
        switch (tier) {
            case SpellBookTier::BASIC:    return 2;
            case SpellBookTier::ADVANCED: return 3;
            case SpellBookTier::MASTER:   return 4;
            default:                      return 2;
        }
    }

    int max_augments() const {
        switch (tier) {
            case SpellBookTier::BASIC:    return 1;
            case SpellBookTier::ADVANCED: return 2;
            case SpellBookTier::MASTER:   return 3;
            default:                      return 1;
        }
    }

    float mana_multiplier() const {
        switch (tier) {
            case SpellBookTier::BASIC:    return 1.0f;
            case SpellBookTier::ADVANCED: return 0.85f;
            case SpellBookTier::MASTER:   return 0.7f;
            default:                      return 1.0f;
        }
    }

    const SpellPreset& current_preset() const {
        return presets[active_preset];
    }

    bool switch_preset(int index) {
        if (index < 0 || index >= max_presets()) return false;
        active_preset = index;
        return true;
    }
};

} // namespace science_and_theology::magic
