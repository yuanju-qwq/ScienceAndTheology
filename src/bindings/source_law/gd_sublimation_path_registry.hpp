#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace science_and_theology {

// ============================================================
// GDSublimationPathRegistry — GDExtension binding for
// SublimationPathRegistry. Allows GDScript to register
// sublimation paths and organ skills.
// ============================================================
class GDSublimationPathRegistry : public godot::Resource {
    GDCLASS(GDSublimationPathRegistry, godot::Resource)

public:
    GDSublimationPathRegistry() = default;

    // Register a sublimation path from GDScript.
    // Dictionary fields:
    //   path_id (int, required): SublimationPath enum
    //     (NONE=0, SAND_ARMOR=1, TIDAL=2, STORM=3, FURNACE=4, RADIANCE=5).
    //   id (String, required): stable path key, e.g. "sand_armor".
    //   title_key (String, optional): localization key.
    //   primary_element (int, optional): RuneElement, default 2 (EARTH).
    //   organ_stages (Array of Dictionary, optional): each entry has
    //     slot (int, OrganSlot), organ_name (String), element (int),
    //     min_sublimation_level (int), sublimation_degree_granted (int).
    // Returns true on success.
    static bool register_path(const godot::Dictionary& def);

    // Register an organ skill from GDScript.
    // Dictionary fields:
    //   id (String, required): stable skill key.
    //   title_key (String, optional): localization key.
    //   required_slot (int, optional): OrganSlot, default 1 (BONE).
    //   required_path (int, optional): SublimationPath, default 1 (SAND_ARMOR).
    //   min_organ_level (int, optional, default 0).
    //   mana_cost (int, optional, default 10).
    //   cooldown_ticks (int, optional, default 60).
    //   effect_type (int, optional, default 0).
    //   effect_param_1 (float, optional, default 0.0).
    //   effect_param_2 (float, optional, default 0.0).
    // Returns true on success.
    static bool register_skill(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
