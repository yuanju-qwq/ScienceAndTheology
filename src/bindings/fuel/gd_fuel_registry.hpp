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

    static bool is_fuel(int64_t item_id);
    static int64_t get_burn_ticks(int64_t item_id);
    static godot::Dictionary get_fuel_info(int64_t item_id);
    static godot::String get_burn_ticks_formatted(int64_t item_id);

protected:
    static void _bind_methods();
};

} // namespace science_and_theology
