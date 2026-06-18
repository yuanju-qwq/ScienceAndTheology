#include "gd_fuel_registry.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "core/fuel/fuel_registry.hpp"
#include "core/material/material_item.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

void GDFuelRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDFuelRegistry",
        D_METHOD("is_fuel", "item_id"),
        &GDFuelRegistry::is_fuel);
    ClassDB::bind_static_method("GDFuelRegistry",
        D_METHOD("get_burn_ticks", "item_id"),
        &GDFuelRegistry::get_burn_ticks);
    ClassDB::bind_static_method("GDFuelRegistry",
        D_METHOD("get_fuel_info", "item_id"),
        &GDFuelRegistry::get_fuel_info);
    ClassDB::bind_static_method("GDFuelRegistry",
        D_METHOD("get_burn_ticks_formatted", "item_id"),
        &GDFuelRegistry::get_burn_ticks_formatted);
}

bool GDFuelRegistry::is_fuel(int64_t item_id) {
    return FuelRegistry::is_item_fuel(static_cast<ItemId>(item_id));
}

int64_t GDFuelRegistry::get_burn_ticks(int64_t item_id) {
    return FuelRegistry::get_item_burn_ticks(static_cast<ItemId>(item_id));
}

godot::Dictionary GDFuelRegistry::get_fuel_info(int64_t item_id) {
    godot::Dictionary info;
    const FuelDefinition* def = FuelRegistry::get_by_item(
        static_cast<ItemId>(item_id));
    if (def == nullptr) return info;

    info["name"] = def->name;
    info["title_key"] = def->title_key;
    info["category"] = static_cast<int>(def->category);
    info["burn_ticks"] = static_cast<int64_t>(def->burn_ticks);
    return info;
}

godot::String GDFuelRegistry::get_burn_ticks_formatted(int64_t item_id) {
    int64_t ticks = get_burn_ticks(item_id);
    if (ticks <= 0) return "";

    int seconds = static_cast<int>(ticks / 20);
    if (seconds >= 60) {
        int minutes = seconds / 60;
        seconds %= 60;
        if (seconds > 0) {
            return godot::String::num(minutes) + "m " + godot::String::num(seconds) + "s";
        }
        return godot::String::num(minutes) + "m";
    }
    return godot::String::num(seconds) + "s";
}

} // namespace science_and_theology
