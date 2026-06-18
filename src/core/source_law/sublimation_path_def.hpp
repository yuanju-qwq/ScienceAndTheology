#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/magic/rune_def.hpp"
#include "organ_def.hpp"
#include "source_law_types.hpp"

namespace science_and_theology::source_law {

// ============================================================
// PathOrganStage — one organ stage in a sublimation path
// ============================================================
struct PathOrganStage {
    OrganSlot slot = OrganSlot::BONE;
    const char* organ_name = "";
    magic::RuneElement element = magic::RuneElement::EARTH;
    int min_sublimation_level = 1;
    int sublimation_degree_granted = 1;
};

// ============================================================
// OrganSkillDef — definition of an organ-granted skill
// ============================================================
struct OrganSkillDef {
    const char* id = "";
    const char* title_key = "";
    OrganSlot required_slot = OrganSlot::BONE;
    SublimationPath required_path = SublimationPath::SAND_ARMOR;
    int min_organ_level = 0;

    // Mana cost to activate.
    int mana_cost = 10;

    // Cooldown in ticks (20 TPS).
    int cooldown_ticks = 60;

    // Skill effect type (for future expansion).
    int effect_type = 0;

    // Effect parameters (e.g. shield amount, damage, duration).
    float effect_param_1 = 0.0f;
    float effect_param_2 = 0.0f;
};

// ============================================================
// SublimationPathDef — definition of a sublimation road
// ============================================================
struct SublimationPathDef {
    SublimationPath path_id = SublimationPath::NONE;
    const char* id = "";
    const char* title_key = "";
    magic::RuneElement primary_element = magic::RuneElement::EARTH;

    // Organ stages in this path (ordered by sublimation level).
    std::vector<PathOrganStage> organ_stages;

    // Skills granted by this path's organs.
    std::vector<OrganSkillDef> skills;
};

} // namespace science_and_theology::source_law
