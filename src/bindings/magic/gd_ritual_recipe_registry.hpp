#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/magic/ritual_recipe_registry.hpp"

namespace science_and_theology {

class GDRitualRecipeRegistry : public godot::Resource {
    GDCLASS(GDRitualRecipeRegistry, godot::Resource)

public:
    GDRitualRecipeRegistry() = default;

    // Register a ritual recipe from GDScript.
    // Dictionary fields:
    //   id (String, required): stable recipe key, e.g. "ritual_machine_speed".
    //   title_key (String, optional): human-readable title/translation key.
    //   pedestals (Array of Dictionary, optional): each entry has
    //     element (int, RuneElement, default 0=FIRE),
    //     min_tier (int, RuneTier, default 0=COMMON),
    //     strict_element (bool, default false).
    //   mana_cost (int, optional, default 50).
    //   duration_ticks (int, optional, default 100).
    //   consume_runes (bool, optional, default true).
    //   effect (Dictionary, optional): { type (int, RitualEffectType, default 0=NONE),
    //     param_json (String, default ""), duration_ticks (int, default 0) }.
    // Returns true on success.
    static bool register_recipe(const godot::Dictionary& def);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
