#pragma once

#include <cstdint>

namespace science_and_theology::magic {

enum class RuneElement : uint8_t {
    FIRE = 0,
    WATER,
    EARTH,
    AIR,
    LIGHT,
    DARK,
    ORDER,
    CHAOS,
    COUNT
};

enum class RuneTier : uint8_t {
    COMMON = 0,
    REFINED,
    SUPERIOR,
    LEGENDARY,
    COUNT
};

struct RuneDef {
    const char* name = "";
    RuneElement element = RuneElement::FIRE;
    RuneTier tier = RuneTier::COMMON;
    int potency = 1;
};

inline int rune_tier_potency(RuneTier tier) {
    switch (tier) {
        case RuneTier::COMMON:    return 1;
        case RuneTier::REFINED:   return 2;
        case RuneTier::SUPERIOR:  return 3;
        case RuneTier::LEGENDARY: return 5;
        default:                  return 0;
    }
}

inline const char* rune_element_name(RuneElement elem) {
    switch (elem) {
        case RuneElement::FIRE:   return "fire";
        case RuneElement::WATER:  return "water";
        case RuneElement::EARTH:  return "earth";
        case RuneElement::AIR:    return "air";
        case RuneElement::LIGHT:  return "light";
        case RuneElement::DARK:   return "dark";
        case RuneElement::ORDER:  return "order";
        case RuneElement::CHAOS:  return "chaos";
        default:                  return "unknown";
    }
}

inline const char* rune_tier_name(RuneTier tier) {
    switch (tier) {
        case RuneTier::COMMON:    return "common";
        case RuneTier::REFINED:   return "refined";
        case RuneTier::SUPERIOR:  return "superior";
        case RuneTier::LEGENDARY: return "legendary";
        default:                  return "unknown";
    }
}

} // namespace science_and_theology::magic
