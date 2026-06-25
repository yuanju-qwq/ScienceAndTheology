#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace science_and_theology {

// GDScript binding for registering biome overrides.
// Biome overrides control per-biome ecosystem parameters (water, fertility,
// vegetation/creature caps). Species-biome association is self-described
// by CreatureSpeciesDef::biomes, not by biome overrides.
class GDBiomeConfigRegistry : public godot::Object {
    GDCLASS(GDBiomeConfigRegistry, godot::Object)

public:
    GDBiomeConfigRegistry() = default;
    ~GDBiomeConfigRegistry() override = default;

    // Register a biome override from GDScript.
    // Dictionary fields:
    //   biome_type (int, required): ecosystem_biome constant
    //       (0=Plains, 1=Desert, 2=Rocky, 3=Ocean, 4=Barren).
    //   base_water (float, optional, default 0.5).
    //   base_fertility (float, optional, default 0.5).
    //   veg_growth_multiplier (float, optional, default 1.0).
    //   max_vegetation (float, optional, default 1.0).
    //   max_herbivore (float, optional, default 1.0).
    //   max_predator (float, optional, default 1.0).
    // Returns true on success.
    static bool register_biome_override(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
