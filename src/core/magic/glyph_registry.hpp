#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rune_registry.hpp"
#include "glyph_def.hpp"

namespace science_and_theology::magic {

using GlyphId = uint8_t;
inline constexpr GlyphId kInvalidGlyphId = 0xFF;

class GlyphRegistry {
public:
    static void initialize();
    // 完全清空 registry（用于热重载），不预留 ID 0。
    static void reset();

    // Register a glyph from GDScript. Stores the name string persistently
    // and updates the appropriate index (form/effect/augment) based on
    // slot_type. Requires explicit_id (不再支持自动分配).
    // Returns the assigned GlyphId, or kInvalidGlyphId on failure.
    static GlyphId register_glyph(const GlyphDef& def, GlyphId explicit_id);

    static const GlyphDef* get_by_id(GlyphId id);
    static const GlyphDef* get_by_name(const char* name);
    static const GlyphDef* get_effect_glyph(RuneElement element, RuneTier tier);
    static const GlyphDef* get_augment_glyph(RuneElement element, RuneTier tier);
    static const GlyphDef* get_form_glyph(SpellForm form);

    static GlyphId get_id(const char* name);
    static size_t count();

private:
    static void register_builtin_glyphs();
    static void register_form_glyphs();
    static void register_effect_augment_glyphs();
};

} // namespace science_and_theology::magic
