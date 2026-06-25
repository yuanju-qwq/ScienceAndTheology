#include "sublimation_path_registry.hpp"

#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>

namespace science_and_theology::source_law {

static SublimationPathDef g_paths[static_cast<int>(SublimationPath::COUNT)];
static std::unordered_map<std::string, const OrganSkillDef*> g_skill_map;

// Persistent storage for const char* fields coming from GDScript.
// std::deque keeps references to existing elements stable when appending,
// so c_str() pointers handed to the defs remain valid across registrations.
static std::deque<std::string> g_string_storage;

// Returns a stable const char* for the given source string. Empty strings
// map to the shared empty literal so we don't allocate storage for them.
static const char* store_string(const char* src) {
    if (src == nullptr || src[0] == '\0') return "";
    g_string_storage.emplace_back(src);
    return g_string_storage.back().c_str();
}

void SublimationPathRegistry::initialize() {
    g_skill_map.clear();
    g_string_storage.clear();
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

    SublimationPathDef stored = def;
    stored.id = store_string(def.id);
    stored.title_key = store_string(def.title_key);

    stored.organ_stages.clear();
    stored.organ_stages.reserve(def.organ_stages.size());
    for (const auto& stage : def.organ_stages) {
        PathOrganStage s = stage;
        s.organ_name = store_string(stage.organ_name);
        stored.organ_stages.push_back(s);
    }

    // Skills are registered separately via register_skill; reset the slot
    // so re-registering a path does not retain stale skills.
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

    OrganSkillDef stored = def;
    stored.id = store_string(def.id);
    stored.title_key = store_string(def.title_key);

    g_paths[idx].skills.push_back(stored);
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

void SublimationPathRegistry::register_builtin_paths() {
    // No-op: built-in paths are now registered from GDScript via
    // GDSublimationPathRegistry (see BuiltinSublimationPaths.gd).
}

} // namespace science_and_theology::source_law
