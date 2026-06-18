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

    static const SublimationPathDef* get(SublimationPath path);
    static const OrganSkillDef* get_skill(const char* skill_id);

    // Get all skills available to a player with given organs.
    static std::vector<const OrganSkillDef*> get_available_skills(
        const OrganArray& organs);

    static size_t count();

private:
    static void register_builtin_paths();
};

} // namespace science_and_theology::source_law
