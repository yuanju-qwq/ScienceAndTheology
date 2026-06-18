#pragma once

#include <cstdint>

#include "spell_book.hpp"

namespace science_and_theology::magic {

// ============================================================
// SourceLawWeaponTier — tier of a source law weapon
// ============================================================
//
// Maps to design doc section 10.5:
//   BASIC_GUIDE_TOOL  = 基础源律导具 (3 glyph slots, 2 presets, 1.0x mana)
//   ARM_GUARD         = 源律臂铠     (4 glyph slots, 3 presets, 0.85x mana)
//   AETHER_WEAPON     = 以太术式武装 (5 glyph slots, 4 presets, 0.7x mana)

enum class SourceLawWeaponTier : uint8_t {
    BASIC_GUIDE_TOOL = 0,
    ARM_GUARD,
    AETHER_WEAPON,
    COUNT
};

inline const char* source_law_weapon_tier_name(SourceLawWeaponTier tier) {
    switch (tier) {
        case SourceLawWeaponTier::BASIC_GUIDE_TOOL: return "basic_guide_tool";
        case SourceLawWeaponTier::ARM_GUARD:        return "arm_guard";
        case SourceLawWeaponTier::AETHER_WEAPON:    return "aether_weapon";
        default:                                   return "unknown";
    }
}

// ============================================================
// SourceLawWeaponDef — definition of a source law weapon
// ============================================================
//
// A source law weapon is the equipment that goes into the ARM slot.
// It wraps a SpellBook and adds tier-specific configuration.
// The SpellBook handles preset/glyph management; this struct
// provides the bridge between the equipment system and the spell system.

struct SourceLawWeaponDef {
    const char* id = "";
    SourceLawWeaponTier weapon_tier = SourceLawWeaponTier::BASIC_GUIDE_TOOL;

    // Total glyph slots available (form + effect + augments).
    int total_glyph_slots() const {
        switch (weapon_tier) {
            case SourceLawWeaponTier::BASIC_GUIDE_TOOL: return 3;
            case SourceLawWeaponTier::ARM_GUARD:        return 4;
            case SourceLawWeaponTier::AETHER_WEAPON:    return 5;
            default:                                   return 3;
        }
    }

    // Max augment slots = total - form(1) - effect(1).
    int max_augment_slots() const {
        return total_glyph_slots() - 2;
    }

    // Max number of spell presets.
    int max_presets() const {
        switch (weapon_tier) {
            case SourceLawWeaponTier::BASIC_GUIDE_TOOL: return 2;
            case SourceLawWeaponTier::ARM_GUARD:        return 3;
            case SourceLawWeaponTier::AETHER_WEAPON:    return 4;
            default:                                   return 2;
        }
    }

    // Mana cost multiplier (lower = more efficient).
    float mana_multiplier() const {
        switch (weapon_tier) {
            case SourceLawWeaponTier::BASIC_GUIDE_TOOL: return 1.0f;
            case SourceLawWeaponTier::ARM_GUARD:        return 0.85f;
            case SourceLawWeaponTier::AETHER_WEAPON:    return 0.7f;
            default:                                   return 1.0f;
        }
    }

    // Convert to SpellBookTier for SpellBook compatibility.
    SpellBookTier to_spell_book_tier() const {
        switch (weapon_tier) {
            case SourceLawWeaponTier::BASIC_GUIDE_TOOL: return SpellBookTier::BASIC;
            case SourceLawWeaponTier::ARM_GUARD:        return SpellBookTier::ADVANCED;
            case SourceLawWeaponTier::AETHER_WEAPON:    return SpellBookTier::MASTER;
            default:                                   return SpellBookTier::BASIC;
        }
    }

    // Create a SpellBook configured for this weapon tier.
    SpellBook create_spell_book() const {
        SpellBook book;
        book.id = id;
        book.tier = to_spell_book_tier();
        book.active_preset = 0;
        return book;
    }
};

} // namespace science_and_theology::magic
