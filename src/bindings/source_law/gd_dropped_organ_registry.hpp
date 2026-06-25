#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace science_and_theology {

// ============================================================
// GDDroppedOrganRegistry — GDExtension binding for DroppedOrganRegistry
// ============================================================
class GDDroppedOrganRegistry : public godot::Resource {
    GDCLASS(GDDroppedOrganRegistry, godot::Resource)

public:
    GDDroppedOrganRegistry() = default;

    // Register a dropped organ from GDScript.
    // Dictionary fields:
    //   id (String, required): stable organ key, e.g. "dropped_rock_lizard_heart".
    //   title_key (String, optional): display title.
    //   target_slot (int, optional): OrganSlot (HEART=0, BONE=1, BLOOD=2,
    //       LUNG=3, EYE=4, NERVE=5, SKIN=6).
    //   source (int, optional): BloodlineSource (NONE=0, CREATURE=1, ABERRATION=2).
    //   source_creature_id (String, optional): identifier of the source creature.
    //   primary_element (int, optional): RuneElement (default 2=EARTH).
    //   secondary_elements (Array of int, optional): RuneElement values.
    //   imitated_path (int, optional): SublimationPath (default 0=NONE).
    //   source_cost (int, optional, default 0).
    //   stability_modifier (float, optional, default 0.0).
    //   mutation_modifier (float, optional, default 0.0).
    //   result_quality (int, optional): OrganQuality
    //       (FLAWED=0, COMMON=1, GOOD=2, PURE=3, ANCIENT=4, PERFECT=5).
    //   result_power_multiplier (float, optional, default 0.6).
    // Returns true on success.
    static bool register_organ(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
