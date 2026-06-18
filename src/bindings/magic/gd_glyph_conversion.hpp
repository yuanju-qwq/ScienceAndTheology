#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>

namespace science_and_theology {

class GDGlyphConversion : public godot::Resource {
    GDCLASS(GDGlyphConversion, godot::Resource)

public:
    GDGlyphConversion() = default;

    bool can_convert(int element, int tier, int output_type) const;
    godot::Dictionary get_result(int element, int tier, int output_type) const;
    int get_rune_cost(int element, int tier, int output_type) const;
    godot::Array get_recipes() const;
    bool is_basic_subset(int element, int tier) const;

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
