#include "gd_glyph_conversion.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "core/magic/glyph_conversion.hpp"
#include "core/magic/glyph_registry.hpp"

using namespace godot;

namespace science_and_theology {

using namespace magic;

void GDGlyphConversion::_bind_methods() {
    ClassDB::bind_method(D_METHOD("can_convert", "element", "tier", "output_type"),
                         &GDGlyphConversion::can_convert);
    ClassDB::bind_method(D_METHOD("get_result", "element", "tier", "output_type"),
                         &GDGlyphConversion::get_result);
    ClassDB::bind_method(D_METHOD("get_rune_cost", "element", "tier", "output_type"),
                         &GDGlyphConversion::get_rune_cost);
    ClassDB::bind_method(D_METHOD("get_recipes"),
                         &GDGlyphConversion::get_recipes);
    ClassDB::bind_method(D_METHOD("is_basic_subset", "element", "tier"),
                         &GDGlyphConversion::is_basic_subset);
}

bool GDGlyphConversion::can_convert(int element, int tier, int output_type) const {
    return GlyphConversion::can_convert(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier),
        static_cast<GlyphSlotType>(output_type));
}

Dictionary GDGlyphConversion::get_result(int element, int tier, int output_type) const {
    Dictionary dict;
    const GlyphDef* glyph = GlyphConversion::get_result(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier),
        static_cast<GlyphSlotType>(output_type));
    if (glyph == nullptr) return dict;

    dict["name"] = glyph->name;
    dict["slot_type"] = static_cast<int>(glyph->slot_type);
    dict["element"] = static_cast<int>(glyph->element);
    dict["tier"] = static_cast<int>(glyph->tier);
    dict["potency"] = glyph->potency;
    dict["form"] = static_cast<int>(glyph->form);
    return dict;
}

int GDGlyphConversion::get_rune_cost(int element, int tier, int output_type) const {
    return GlyphConversion::get_rune_cost(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier),
        static_cast<GlyphSlotType>(output_type));
}

Array GDGlyphConversion::get_recipes() const {
    Array arr;
    for (const auto& recipe : GlyphConversion::get_recipes()) {
        Dictionary d;
        d["element"] = static_cast<int>(recipe.element);
        d["tier"] = static_cast<int>(recipe.tier);
        d["output_type"] = static_cast<int>(recipe.output_type);
        d["rune_cost"] = recipe.rune_cost;
        arr.append(d);
    }
    return arr;
}

bool GDGlyphConversion::is_basic_subset(int element, int tier) const {
    return GlyphConversion::is_basic_subset(
        static_cast<RuneElement>(element),
        static_cast<RuneTier>(tier));
}

} // namespace science_and_theology
