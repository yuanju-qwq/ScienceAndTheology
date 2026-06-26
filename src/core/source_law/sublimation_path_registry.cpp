#include "sublimation_path_registry.hpp"

#include "core/common/string_pool.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

namespace science_and_theology::source_law {

static SublimationPathDef g_paths[static_cast<int>(SublimationPath::COUNT)];
static std::unordered_map<std::string, const OrganSkillDef*> g_skill_map;

void SublimationPathRegistry::initialize() {
    g_skill_map.clear();
    for (int i = 0; i < static_cast<int>(SublimationPath::COUNT); ++i) {
        g_paths[i] = SublimationPathDef{};
    }

    // Built-in paths are now registered from GDScript via
    // GDSublimationPathRegistry (see BuiltinSublimationPaths.gd).
}

bool SublimationPathRegistry::register_path(const SublimationPathDef& def) {
    int idx = static_cast<int>(def.path_id);
    if (idx <= 0 || idx >= static_cast<int>(SublimationPath::COUNT)) {
        return false;
    }

    // Skills are registered separately via register_skill; reset the slot
    // so re-registering a path does not retain stale skills.
    SublimationPathDef stored = def;
    stored.skills.clear();

    g_paths[idx] = stored;
    rebuild_skill_map();
    return true;
}

bool SublimationPathRegistry::register_skill(const OrganSkillDef& def) {
    int idx = static_cast<int>(def.required_path);
    if (idx <= 0 || idx >= static_cast<int>(SublimationPath::COUNT)) {
        return false;
    }

    // 幂等检查：若 def.id 已注册则直接返回 true，不追加新条目。
    if (def.id != nullptr && def.id[0] != '\0' &&
        g_skill_map.find(def.id) != g_skill_map.end()) {
        return true;
    }

    g_paths[idx].skills.push_back(def);
    rebuild_skill_map();
    return true;
}

void SublimationPathRegistry::rebuild_skill_map() {
    g_skill_map.clear();
    for (int p = 1; p < static_cast<int>(SublimationPath::COUNT); ++p) {
        for (const auto& skill : g_paths[p].skills) {
            if (skill.id != nullptr && skill.id[0] != '\0') {
                g_skill_map[skill.id] = &skill;
            }
        }
    }
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

void SublimationPathRegistry::reset() {
    // 清空技能查找表、字符串存储，并重置所有路径槽位。
    g_skill_map.clear();
    for (int i = 0; i < static_cast<int>(SublimationPath::COUNT); ++i) {
        g_paths[i] = SublimationPathDef{};
    }
}

} // namespace science_and_theology::source_law
