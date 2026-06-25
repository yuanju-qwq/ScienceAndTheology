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

    // Register a new item fuel from a content pack.
    // Dictionary fields:
    //   name (String, required): stable fuel key.
    //   title_key (String, optional): localization key.
    //   item_id (int, required): the item id to register as fuel.
    //   burn_ticks (int, required): burn duration in game ticks (20 TPS).
    //   category (int, optional): FuelCategory enum, default SOLID (0).
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
