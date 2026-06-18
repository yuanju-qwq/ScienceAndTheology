#include "sublimation_path_registry.hpp"

#include <string>
#include <unordered_map>

namespace science_and_theology::source_law {

static SublimationPathDef g_paths[static_cast<int>(SublimationPath::COUNT)];
static std::unordered_map<std::string, const OrganSkillDef*> g_skill_map;

void SublimationPathRegistry::initialize() {
    g_skill_map.clear();
    register_builtin_paths();
}

const SublimationPathDef* SublimationPathRegistry::get(SublimationPath path) {
    int idx = static_cast<int>(path);
    if (idx < 0 || idx >= static_cast<int>(SublimationPath::COUNT)) return nullptr;
    return &g_paths[idx];
}

const OrganSkillDef* SublimationPathRegistry::get_skill(const char* skill_id) {
    auto it = g_skill_map.find(skill_id);
    if (it == g_skill_map.end()) return nullptr;
    return it->second;
}

std::vector<const OrganSkillDef*> SublimationPathRegistry::get_available_skills(
    const OrganArray& organs) {
    std::vector<const OrganSkillDef*> result;

    for (int p = 1; p < static_cast<int>(SublimationPath::COUNT); ++p) {
        const auto& path_def = g_paths[p];
        for (const auto& skill : path_def.skills) {
            int slot = static_cast<int>(skill.required_slot);
            if (slot < 0 || slot >= kOrganSlotCount) continue;

            const auto& organ = organs[slot];
            if (!organ.is_sublimated()) continue;
            if (organ.path_id != skill.required_path) continue;
            if (organ.level < skill.min_organ_level) continue;

            result.push_back(&skill);
        }
    }

    return result;
}

size_t SublimationPathRegistry::count() {
    return static_cast<int>(SublimationPath::COUNT);
}

void SublimationPathRegistry::register_builtin_paths() {
    // --- Sand Armor Path ---
    auto& sand = g_paths[static_cast<int>(SublimationPath::SAND_ARMOR)];
    sand.path_id = SublimationPath::SAND_ARMOR;
    sand.id = "sand_armor";
    sand.title_key = "path.sand_armor";
    sand.primary_element = magic::RuneElement::EARTH;
    sand.organ_stages = {
        {OrganSlot::BONE, "Sand Armor Rock Core", magic::RuneElement::EARTH, 1, 1},
    };
    sand.skills = {
        {"skill_rock_shield", "Rock Shield",
         OrganSlot::BONE, SublimationPath::SAND_ARMOR, 0,
         10, 60, 1, 30.0f, 5.0f},
    };
    for (const auto& skill : sand.skills) {
        g_skill_map[skill.id] = &skill;
    }

    // --- Tidal Path (placeholder for V0.7) ---
    auto& tidal = g_paths[static_cast<int>(SublimationPath::TIDAL)];
    tidal.path_id = SublimationPath::TIDAL;
    tidal.id = "tidal";
    tidal.title_key = "path.tidal";
    tidal.primary_element = magic::RuneElement::WATER;
    tidal.organ_stages = {
        {OrganSlot::LUNG, "Tidal Lung", magic::RuneElement::WATER, 1, 1},
    };

    // --- Storm Path (placeholder for V0.7) ---
    auto& storm = g_paths[static_cast<int>(SublimationPath::STORM)];
    storm.path_id = SublimationPath::STORM;
    storm.id = "storm";
    storm.title_key = "path.storm";
    storm.primary_element = magic::RuneElement::AIR;
    storm.organ_stages = {
        {OrganSlot::NERVE, "Thunder Nerve", magic::RuneElement::AIR, 1, 1},
    };

    // --- Furnace Path (placeholder for V0.7) ---
    auto& furnace = g_paths[static_cast<int>(SublimationPath::FURNACE)];
    furnace.path_id = SublimationPath::FURNACE;
    furnace.id = "furnace";
    furnace.title_key = "path.furnace";
    furnace.primary_element = magic::RuneElement::FIRE;
    furnace.organ_stages = {
        {OrganSlot::HEART, "Blazing Heart", magic::RuneElement::FIRE, 1, 1},
    };

    // --- Radiance Path (placeholder for V0.7) ---
    auto& radiance = g_paths[static_cast<int>(SublimationPath::RADIANCE)];
    radiance.path_id = SublimationPath::RADIANCE;
    radiance.id = "radiance";
    radiance.title_key = "path.radiance";
    radiance.primary_element = magic::RuneElement::LIGHT;
    radiance.organ_stages = {
        {OrganSlot::EYE, "Radiance Eye", magic::RuneElement::LIGHT, 1, 1},
    };
}

} // namespace science_and_theology::source_law
