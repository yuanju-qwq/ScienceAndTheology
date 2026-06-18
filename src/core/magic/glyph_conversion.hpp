#pragma once

#include <cstdint>
#include <vector>

#include "glyph_def.hpp"
#include "rune_def.hpp"
#include "rune_registry.hpp"
#include "glyph_registry.hpp"

namespace science_and_theology::magic {

// ============================================================
// GlyphConversion — converts runes into glyphs (魔符转换)
// ============================================================
//
// Design doc (section 10.1):
//   Rune = stable source law fragment (源律片段)
//   Glyph = spell component consumable by source law weapon (魔符)
//
// Conversion rules:
//   - A rune can be converted into an effect glyph or an augment glyph.
//   - The element and tier of the rune determine the resulting glyph.
//   - Form glyphs are NOT produced from runes (they are fixed types).
//
// V0.3 scope: only fire/water/earth + common/refined are available
// for conversion. Other elements/tiers are reserved for later versions.

struct GlyphConversionRecipe {
    RuneElement element;
    RuneTier tier;
    GlyphSlotType output_type;  // EFFECT or AUGMENT
    // Number of runes consumed per conversion (default 1).
    int rune_cost = 1;
};

class GlyphConversion {
public:
    // Initialize the conversion table. Called once at startup.
    static void initialize();

    // Check if a rune can be converted to the given glyph type.
    static bool can_convert(RuneElement element, RuneTier tier,
                            GlyphSlotType output_type);

    // Get the glyph produced by converting a rune.
    // Returns nullptr if conversion is not available.
    static const GlyphDef* get_result(RuneElement element, RuneTier tier,
                                       GlyphSlotType output_type);

    // Get the rune cost for a conversion.
    static int get_rune_cost(RuneElement element, RuneTier tier,
                             GlyphSlotType output_type);

    // Get all available conversion recipes.
    static const std::vector<GlyphConversionRecipe>& get_recipes();

    // Check if a rune element/tier is in the V0.3 basic subset.
    static bool is_basic_subset(RuneElement element, RuneTier tier);

private:
    static void register_recipes();

    static std::vector<GlyphConversionRecipe> recipes_;
};

} // namespace science_and_theology::magic
