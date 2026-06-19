#pragma once

// ============================================================
// GT V3 Multiblock System — Controller Adapter Layer
// ============================================================
//
// Ported from GregTech CEu's V3 MultiblockControllerBase +
// MultiblockStructureOperations + MultiblockStructureCommitter.
//
// In GT V3, MultiblockControllerBase is a base class that
// multiblock controllers extend. It holds a StructureRuntime,
// provides checkStructurePattern(), and calls formStructure()
// on success.
//
// In our system, controllers are data in MachineBlockEntityState
// (not polymorphic objects), so this adapter is a static utility
// that wraps the runtime + registry interaction:
//
//   1. register_definition(machine_type, factory)
//      — called at startup to register each machine's structure
//   2. check_formation(world, registry, machine_id)
//      — called when a machine is placed or a block changes
//   3. invalidate(registry, machine_id)
//      — called when a claimed cell is broken
//
// The adapter internally creates a StructureRuntime per check,
// calls runtime.check(), and on success updates the machine's
// formation state via registry.set_machine_formation().

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "structure_definition.hpp"
#include "structure_runtime.hpp"
#include "../world/entity_data.hpp"

namespace science_and_theology {

// Forward declarations.
class WorldData;
class BlockEntityRegistry;

namespace multiblock {

// ------------------------------------------------------------
// MultiblockControllerBase — static utility for formation checks.
//
// GT V3 equivalent: MultiblockControllerBase + 
// MultiblockStructureOperations + MultiblockStructureCommitter
// (collapsed into a single static utility since our controllers
//  are data, not polymorphic objects).
// ------------------------------------------------------------
class MultiblockControllerBase {
public:
    // Register a structure definition factory for a machine type.
    // The factory is called lazily on first check_formation() for
    // that machine type, and the result is cached via
    // StructureDefinition::get_or_build().
    static void register_definition(
        const std::string& machine_type,
        std::function<std::shared_ptr<StructureDefinition>()> factory);

    // Get the structure definition for a machine type.
    // Returns nullptr if the machine type has no registered definition.
    // A machine type with no definition is treated as trivial (always formed).
    static std::shared_ptr<StructureDefinition> get_definition(
        const std::string& machine_type);

    // Check whether a machine type has a registered definition.
    static bool has_definition(const std::string& machine_type);

    // Check formation for a machine entity.
    //
    // world:     provides terrain material queries.
    // registry:  block entity registry — looks up the machine + updates state.
    // machine_id: EntityId of the controller machine.
    //
    // Behavior:
    //   - Looks up the machine's EntityId in the registry.
    //   - If not a MACHINE entity, returns failure.
    //   - If machine_type has no registered definition, treats it as
    //     trivial: sets formed=true with no claimed cells.
    //   - Otherwise, creates a StructureRuntime and checks formation.
    //   - On success: calls registry.set_machine_formation() with
    //     claimed_cells and hatch_entities.
    //   - On failure: calls registry.set_machine_formation() with
    //     formed=false to clear any previous formation.
    //
    // Returns the check result.
    static StructureCheckResult check_formation(
        const WorldData& world,
        BlockEntityRegistry& registry,
        EntityId machine_id);

    // Invalidate a machine's formation.
    // Calls registry.set_machine_formation() with formed=false.
    static void invalidate(
        BlockEntityRegistry& registry,
        EntityId machine_id);

    // Clear all registered definition factories (for testing / hot-reload).
    static void clear_registry();

private:
    static std::unordered_map<std::string,
        std::function<std::shared_ptr<StructureDefinition>()>>& factories();
};

} // namespace multiblock
} // namespace science_and_theology
