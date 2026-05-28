#pragma once

#include "glyph_def.hpp"
#include "spell_book.hpp"

namespace science_and_theology::magic {

class SpellCompiler {
public:
    static SpellDef compile(const SpellPreset& preset);

private:
    static float get_augment_damage_mult(RuneElement element, RuneTier tier);
    static int get_augment_range_bonus(RuneElement element, RuneTier tier);
    static float get_augment_cast_speed(RuneElement element, RuneTier tier);
    static int get_augment_mana_reduction(RuneElement element, RuneTier tier);
    static bool get_augment_homing(RuneElement element, RuneTier tier);
    static float get_augment_pierce(RuneElement element, RuneTier tier);
    static float get_augment_chaos(RuneElement element, RuneTier tier);
};

} // namespace science_and_theology::magic
