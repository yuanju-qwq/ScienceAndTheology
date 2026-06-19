#include "multiblock_controller.hpp"

#include "../world/world_data.hpp"
#include "../world/block_entity_registry.hpp"

namespace science_and_theology::multiblock {

// ============================================================
// MultiblockControllerBase — factory registry
// ============================================================

std::unordered_map<std::string,
    std::function<std::shared_ptr<StructureDefinition>()>>&
MultiblockControllerBase::factories() {
    // Meyers singleton — initialized on first use.
    static std::unordered_map<std::string,
        std::function<std::shared_ptr<StructureDefinition>()>> map;
    return map;
}

void MultiblockControllerBase::register_definition(
    const std::string& machine_type,
    std::function<std::shared_ptr<StructureDefinition>()> factory) {
    factories()[machine_type] = std::move(factory);
}

std::shared_ptr<StructureDefinition>
MultiblockControllerBase::get_definition(const std::string& machine_type) {
    auto& f = factories();
    auto it = f.find(machine_type);
    if (it == f.end()) return nullptr;

    // Use the factory to get_or_build the cached definition.
    return StructureDefinition::get_or_build(machine_type, it->second);
}

bool MultiblockControllerBase::has_definition(const std::string& machine_type) {
    return factories().find(machine_type) != factories().end();
}

void MultiblockControllerBase::clear_registry() {
    factories().clear();
    StructureDefinition::clear_cache();
}

// ============================================================
// MultiblockControllerBase — formation check
// ============================================================

StructureCheckResult MultiblockControllerBase::check_formation(
    const WorldData& world,
    BlockEntityRegistry& registry,
    EntityId machine_id) {

    // Look up the machine entity.
    BlockEntityType type = registry.get_entity_type(machine_id);
    if (type != BlockEntityType::MACHINE) {
        return StructureCheckResult::failure(0, 0, 0,
            "entity is not a MACHINE");
    }

    const MachineBlockEntityState* state = registry.get_machine_state(machine_id);
    if (state == nullptr) {
        return StructureCheckResult::failure(0, 0, 0,
            "no machine state for entity");
    }

    const std::string machine_type = state->machine_type;
    const uint8_t facing = state->facing;

    // Get the controller's position.
    const BlockEntityPlacement* placement = registry.get_placement(machine_id);
    if (placement == nullptr) {
        return StructureCheckResult::failure(0, 0, 0,
            "no placement for entity");
    }

    const int32_t ctrl_x = placement->root_x;
    const int32_t ctrl_y = placement->root_y;
    const int32_t ctrl_z = placement->root_z;

    // Get the dimension.
    const std::string* dim_ptr = registry.get_dimension_id(machine_id);
    const std::string dimension_id = (dim_ptr != nullptr) ? *dim_ptr : "overworld";

    // Look up the structure definition.
    auto def = get_definition(machine_type);
    if (def == nullptr) {
        // No definition registered — trivial machine (always formed).
        registry.set_machine_formation(machine_id, true, {}, {});
        return StructureCheckResult::success({}, {});
    }

    // Create a runtime and check formation.
    StructureRuntime runtime(def);
    StructureCheckResult result = runtime.check(
        world, registry, machine_id, dimension_id,
        ctrl_x, ctrl_y, ctrl_z, facing);

    // Apply the result to the registry.
    if (result.matched) {
        registry.set_machine_formation(
            machine_id, true,
            result.claimed_cells,
            result.hatch_entities);
    } else {
        // Clear any previous formation.
        registry.set_machine_formation(machine_id, false, {}, {});
    }

    return result;
}

void MultiblockControllerBase::invalidate(
    BlockEntityRegistry& registry,
    EntityId machine_id) {
    registry.set_machine_formation(machine_id, false, {}, {});
}

} // namespace science_and_theology::multiblock
