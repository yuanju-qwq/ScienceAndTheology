#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/magic/glyph_registry.hpp"

namespace science_and_theology {

class GDGlyphRegistry : public godot::Resource {
    GDCLASS(GDGlyphRegistry, godot::Resource)

public:
    GDGlyphRegistry() = default;

    godot::Dictionary get_glyph_by_name(const godot::String& name) const;
    godot::Dictionary get_effect_glyph(int element, int tier) const;
    godot::Dictionary get_augment_glyph(int element, int tier) const;
    godot::Dictionary get_form_glyph(int form) const;
    int get_glyph_count() const;
    godot::PackedStringArray get_all_glyph_names() const;
    godot::PackedStringArray get_form_glyph_names() const;

protected:
    static void _bind_methods();

private:
    static godot::Dictionary def_to_dict(const magic::GlyphDef* def);
};

} // namespace science_and_theology
