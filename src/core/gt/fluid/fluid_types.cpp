#include "fluid_types.hpp"

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

    register_builtin_fluids();
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
    // --- Basic fluids ---
    register_fluid({"water", "Water", "H2O", 300, false});
    register_fluid({"lava", "Lava", "?", 1500, false});
    register_fluid({"steam", "Steam", "H2O", 400, true});

    // --- Oil processing ---
    register_fluid({"oil", "Oil", "?", 300, false});
    register_fluid({"oil_heavy", "Heavy Oil", "?", 350, false});
    register_fluid({"oil_light", "Light Oil", "?", 250, false});

    // --- Fuels ---
    register_fluid({"fuel_diesel", "Diesel Fuel", "?", 250, false});
    register_fluid({"fuel_rocket", "Rocket Fuel", "?", 200, false});
    register_fluid({"ethanol", "Ethanol", "C2H5OH", 300, false});

    // --- Acids ---
    register_fluid({"sulfuric_acid", "Sulfuric Acid", "H2SO4", 300, false});
    register_fluid({"hydrochloric_acid", "Hydrochloric Acid", "HCl", 300,
                    false});
    register_fluid({"nitric_acid", "Nitric Acid", "HNO3", 300, false});
    register_fluid({"hydrofluoric_acid", "Hydrofluoric Acid", "HF", 300,
                    false});
    register_fluid({"aqua_regia", "Aqua Regia", "HNO3+3HCl", 300, false});

    // --- Industrial chemicals ---
    register_fluid({"creosote", "Creosote", "?", 400, false});
    register_fluid({"lubricant", "Lubricant", "?", 300, false});
    register_fluid({"glue", "Glue", "?", 300, false});
    register_fluid({"biomass", "Biomass", "?", 300, false});

    // --- Gases ---
    register_fluid({"oxygen", "Oxygen", "O2", 90, true});
    register_fluid({"hydrogen", "Hydrogen", "H2", 20, true});
    register_fluid({"nitrogen", "Nitrogen", "N2", 77, true});
    register_fluid({"natural_gas", "Natural Gas", "CH4", 111, true});

    // --- Molten metals ---
    register_fluid({"molten_iron", "Molten Iron", "Fe", 1811, false});
    register_fluid({"molten_gold", "Molten Gold", "Au", 1337, false});
    register_fluid({"molten_copper", "Molten Copper", "Cu", 1358, false});
    register_fluid({"molten_tin", "Molten Tin", "Sn", 505, false});
    register_fluid({"molten_lead", "Molten Lead", "Pb", 601, false});

    // --- Other ---
    register_fluid({"mercury", "Mercury", "Hg", 234, false});
    register_fluid({"distilled_water", "Distilled Water", "H2O", 300, false});
}

} // namespace science_and_theology::gt