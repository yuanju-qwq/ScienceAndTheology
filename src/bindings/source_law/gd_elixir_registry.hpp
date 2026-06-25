#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace science_and_theology {

// ============================================================
// GDElixirRegistry — GDExtension binding for ElixirRegistry
// ============================================================
//
// Allows GDScript to register elixir recipes into the C++
// ElixirRegistry. Built-in recipes are registered from
// scripts/content/BuiltinElixirs.gd.
class GDElixirRegistry : public godot::Resource {
    GDCLASS(GDElixirRegistry, godot::Resource)

public:
    GDElixirRegistry() = default;

    // Register an elixir recipe from GDScript.
    // Dictionary fields:
    //   id (String, required): stable elixir key, e.g. "elixir_sand_armor_initiation".
    //   title_key (String, optional): display title.
    //   type (int, required): ElixirType
    //       (0=INITIATION, 1=ENHANCEMENT, 2=PROMOTION, 3=TUNING, 4=PURIFICATION).
    //   target_path (int, optional): SublimationPath, default 0 (NONE)
    //       (0=NONE, 1=SAND_ARMOR, 2=TIDAL, 3=STORM, 4=FURNACE, 5=RADIANCE).
    //   target_slot (int, optional): OrganSlot, default 7 (COUNT = any)
    //       (0=HEART, 1=BONE, 2=BLOOD, 3=LUNG, 4=EYE, 5=NERVE, 6=SKIN, 7=COUNT).
    //   primary_element (int, optional): RuneElement, default 2 (EARTH).
    //   source_cost (int, optional): default 0.
    //   stability_modifier (float, optional): default 0.0.
    //   mutation_modifier (float, optional): default 0.0.
    //   tuning_degree (int, optional): default 0.
    //   required_rune_elements (PackedByteArray or Array of int, optional):
    //       array of RuneElement values.
    // Returns true on success.
    static bool register_recipe(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
