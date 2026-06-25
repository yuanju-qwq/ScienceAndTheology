#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace science_and_theology {

class GDSpeciesRegistry : public godot::Object {
    GDCLASS(GDSpeciesRegistry, godot::Object)

public:
    GDSpeciesRegistry() = default;
    ~GDSpeciesRegistry() override = default;

    // Register a species definition from GDScript.
    // Dictionary fields:
    //   species_key (String, required): unique key (e.g. "glow_deer").
    //   title_key (String, required): translation key (e.g. "creature.glow_deer").
    //   role (int, required): 0 = HERBIVORE, 1 = PREDATOR.
    //   model_key (String, required): 3D model resource key.
    //   move_speed (float, optional, default 0.0 = use ecosystem default).
    //   base_health (float, optional, default 1.0).
    //   flee_detection_radius (float, optional, default 0.0).
    //   wander_radius (float, optional, default 0.0).
    //   model_scale (float, optional, default 1.0).
    //   biomes (Array[int], optional): biome types where this species spawns.
    //   drops (Array[Dictionary], optional):
    //       {item_key: String, chance: float, min_count: int, max_count: int}
    // Returns true on success.
    static bool register_species(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
