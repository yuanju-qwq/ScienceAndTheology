#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rune_def.hpp"
#include "altar_def.hpp"

namespace science_and_theology::magic {

enum class RitualEffectType : uint8_t {
    NONE = 0,
    MACHINE_BLESSING,
    TOOL_ENCHANTMENT,
    TERRAIN_ALTERATION,
    PLAYER_BUFF,
    WORLD_EVENT,
    CURSE,
    TELEPORTATION,
    DIVINATION,
    MANA_EXPANSION,
    COUNT
};

struct RitualPedestalSlot {
    RuneElement element = RuneElement::FIRE;
    RuneTier min_tier = RuneTier::COMMON;
    bool strict_element = false;
};

struct RitualEffectData {
    RitualEffectType type = RitualEffectType::NONE;
    std::string param_json;      // JSON-serialized parameters
    int duration_ticks = 0;      // 0 = instant/permanent
};

struct RitualRecipe {
    const char* id = "";
    const char* display_name = "";

    std::vector<RitualPedestalSlot> pedestals;

    int mana_cost = 50;
    int duration_ticks = 100;   // 5 seconds at 20TPS
    bool consume_runes = true;

    RitualEffectData effect;
};

} // namespace science_and_theology::magic
