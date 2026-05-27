#include "module.hpp"

#include <cstring>
#include <unordered_map>

namespace science_and_theology::gt {

static std::vector<ModuleDefinition> g_modules;
static std::unordered_map<std::string, const ModuleDefinition*> g_module_by_name;

std::vector<ModuleDefinition>& ModuleRegistry::storage() {
    return g_modules;
}

void ModuleRegistry::initialize() {
    g_modules.clear();
    g_module_by_name.clear();
    register_energy_hatches();
    register_coils();
    register_mufflers();
}

const ModuleDefinition* ModuleRegistry::get(const char* name) {
    auto it = g_module_by_name.find(name);
    return (it != g_module_by_name.end()) ? it->second : nullptr;
}

const ModuleDefinition* ModuleRegistry::get(ModuleCategory category,
                                             VoltageTier tier) {
    for (auto& def : g_modules) {
        if (def.category == category && def.tier == tier) return &def;
    }
    return nullptr;
}

const std::vector<ModuleDefinition*>& ModuleRegistry::get_all() {
    static thread_local std::vector<ModuleDefinition*> ptrs;
    ptrs.clear();
    for (auto& def : g_modules) {
        ptrs.push_back(&def);
    }
    return ptrs;
}

// ============================================================
// Energy hatches — required on all machines
// ============================================================

void ModuleRegistry::register_energy_hatches() {
    auto reg = [](const char* name, VoltageTier tier, int64_t max_eu) {
        ModuleDefinition def;
        def.name = name;
        def.display_name = name;
        def.category = ModuleCategory::ENERGY_INPUT;
        def.tier = tier;
        def.max_eu_per_tick = max_eu;
        g_modules.push_back(def);
        g_module_by_name[name] = &g_modules.back();
    };

    reg("Energy Hatch ULV",   VoltageTier::ULV,    8);
    reg("Energy Hatch LV",    VoltageTier::LV,    32);
    reg("Energy Hatch MV",    VoltageTier::MV,   128);
    reg("Energy Hatch HV",    VoltageTier::HV,   512);
    reg("Energy Hatch EV",    VoltageTier::EV,  2048);
    reg("Energy Hatch IV",    VoltageTier::IV,  8192);
    reg("Energy Hatch LuV",   VoltageTier::LuV, 32768);
    reg("Energy Hatch ZPM",   VoltageTier::ZPM, 131072);
    reg("Energy Hatch UV",    VoltageTier::UV,  524288);
}

// ============================================================
// Coils — affect heat capacity, efficiency, and parallel count
// ============================================================

void ModuleRegistry::register_coils() {
    auto reg = [](const char* name, VoltageTier tier,
                  int64_t heat, int64_t eff, int64_t parallel) {
        ModuleDefinition def;
        def.name = name;
        def.display_name = name;
        def.category = ModuleCategory::COIL;
        def.tier = tier;
        def.heat_capacity = heat;
        def.efficiency_pct = eff;
        def.parallel_bonus = parallel;
        g_modules.push_back(def);
        g_module_by_name[name] = &g_modules.back();
    };

    // GT-style coil progression: better coils at higher tiers
    //    name                  tier   heat    eff%   parallel
    reg("Cupronickel Coil",    VoltageTier::LV,   1800, 100, 2);
    reg("Kanthal Coil",        VoltageTier::MV,   2700, 100, 4);
    reg("Nichrome Coil",       VoltageTier::HV,   3600, 100, 6);
    reg("Tungstensteel Coil",  VoltageTier::EV,   7200,  95, 8);
    reg("HSS-G Coil",          VoltageTier::IV,   9000,  90, 10);
    reg("Naquadah Coil",       VoltageTier::LuV, 10800,  90, 12);
    reg("Naquadah Alloy Coil", VoltageTier::ZPM, 14400,  85, 14);
    reg("Superconductor Coil", VoltageTier::UV,  18000,  80, 16);
}

// ============================================================
// Mufflers — reduce pollution
// ============================================================

void ModuleRegistry::register_mufflers() {
    auto reg = [](const char* name, VoltageTier tier, int64_t reduction) {
        ModuleDefinition def;
        def.name = name;
        def.display_name = name;
        def.category = ModuleCategory::MUFFLER;
        def.tier = tier;
        def.pollution_reduction_pct = reduction;
        g_modules.push_back(def);
        g_module_by_name[name] = &g_modules.back();
    };

    reg("Muffler LV",   VoltageTier::LV,   25);
    reg("Muffler MV",   VoltageTier::MV,   50);
    reg("Muffler HV",   VoltageTier::HV,   75);
    reg("Muffler EV",   VoltageTier::EV,   85);
    reg("Muffler IV+",  VoltageTier::IV,   95);
}

} // namespace science_and_theology::gt