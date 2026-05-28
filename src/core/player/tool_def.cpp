#include "tool_def.hpp"

namespace science_and_theology::gt {

ToolStats ToolStats::default_for(ToolType type, uint8_t tier) {
    ToolStats stats;
    stats.type = type;
    stats.tier = tier;

    switch (type) {
    case ToolType::PICKAXE:
        stats.mining_level = tier + 1;
        switch (tier) {
        case 0: // Wood / Flint
            stats.speed_multiplier = 0.8f;
            stats.max_durability = 60;
            stats.attack_damage = 2.0f;
            break;
        case 1: // Stone
            stats.speed_multiplier = 1.0f;
            stats.max_durability = 132;
            stats.attack_damage = 3.0f;
            break;
        case 2: // Iron
            stats.speed_multiplier = 1.5f;
            stats.max_durability = 251;
            stats.attack_damage = 4.0f;
            break;
        case 3: // Diamond
            stats.speed_multiplier = 3.0f;
            stats.max_durability = 1562;
            stats.attack_damage = 5.0f;
            stats.mining_level = 4;
            break;
        default:
            stats.speed_multiplier = 5.0f;
            stats.max_durability = 3200;
            stats.attack_damage = 6.0f;
            stats.mining_level = tier;
            break;
        }
        break;

    case ToolType::AXE:
        stats.mining_level = tier + 1;
        switch (tier) {
        case 0:
            stats.speed_multiplier = 1.0f;
            stats.max_durability = 60;
            stats.attack_damage = 3.0f;
            break;
        case 1:
            stats.speed_multiplier = 1.2f;
            stats.max_durability = 132;
            stats.attack_damage = 4.0f;
            break;
        case 2:
            stats.speed_multiplier = 1.8f;
            stats.max_durability = 251;
            stats.attack_damage = 5.0f;
            break;
        case 3:
            stats.speed_multiplier = 3.5f;
            stats.max_durability = 1562;
            stats.attack_damage = 6.0f;
            stats.mining_level = 4;
            break;
        default:
            stats.speed_multiplier = 6.0f;
            stats.max_durability = 3200;
            stats.attack_damage = 7.0f;
            stats.mining_level = tier;
            break;
        }
        break;

    case ToolType::SHOVEL:
        stats.mining_level = tier + 1;
        switch (tier) {
        case 0:
            stats.speed_multiplier = 1.0f;
            stats.max_durability = 60;
            stats.attack_damage = 1.5f;
            break;
        case 1:
            stats.speed_multiplier = 1.2f;
            stats.max_durability = 132;
            stats.attack_damage = 2.5f;
            break;
        case 2:
            stats.speed_multiplier = 1.8f;
            stats.max_durability = 251;
            stats.attack_damage = 3.5f;
            break;
        case 3:
            stats.speed_multiplier = 3.5f;
            stats.max_durability = 1562;
            stats.attack_damage = 4.5f;
            stats.mining_level = 4;
            break;
        default:
            stats.speed_multiplier = 6.0f;
            stats.max_durability = 3200;
            stats.attack_damage = 5.5f;
            stats.mining_level = tier;
            break;
        }
        break;

    case ToolType::SWORD:
        stats.attack_cooldown = 0.4f;
        stats.mining_level = 0;
        stats.speed_multiplier = 0.0f;
        switch (tier) {
        case 0:
            stats.attack_damage = 4.0f;
            stats.max_durability = 60;
            stats.attack_cooldown = 0.55f;
            break;
        case 1:
            stats.attack_damage = 5.0f;
            stats.max_durability = 132;
            break;
        case 2:
            stats.attack_damage = 6.0f;
            stats.max_durability = 251;
            stats.attack_cooldown = 0.35f;
            break;
        case 3:
            stats.attack_damage = 7.0f;
            stats.max_durability = 1562;
            stats.attack_cooldown = 0.3f;
            break;
        default:
            stats.attack_damage = 8.0f + tier * 2.0f;
            stats.max_durability = 3200;
            stats.attack_cooldown = 0.25f;
            break;
        }
        break;

    default:
        break;
    }

    return stats;
}

} // namespace science_and_theology::gt
