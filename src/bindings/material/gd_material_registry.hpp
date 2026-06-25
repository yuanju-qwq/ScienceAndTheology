#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>

namespace godot {

class GDMaterialRegistry : public RefCounted {
    GDCLASS(GDMaterialRegistry, RefCounted)

public:
    // Register a single material from a GDScript Dictionary.
    // Dictionary keys: id, name, title_key, gen_flags, state, color,
    //   melting_point, boiling_point, mass, chemical_formula,
    //   elements (Array of {element: String, count: int})
    static bool register_material(const Dictionary& def);

    // Register a compound/mineral as a lightweight mod item.
    // Compounds are NOT C++ materials — they are simple items with a key
    // and title, registered dynamically. No gen_flags, no formula needed.
    // Call BEFORE finalize() so items exist when ItemRegistry initializes.
    static bool register_compound(const String& item_key, const String& title_key);

    // Finalize the registry — locks materials and triggers ItemRegistry init.
    static void finalize();

protected:
    static void _bind_methods();
};

} // namespace godot