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
    register_energy_modules();
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
// Energy modules — required on all machines
// ============================================================

void ModuleRegistry::register_energy_modules() {
    auto reg = [](const char* name, VoltageTier tier, int64_t max_eu) {
        ModuleDefinition def;
        def.name = name;
        def.title_key = name;
        def.category = ModuleCategory::ENERGY_INPUT;
        def.tier = tier;
        def.max_eu_per_tick = max_eu;
        g_modules.push_back(def);
        g_module_by_name[name] = &g_modules.back();
    };

    reg("Energy Module ULV",   VoltageTier::ULV,    8);
    reg("Energy Module LV",    VoltageTier::LV,    32);
    reg("Energy Module MV",    VoltageTier::MV,   128);
    reg("Energy Module HV",    VoltageTier::HV,   512);
    reg("Energy Module EV",    VoltageTier::EV,  2048);
    reg("Energy Module IV",    VoltageTier::IV,  8192);
    reg("Energy Module LuV",   VoltageTier::LuV, 32768);
    reg("Energy Module ZPM",   VoltageTier::ZPM, 131072);
    reg("Energy Module UV",    VoltageTier::UV,  524288);

    // 4A energy modules
    reg("Energy Module ULV 4A",   VoltageTier::ULV,    32);
    reg("Energy Module LV 4A",    VoltageTier::LV,    128);
    reg("Energy Module MV 4A",    VoltageTier::MV,    512);
    reg("Energy Module HV 4A",    VoltageTier::HV,   2048);
    reg("Energy Module EV 4A",    VoltageTier::EV,   8192);
    reg("Energy Module IV 4A",    VoltageTier::IV,  32768);
    reg("Energy Module LuV 4A",   VoltageTier::LuV, 131072);
    reg("Energy Module ZPM 4A",   VoltageTier::ZPM, 524288);
    reg("Energy Module UV 4A",    VoltageTier::UV, 2097152);

    // 16A energy modules
    reg("Energy Module LV 16A",   VoltageTier::LV,    512);
    reg("Energy Module MV 16A",   VoltageTier::MV,   2048);
    reg("Energy Module HV 16A",   VoltageTier::HV,   8192);
    reg("Energy Module EV 16A",   VoltageTier::EV,  32768);
    reg("Energy Module IV 16A",   VoltageTier::IV, 131072);
    reg("Energy Module LuV 16A",  VoltageTier::LuV, 524288);
    reg("Energy Module ZPM 16A",  VoltageTier::ZPM, 2097152);
    reg("Energy Module UV 16A",   VoltageTier::UV, 8388608);
}

// ============================================================
// Coils — affect heat capacity, efficiency, and parallel count
// ============================================================

void ModuleRegistry::register_coils() {
    auto reg = [](const char* name, VoltageTier tier,
                  int64_t heat, int64_t eff, int64_t parallel) {
        ModuleDefinition def;
        def.name = name;
        def.title_key = name;
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
        def.title_key = name;
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