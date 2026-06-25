#include "fuel_registry.hpp"

#include <cstring>
#include <unordered_map>

#include "fluid/fluid_registry.hpp"
#include "material/material.hpp"
#include "material/material_item.hpp"

namespace science_and_theology::gt {

// ============================================================
// Internal storage
// ============================================================

static std::vector<FuelDefinition> g_fuel_registry;
static std::unordered_map<ItemId, size_t> g_fuel_by_item;
static std::unordered_map<FluidId, size_t> g_fuel_by_fluid;

std::vector<FuelDefinition>& FuelRegistry::registry() {
    return g_fuel_registry;
}

// ============================================================
// Lifecycle
// ============================================================

void FuelRegistry::initialize() {
    g_fuel_registry.clear();
    g_fuel_by_item.clear();
    g_fuel_by_fluid.clear();
    register_builtin_fuels();
}

void FuelRegistry::register_fuel(const FuelDefinition& def) {
    // Prevent duplicate registration.
    if (def.item_id != kInvalidItemId) {
        if (g_fuel_by_item.find(def.item_id) != g_fuel_by_item.end()) return;
    }
    if (def.fluid_id != kInvalidFluidId) {
        if (g_fuel_by_fluid.find(def.fluid_id) != g_fuel_by_fluid.end()) return;
    }

    g_fuel_registry.push_back(def);
    const size_t stored_index = g_fuel_registry.size() - 1;
    const FuelDefinition& stored = g_fuel_registry[stored_index];

    if (stored.item_id != kInvalidItemId) {
        g_fuel_by_item[stored.item_id] = stored_index;
    }
    if (stored.fluid_id != kInvalidFluidId) {
        g_fuel_by_fluid[stored.fluid_id] = stored_index;
    }
}

// ============================================================
// Queries
// ============================================================

const FuelDefinition* FuelRegistry::get_by_item(ItemId item_id) {
    auto it = g_fuel_by_item.find(item_id);
    return (it != g_fuel_by_item.end() && it->second < g_fuel_registry.size())
        ? &g_fuel_registry[it->second]
        : nullptr;
}

const FuelDefinition* FuelRegistry::get_by_fluid(FluidId fluid_id) {
    auto it = g_fuel_by_fluid.find(fluid_id);
    return (it != g_fuel_by_fluid.end() && it->second < g_fuel_registry.size())
        ? &g_fuel_registry[it->second]
        : nullptr;
}

bool FuelRegistry::is_item_fuel(ItemId item_id) {
    return get_by_item(item_id) != nullptr;
}

bool FuelRegistry::is_fluid_fuel(FluidId fluid_id) {
    return get_by_fluid(fluid_id) != nullptr;
}

int64_t FuelRegistry::get_item_burn_ticks(ItemId item_id) {
    const FuelDefinition* def = get_by_item(item_id);
    return def ? def->burn_ticks : 0;
}

int64_t FuelRegistry::get_fluid_burn_ticks(FluidId fluid_id) {
    const FuelDefinition* def = get_by_fluid(fluid_id);
    return def ? def->burn_ticks : 0;
}

const std::vector<FuelDefinition>& FuelRegistry::get_all() {
    return g_fuel_registry;
}

size_t FuelRegistry::get_fuel_count() {
    return g_fuel_registry.size();
}

// ============================================================
// Built-in fuel definitions
// ============================================================

void FuelRegistry::register_builtin_fuels() {
    // No-op: solid fuels are now registered from GDScript.
    // Fluid fuels are registered in register_builtin_fluid_fuels().
}

void FuelRegistry::register_builtin_fluid_fuels() {
    auto reg_fluid = [](const char* fluid_name, const char* title_key,
                         int64_t burn_ticks, FuelCategory cat) {
        FluidId id = FluidRegistry::get_fluid_id(fluid_name);
        if (id == kInvalidFluidId) return;

        FuelDefinition def;
        def.name = fluid_name;
        def.title_key = title_key;
        def.category = cat;
        def.fluid_id = id;
        def.burn_ticks = burn_ticks;
        register_fuel(def);
    };

    // --- Liquid fuels ---
    reg_fluid("lava", "fuel.lava", 10000, FuelCategory::LIQUID);
    reg_fluid("fuel_diesel", "fuel.diesel", 5000, FuelCategory::LIQUID);
    reg_fluid("oil", "fuel.oil", 3000, FuelCategory::LIQUID);

    // --- Gaseous fuels ---
    reg_fluid("natural_gas", "fuel.natural_gas", 3000, FuelCategory::GAS);
    reg_fluid("hydrogen", "fuel.hydrogen", 1000, FuelCategory::GAS);
}

} // namespace science_and_theology::gt
