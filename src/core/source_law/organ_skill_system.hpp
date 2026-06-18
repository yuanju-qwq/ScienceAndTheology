#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sublimation_path_def.hpp"
#include "organ_def.hpp"

namespace science_and_theology::source_law {

// ============================================================
// OrganSkillSystem — handles skill activation, cooldown, effects
// ============================================================
class PlayerSourceLawData;

class OrganSkillSystem {
public:
    // Check if a skill can be activated.
    static bool can_activate(const PlayerSourceLawData& data,
                             const OrganSkillDef& skill,
                             uint64_t current_tick,
                             uint64_t last_activate_tick);

    // Get the effective mana cost for a skill.
    static int get_mana_cost(const PlayerSourceLawData& data,
                             const OrganSkillDef& skill);

    // Skill effect types.
    static constexpr int EFFECT_SHIELD = 1;
    static constexpr int EFFECT_DAMAGE = 2;
    static constexpr int EFFECT_HEAL = 3;
    static constexpr int EFFECT_BUFF = 4;
};

} // namespace science_and_theology::source_law
