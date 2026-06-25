#pragma once

#include <cstdint>
#include <vector>

#include "dropped_organ_def.hpp"

namespace science_and_theology::source_law {

using DroppedOrganId = uint16_t;
inline constexpr DroppedOrganId kInvalidDroppedOrganId = 0xFFFF;

// ============================================================
// DroppedOrganRegistry — registry for all droppable source organs
// ============================================================
class DroppedOrganRegistry {
public:
    static void initialize();
    // 清空所有全局状态（不 reserve ID 0），用于测试或重新装载。
    static void reset();

    static const DroppedOrganDef* get_by_id(DroppedOrganId id);
    static const DroppedOrganDef* get_by_name(const char* name);

    // Find all dropped organs for a given source creature.
    static std::vector<DroppedOrganId> find_by_creature(const char* creature_id);

    // Find all dropped organs for a given target slot.
    static std::vector<DroppedOrganId> find_by_slot(OrganSlot slot);

    // Find all aberration-sourced organs that imitate a given path.
    static std::vector<DroppedOrganId> find_by_imitated_path(SublimationPath path);

    static size_t count();

    // Register a dropped organ from GDScript.  Returns kInvalidDroppedOrganId
    // on failure, otherwise the assigned id. Requires explicit_id
    // (不再支持自动分配).
    static DroppedOrganId register_organ(const DroppedOrganDef& def,
                                         DroppedOrganId explicit_id);

private:
    static void register_builtin_organs();
};

} // namespace science_and_theology::source_law
