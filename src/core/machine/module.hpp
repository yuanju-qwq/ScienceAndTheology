#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../power/power_node.hpp"

namespace science_and_theology::gt {

// ============================================================
// Module categories
// ============================================================

enum class ModuleCategory : uint8_t {
    ENERGY_INPUT  = 0,   // energy module — required on all machines
    COIL          = 1,   // heating coil — heat capacity, parallel
    MUFFLER       = 2,   // pollution reduction
    OVERCLOCK     = 3,   // speed up at extra power cost
    TRANSFORMER   = 4,   // alter accepted voltage
};

// ============================================================
// Module definition (data, not runtime)
// ============================================================

struct ModuleDefinition {
    const char* name = "";
    const char* title_key = "";
    ModuleCategory category = ModuleCategory::ENERGY_INPUT;
    VoltageTier tier = VoltageTier::ULV;

    // Energy hatch fields
    int64_t max_eu_per_tick = 0;     // max power this hatch can pass (0 = use tier default)

    // Coil fields
    int64_t heat_capacity = 0;        // added to total heat
    int64_t efficiency_pct = 100;     // efficiency modifier (100 = no change)
    int64_t parallel_bonus = 0;       // added parallel count per coil

    // Muffler fields
    int64_t pollution_reduction_pct = 0;  // percentage reduced

    // Overclock fields
    int64_t speed_multiplier_pct = 100;   // 200 = 2x speed
    int64_t power_multiplier_pct = 100;   // 200 = 2x power cost
};

// ============================================================
// Slot template — defines what a machine blueprint can accept
// ============================================================

struct ModuleSlot {
    ModuleCategory category = ModuleCategory::ENERGY_INPUT;
    int max_count = 1;                     // max modules of this category
    VoltageTier min_tier = VoltageTier::ULV;
    VoltageTier max_tier = VoltageTier::UV;
};

// ============================================================
// Installed module — runtime instance in a slot
// ============================================================

struct InstalledModule {
    const ModuleDefinition* def = nullptr;
};

// ============================================================
// Module registry — all available module definitions
// ============================================================

class ModuleRegistry {
public:
    static void initialize();

    static const ModuleDefinition* get(const char* name);
    static const ModuleDefinition* get(ModuleCategory category, VoltageTier tier);
    static const std::vector<ModuleDefinition*>& get_all();

private:
    static void register_energy_modules();
    static void register_coils();
    static void register_mufflers();

    static std::vector<ModuleDefinition>& storage();
};

} // namespace science_and_theology::gt