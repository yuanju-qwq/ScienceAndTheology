#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

// ============================================================
// GDMachineDefinitionRegistry — GDScript binding for machine defs
// ============================================================
//
// Allows content packs to register new machine types with metadata
// (display name, tier, GUI scene, slots, power). The actual machine
// processing logic (recipe matching, input/output) is handled by a
// GDScript callback registered separately via ModRegistrar.
//
// GDScript usage:
//   GDMachineDefinitionRegistry.register_definition({
//       "type_key": "my_mod:alloy_smelter",
//       "display_name": "Alloy Smelter",
//       "gui_scene_path": "res://mods/my_mod/ui/alloy_smelter.tscn",
//       "tier": 2,
//       "input_slots": 2,
//       "output_slots": 1,
//       "power_capacity": 32000,
//   })
class GDMachineDefinitionRegistry : public godot::Object {
    GDCLASS(GDMachineDefinitionRegistry, godot::Object)

public:
    GDMachineDefinitionRegistry() = default;
    ~GDMachineDefinitionRegistry() override = default;

    // Register a machine definition. Returns true on success.
    // Dictionary fields:
    //   type_key (String, required): globally unique machine type key.
    //   display_name (String, optional): human-readable name.
    //   gui_scene_path (String, optional): path to GUI scene.
    //   model_path (String, optional): path to 3D model.
    //   tier (int, optional, default 1): machine tier.
    //   input_slots (int, optional, default 1).
    //   output_slots (int, optional, default 1).
    //   power_capacity (int, optional, default 0): max EU storage.
    //   panel_layout (Dictionary, optional): data-driven GUI layout. Fields:
    //     panel_width (float), panel_height (float),
    //     elements (Array[Dictionary]) where each element has:
    //       element_id (String), element_type (String),
    //       role (String), slot_index (int),
    //       rect (Array[float]): [left, top, right, bottom] offsets.
    static bool register_definition(const godot::Dictionary& def);

    // Returns true if a machine type is registered.
    static bool has_definition(const godot::String& type_key);

    // Look up a machine definition. Returns a Dictionary or empty if not found.
    static godot::Dictionary get_definition(const godot::String& type_key);

    // Returns the total number of registered machine definitions.
    static int64_t get_definition_count();

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
