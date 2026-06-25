#include "gd_fuel_registry.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <unordered_set>

#include "core/fluid/fluid_registry.hpp"
#include "core/fuel/fuel_registry.hpp"
#include "core/material/material_item.hpp"

namespace science_and_theology {

using namespace godot;
using namespace gt;

namespace {
// Persistent string storage for fuel names/keys from GDScript.
std::unordered_set<std::string> g_string_pool;

const char* intern_string(const std::string& s) {
    auto it = g_string_pool.find(s);
    if (it != g_string_pool.end()) {
        return it->c_str();
    }
    auto result = g_string_pool.insert(s);
    return result.first->c_str();
}
} // namespace

void GDFuelRegistry::_bind_methods() {
    ClassDB::bind_static_method("GDFuelRegistry",
        D_METHOD("register_fuel", "def"),
        &GDFuelRegistry::register_fuel);
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

bool GDFuelRegistry::register_fuel(const Dictionary& def) {
    String name = def.get("name", "");
    if (name.is_empty()) return false;

    Variant burn_v = def.get("burn_ticks", Variant());
    if (burn_v.get_type() == Variant::NIL) return false;
    int64_t burn_ticks = static_cast<int64_t>(burn_v);
    if (burn_ticks <= 0) return false;

    FuelDefinition fuel_def;
    fuel_def.name = intern_string(std::string(name.utf8().get_data()));
    String title = def.get("title_key", "");
    fuel_def.title_key = title.is_empty()
        ? fuel_def.name
        : intern_string(std::string(title.utf8().get_data()));
    fuel_def.burn_ticks = burn_ticks;
    fuel_def.category = static_cast<FuelCategory>(
        static_cast<int>(def.get("category", 0)));

    // Resolve fuel target: either item_id (solid) or fluid_name (liquid/gas).
    Variant item_id_v = def.get("item_id", Variant());
    Variant fluid_name_v = def.get("fluid_name", Variant());
    if (item_id_v.get_type() != Variant::NIL) {
        fuel_def.item_id = static_cast<ItemId>(static_cast<int64_t>(item_id_v));
    } else if (fluid_name_v.get_type() != Variant::NIL) {
        String fluid_name = fluid_name_v;
        FluidId fid = FluidRegistry::get_fluid_id(fluid_name.utf8().get_data());
        if (fid == kInvalidFluidId) return false;
        fuel_def.fluid_id = fid;
    } else {
        return false;  // Neither item_id nor fluid_name provided.
    }

    FuelRegistry::register_fuel(fuel_def);
    return true;
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
