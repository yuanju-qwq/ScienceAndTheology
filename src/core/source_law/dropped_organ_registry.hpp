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
    // on failure, otherwise the assigned id.
    static DroppedOrganId register_organ(const DroppedOrganDef& def);

private:
    static void register_builtin_organs();
};

} // namespace science_and_theology::source_law
