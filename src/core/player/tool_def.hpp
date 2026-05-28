#pragma once

#include <cstdint>
#include <string>

namespace science_and_theology::gt {

enum class ToolType : uint8_t {
    NONE = 0,
    PICKAXE,
    AXE,
    SHOVEL,
    SWORD,
};

inline constexpr const char* kToolTypeNames[] = {
    "None",
    "Pickaxe",
    "Axe",
    "Shovel",
    "Sword",
};

inline const char* tool_type_name(ToolType type) {
    auto idx = static_cast<size_t>(type);
    if (idx >= sizeof(kToolTypeNames) / sizeof(kToolTypeNames[0])) return "Unknown";
    return kToolTypeNames[idx];
}

struct ToolStats {
    ToolType type = ToolType::NONE;
    uint8_t tier = 0;
    float speed_multiplier = 1.0f;
    uint8_t mining_level = 0;
    float attack_damage = 1.0f;
    float attack_cooldown = 0.5f;
    int32_t max_durability = 0;
    uint8_t range = 1;

    bool is_weapon() const { return type == ToolType::SWORD; }
    bool is_mining_tool() const {
        return type == ToolType::PICKAXE || type == ToolType::AXE
            || type == ToolType::SHOVEL;
    }

    static ToolStats default_for(ToolType type, uint8_t tier);
};

} // namespace science_and_theology::gt
