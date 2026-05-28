#pragma once

#include <cstdint>

#include "../spell_book.hpp"

namespace science_and_theology::magic {

struct SpellEffectResult {
    bool hit = false;
    float damage = 0.0f;
    int tiles_broken = 0;
    float heal_amount = 0.0f;
    int light_radius = 0;
};

// Polymorphic base for spell effect implementations.
// The core library defines the data types; effect logic is
// instantiated by the binding layer or simulation system.

class SpellEffectBase {
public:
    virtual ~SpellEffectBase() = default;

    SpellEffectResult execute(const SpellDef& spell,
                               float caster_x, float caster_y,
                               float target_x, float target_y);
};

} // namespace science_and_theology::magic
