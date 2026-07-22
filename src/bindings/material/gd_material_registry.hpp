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

    // Finalize the registry — locks materials and triggers ItemRegistry init.
    static void finalize();

protected:
    static void _bind_methods();
};

} // namespace godot
