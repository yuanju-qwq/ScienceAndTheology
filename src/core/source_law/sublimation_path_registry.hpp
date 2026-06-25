#pragma once

#include <cstdint>
#include <vector>

#include "sublimation_path_def.hpp"

namespace science_and_theology::source_law {

// ============================================================
// SublimationPathRegistry — registry for all sublimation paths
// ============================================================
class SublimationPathRegistry {
public:
    static void initialize();

    // Register a sublimation path from GDScript. Stores the def's string
    // fields persistently and writes it into the slot indexed by
    // def.path_id. Returns false on invalid path_id.
    static bool register_path(const SublimationPathDef& def);

    // Register an organ skill from GDScript. Appends the skill to the
    // path indexed by def.required_path and refreshes the skill lookup
    // map. Returns false on invalid required_path.
    // 幂等：若 def.id 已存在则返回 true（已注册），不追加新条目。
    static bool register_skill(const OrganSkillDef& def);

    static const SublimationPathDef* get(SublimationPath path);
    static const OrganSkillDef* get_skill(const char* skill_id);

    // Get all skills available to a player with given organs.
    static std::vector<const OrganSkillDef*> get_available_skills(
        const OrganArray& organs);

    static size_t count();

    // 清空所有注册数据（技能表、字符串存储、路径槽位）。
    // 用于测试或热重载场景下的彻底复位。
    static void reset();

private:
    static void register_builtin_paths();
    static void rebuild_skill_map();
};

} // namespace science_and_theology::source_law
