#include "organ_skill_system.hpp"
#include "player_source_law_data.hpp"

namespace science_and_theology::source_law {

bool OrganSkillSystem::can_activate(const PlayerSourceLawData& data,
                                     const OrganSkillDef& skill,
                                     uint64_t current_tick,
                                     uint64_t last_activate_tick) {
    // Must be initiated.
    if (!data.is_initiated()) return false;

    // Must have enough mana.
    int cost = get_mana_cost(data, skill);
    if (data.mana_pool().current_mana < cost) return false;

    // Must have the required organ.
    int slot = static_cast<int>(skill.required_slot);
    if (slot < 0 || slot >= kOrganSlotCount) return false;
    const auto& organ = data.organs()[slot];
    if (!organ.is_sublimated()) return false;
    if (organ.path_id != skill.required_path) return false;
    if (organ.level < skill.min_organ_level) return false;

    // Must be off cooldown.
    if (current_tick - last_activate_tick < static_cast<uint64_t>(skill.cooldown_ticks)) {
        return false;
    }

    return true;
}

int OrganSkillSystem::get_mana_cost(const PlayerSourceLawData& data,
                                     const OrganSkillDef& skill) {
    // Base cost from skill definition.
    int cost = skill.mana_cost;

    // Stability penalty: low stability increases cost.
    if (data.stability() < 50.0f) {
        float penalty = 1.0f + (50.0f - data.stability()) / 100.0f;
        cost = static_cast<int>(cost * penalty);
    }

    // Mutation penalty: high mutation increases cost.
    if (data.mutation() > 40.0f) {
        float penalty = 1.0f + (data.mutation() - 40.0f) / 100.0f;
        cost = static_cast<int>(cost * penalty);
    }

    return cost;
}

} // namespace science_and_theology::source_law
