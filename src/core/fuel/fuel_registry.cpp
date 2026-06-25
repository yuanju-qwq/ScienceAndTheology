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

void FuelRegistry::reset() {
    // 完全清空 registry（用于热重载）。
    g_fuel_registry.clear();
    g_fuel_by_item.clear();
    g_fuel_by_fluid.clear();
}

void FuelRegistry::initialize() {
    // initialize = reset（Fuel 没有 ID 0 概念，用 index 存储）。
    // Built-in fuels are registered from GDScript via GDFuelRegistry
    // (see ContentDatabase.gd).
    reset();
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

} // namespace science_and_theology::gt
