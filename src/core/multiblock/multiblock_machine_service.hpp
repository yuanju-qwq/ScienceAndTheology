#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "structure_element.hpp"
#include "structure_runtime.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

class WorldData;
class BlockEntityRegistry;

namespace multiblock {

struct HatchCapabilitySummary {
    uint16_t mask = 0;
    int item_inputs = 0;
    int item_outputs = 0;
    int fluid_inputs = 0;
    int fluid_outputs = 0;
    int energy_inputs = 0;
    int energy_outputs = 0;
};

struct MachineFormationUpdate {
    EntityId machine_id;
    bool checked = false;
    bool formed = false;
    std::string error;
    HatchCapabilitySummary hatches;
};

// Service layer that connects the GT-style pattern engine to world mutation
// events and machine ticking. This keeps the lower multiblock matcher pure and
// gives gameplay/server code one place to call after placement or block edits.
class MultiblockMachineService {
public:
    // Infer hatch ability from machine_type naming. This is intentionally data
    // light for now; later it should be replaced by MachineDefinition metadata.
    static uint16_t infer_hatch_mask(const std::string& machine_type);

    static HatchCapabilitySummary summarize_hatches(
        const BlockEntityRegistry& registry,
        EntityId machine_id);

    // Check a controller and commit the formed/unformed state to the registry.
    static MachineFormationUpdate check_controller(
        WorldData& world,
        EntityId machine_id);

    // Called after a block cell changes. Rechecks the owning formed machine, if
    // the changed cell was part of one, and also checks any machine rooted at
    // the changed cell.
    static std::vector<MachineFormationUpdate> on_cell_changed(
        WorldData& world,
        const std::string& dimension_id,
        int32_t block_x, int32_t block_y, int32_t block_z);

    // MachineSystem uses this before ticking a Machine*. If an EntityId has no
    // machine block entity state, allow it for backward compatibility.
    static bool can_tick_machine(const WorldData& world, EntityId machine_id);
};

} // namespace multiblock
} // namespace science_and_theology
