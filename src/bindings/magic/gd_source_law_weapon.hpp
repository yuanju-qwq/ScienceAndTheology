#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "core/magic/source_law_weapon_def.hpp"
#include "core/magic/spell_compiler.hpp"
#include "core/magic/spell_caster.hpp"
#include "core/magic/mana_pool.hpp"

namespace science_and_theology {

class GDSourceLawWeapon : public godot::Resource {
    GDCLASS(GDSourceLawWeapon, godot::Resource)

public:
    enum WeaponTierConst {
        TIER_BASIC_GUIDE_TOOL = 0,
        TIER_ARM_GUARD        = 1,
        TIER_AETHER_WEAPON    = 2,
    };

    GDSourceLawWeapon();

    void configure(int tier_level);

    int get_tier() const;
    int get_total_glyph_slots() const;
    int get_max_augment_slots() const;
    int get_max_presets() const;
    float get_mana_multiplier() const;

    // Preset management (delegates to internal SpellBook)
    int get_active_preset() const;
    bool set_active_preset(int index);

    // Glyph slotting
    bool set_form(int preset_index, const godot::String& glyph_name);
    bool set_effect(int preset_index, const godot::String& glyph_name);
    bool add_augment(int preset_index, const godot::String& glyph_name);
    void clear_preset(int preset_index);

    // Spell compilation
    godot::Dictionary compile_spell(int preset_index = -1) const;
    bool can_cast(const godot::Dictionary& mana_dict, int64_t last_cast_tick) const;

    // Query
    godot::Dictionary get_preset_info(int preset_index) const;

protected:
    static void _bind_methods();

private:
    magic::SourceLawWeaponDef weapon_def_;
    magic::SpellBook book_;
    magic::SpellCompiler compiler_;
    magic::SpellCaster caster_;

    static godot::Dictionary spell_to_dict(const magic::SpellDef& spell);
};

} // namespace science_and_theology
