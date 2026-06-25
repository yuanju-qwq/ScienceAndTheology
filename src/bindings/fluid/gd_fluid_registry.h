#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDFluidRegistry — GDScript binding for gt::FluidRegistry
// ============================================================
//
// Allows content packs and the builtin content database to register
// new fluid types at load time. Registration is global and must
// happen before world load.
//
// GDScript usage:
//   GDFluidRegistry.register_fluid({
//       "name": "lubricant",
//       "title_key": "fluid.lubricant",
//       "chemical_formula": "C16H32O2",
//       "temperature": 290,
//       "is_gas": false,
//   })
//   var id = GDFluidRegistry.get_fluid_id("lubricant")
class GDFluidRegistry : public godot::Object {
    GDCLASS(GDFluidRegistry, godot::Object)

public:
    GDFluidRegistry() = default;
    ~GDFluidRegistry() override = default;

    // Register a new fluid. Returns the assigned FluidId, or -1 on failure.
    // Dictionary fields:
    //   name (String, required): stable fluid key, e.g. "lubricant".
    //   title_key (String, optional): localization key, defaults to "fluid.<name>".
    //   chemical_formula (String, optional): display-only formula.
    //   temperature (int, optional): default 300 K.
    //   is_gas (bool, optional): default false.
    static int64_t register_fluid(const godot::Dictionary& def);

    // Look up a fluid by name. Returns FluidId or -1 if not found.
    static int64_t get_fluid_id(const godot::String& name);

    // Look up a fluid by id. Returns a Dictionary with the fluid's fields,
    // or an empty Dictionary if not found.
    static godot::Dictionary get_fluid(int64_t id);

    // Returns the total number of registered fluids.
    static int64_t get_fluid_count();

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
