#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

class GDRuneRegistry : public godot::Resource {
    GDCLASS(GDRuneRegistry, godot::Resource)

public:
    GDRuneRegistry() = default;

    // Register a rune from GDScript.
    // Dictionary fields:
    //   name (String, required): stable rune key, e.g. "fire_rune_common".
    //   element (int, required): RuneElement enum (0=Fire … 7=Chaos).
    //   tier (int, required): RuneTier enum (0=Common … 3=Legendary).
    //   potency (int, optional): defaults to 1.
    // Returns true on success.
    static bool register_rune(const godot::Dictionary& def);

    godot::Dictionary get_rune_by_name(const godot::String& name) const;
    godot::Dictionary get_rune(int element, int tier) const;
    int get_rune_count() const;
    godot::PackedStringArray get_all_rune_names() const;

    static int element_count() { return 8; }
    static int tier_count() { return 4; }

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
