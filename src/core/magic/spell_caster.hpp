#pragma once

#include <cstdint>
#include <string>

#include "mana_pool.hpp"
#include "spell_book.hpp"

namespace science_and_theology::magic {

class SpellCaster {
public:
    bool can_cast(const SpellDef& spell, const ManaPool& mana,
                  uint64_t current_tick, uint64_t last_cast_tick) const;

    int get_cast_cost(const SpellDef& spell) const;
};

inline bool SpellCaster::can_cast(const SpellDef& spell, const ManaPool& mana,
                                   uint64_t current_tick, uint64_t last_cast_tick) const {
    if (mana.current_mana < spell.mana_cost) return false;
    if (current_tick - last_cast_tick < static_cast<uint64_t>(spell.cooldown_ticks)) return false;
    return true;
}

inline int SpellCaster::get_cast_cost(const SpellDef& spell) const {
    return spell.mana_cost;
}

} // namespace science_and_theology::magic
