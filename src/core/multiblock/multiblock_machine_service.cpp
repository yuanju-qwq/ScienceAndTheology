#include "multiblock_machine_service.hpp"

#include <algorithm>

#include "multiblock_controller.hpp"
#include "../world/block_entity_registry.hpp"
#include "../world/world_data.hpp"

namespace science_and_theology::multiblock {

namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void add_hatch_to_summary(HatchCapabilitySummary& summary, uint16_t mask) {
    summary.mask |= mask;
    if (mask & HATCH_ITEM_INPUT) ++summary.item_inputs;
    if (mask & HATCH_ITEM_OUTPUT) ++summary.item_outputs;
    if (mask & HATCH_FLUID_INPUT) ++summary.fluid_inputs;
    if (mask & HATCH_FLUID_OUTPUT) ++summary.fluid_outputs;
    if (mask & HATCH_ENERGY_INPUT) ++summary.energy_inputs;
    if (mask & HATCH_ENERGY_OUTPUT) ++summary.energy_outputs;
}

} // namespace

uint16_t MultiblockMachineService::infer_hatch_mask(
    const std::string& machine_type) {
    const std::string key = lower_copy(machine_type);
    uint16_t mask = 0;

    if (key.find("input_bus") != std::string::npos ||
        key.find("item_input") != std::string::npos ||
        key.find("input_item") != std::string::npos) {
        mask |= HATCH_ITEM_INPUT;
    }
    if (key.find("output_bus") != std::string::npos ||
        key.find("item_output") != std::string::npos ||
        key.find("output_item") != std::string::npos) {
        mask |= HATCH_ITEM_OUTPUT;
    }
    if (key.find("input_hatch") != std::string::npos ||
        key.find("fluid_input") != std::string::npos ||
        key.find("input_fluid") != std::string::npos) {
        mask |= HATCH_FLUID_INPUT;
    }
    if (key.find("output_hatch") != std::string::npos ||
        key.find("fluid_output") != std::string::npos ||
        key.find("output_fluid") != std::string::npos) {
        mask |= HATCH_FLUID_OUTPUT;
    }
    if (key.find("energy_input") != std::string::npos ||
        key.find("energy_hatch") != std::string::npos ||
        key.find("power_input") != std::string::npos) {
        mask |= HATCH_ENERGY_INPUT;
    }
    if (key.find("energy_output") != std::string::npos ||
        key.find("power_output") != std::string::npos) {
        mask |= HATCH_ENERGY_OUTPUT;
    }

    return mask;
}

HatchCapabilitySummary MultiblockMachineService::summarize_hatches(
    const BlockEntityRegistry& registry,
    EntityId machine_id) {
    HatchCapabilitySummary summary;
    const MachineBlockEntityState* machine = registry.get_machine_state(machine_id);
    if (machine == nullptr) return summary;

    for (EntityId hatch_id : machine->hatch_entities) {
        const MachineBlockEntityState* hatch = registry.get_machine_state(hatch_id);
        if (hatch == nullptr) continue;
        add_hatch_to_summary(summary, infer_hatch_mask(hatch->machine_type));
    }

    return summary;
}

MachineFormationUpdate MultiblockMachineService::check_controller(
    WorldData& world,
    EntityId machine_id) {
    MachineFormationUpdate update;
    update.machine_id = machine_id;

    auto& registry = world.block_entity_registry();
    if (registry.get_entity_type(machine_id) != BlockEntityType::MACHINE) {
        update.error = "entity is not a machine";
        return update;
    }

    StructureCheckResult result =
        MultiblockControllerBase::check_formation(world, registry, machine_id);
    update.checked = true;
    update.formed = result.matched;
    if (!result.matched) {
        update.error = result.mismatch_reason;
    }
    update.hatches = summarize_hatches(registry, machine_id);
    return update;
}

std::vector<MachineFormationUpdate> MultiblockMachineService::on_cell_changed(
    WorldData& world,
    const std::string& dimension_id,
    int32_t block_x, int32_t block_y, int32_t block_z) {
    std::vector<MachineFormationUpdate> updates;
    auto& registry = world.block_entity_registry();

    EntityId owner = registry.find_owner_at(block_x, block_y, block_z);
    if (owner.is_valid() && registry.get_entity_type(owner) == BlockEntityType::MACHINE) {
        updates.push_back(check_controller(world, owner));
    }

    EntityId root = registry.find_machine_root_at(block_x, block_y, block_z);
    if (root.is_valid() && root != owner) {
        updates.push_back(check_controller(world, root));
    }

    // A newly placed casing/hatch may be adjacent to a not-yet-formed controller,
    // which cannot be found through claimed-cell ownership. To keep placement
    // behavior deterministic without a full spatial acceleration structure, scan
    // machines in the same dimension and recheck nearby controllers. This is an
    // event path, not a tick path, and can later be replaced by a dirty index.
    for (const auto& pair : registry.all_entities()) {
        const EntityId id = pair.first;
        const auto& entry = pair.second;
        if (id == owner || id == root) continue;
        if (entry.dimension_id != dimension_id) continue;
        if (entry.placement.entity_type != BlockEntityType::MACHINE) continue;
        const int32_t dx = entry.placement.root_x - block_x;
        const int32_t dy = entry.placement.root_y - block_y;
        const int32_t dz = entry.placement.root_z - block_z;
        const int32_t manhattan = std::abs(dx) + std::abs(dy) + std::abs(dz);
        if (manhattan <= 8) {
            updates.push_back(check_controller(world, id));
        }
    }

    return updates;
}

bool MultiblockMachineService::can_tick_machine(
    const WorldData& world,
    EntityId machine_id) {
    const MachineBlockEntityState* state =
        world.block_entity_registry().get_machine_state(machine_id);
    if (state == nullptr) {
        return true;
    }
    return state->formed;
}

} // namespace science_and_theology::multiblock
