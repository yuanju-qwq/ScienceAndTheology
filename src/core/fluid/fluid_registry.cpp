#include "fluid_registry.hpp"

#include <unordered_map>

namespace science_and_theology::gt {

static std::vector<FluidDefinition> g_fluid_registry;
static std::unordered_map<std::string, FluidId> g_fluid_name_map;

std::vector<FluidDefinition>& FluidRegistry::registry() {
    return g_fluid_registry;
}

void FluidRegistry::initialize() {
    g_fluid_registry.clear();
    g_fluid_name_map.clear();

    // Reserve ID 0 as invalid.
    g_fluid_registry.push_back({"__invalid__", "Invalid", "", 0, false});

    // Built-in fluids are now registered from GDScript via GDFluidRegistry
    // (see BuiltinFluids.gd).
}

FluidId FluidRegistry::register_fluid(const FluidDefinition& def) {
    // Check for duplicates by name.
    auto name_it = g_fluid_name_map.find(def.name);
    if (name_it != g_fluid_name_map.end()) {
        return name_it->second;
    }

    FluidId id = static_cast<FluidId>(g_fluid_registry.size());
    g_fluid_registry.push_back(def);
    g_fluid_name_map[def.name] = id;
    return id;
}

const FluidDefinition* FluidRegistry::get_fluid(FluidId id) {
    if (id == kInvalidFluidId || id >= g_fluid_registry.size()) return nullptr;
    return &g_fluid_registry[id];
}

const FluidDefinition* FluidRegistry::get_fluid_by_name(const char* name) {
    auto it = g_fluid_name_map.find(name);
    if (it == g_fluid_name_map.end()) return nullptr;
    return get_fluid(it->second);
}

FluidId FluidRegistry::get_fluid_id(const char* name) {
    auto it = g_fluid_name_map.find(name);
    return (it != g_fluid_name_map.end()) ? it->second : kInvalidFluidId;
}

size_t FluidRegistry::get_fluid_count() {
    // Subtract 1 for the invalid entry at index 0.
    return g_fluid_registry.size() > 0 ? g_fluid_registry.size() - 1 : 0;
}

// ============================================================
// Built-in fluid definitions
// ============================================================

void FluidRegistry::register_builtin_fluids() {
    // No-op: built-in fluids are now registered from GDScript via
    // GDFluidRegistry (see BuiltinFluids.gd).
}

} // namespace science_and_theology::gt