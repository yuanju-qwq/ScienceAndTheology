#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/fuel/fuel_def.hpp"

namespace science_and_theology {

class GDFuelRegistry : public godot::Object {
    GDCLASS(GDFuelRegistry, godot::Object)

public:
    GDFuelRegistry() = default;
    ~GDFuelRegistry() override = default;

    // Register a new fuel from a content pack.
    // Dictionary fields:
    //   name (String, required): stable fuel key.
    //   title_key (String, optional): localization key.
    //   burn_ticks (int, required): burn duration in game ticks (20 TPS).
    //   category (int, optional): FuelCategory enum, default SOLID (0).
    //   item_id (int, optional): the item id to register as solid fuel.
    //   fluid_name (String, optional): fluid name for liquid/gas fuel.
    // Exactly one of item_id or fluid_name must be provided.
    // Returns true on success, false on failure (missing fields or
    // duplicate).
    static bool register_fuel(const godot::Dictionary& def);

    static bool is_fuel(int64_t item_id);
    static int64_t get_burn_ticks(int64_t item_id);
    static godot::Dictionary get_fuel_info(int64_t item_id);
    static godot::String get_burn_ticks_formatted(int64_t item_id);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
