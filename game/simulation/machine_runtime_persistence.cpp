// Chunk-anchored machine runtime persistence implementation.

#define SNT_LOG_CHANNEL "game.machine_persistence"
#include "game/simulation/machine_runtime_persistence.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/machine_tick_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

constexpr size_t kMaxMachineInputSlots = 64;
constexpr size_t kMaxMachineOutputSlots = 64;
constexpr size_t kMaxMachineFluidTanks = 16;
constexpr size_t kMaxMachineRecipeInputs = 64;
constexpr size_t kMaxMachineRecipeOutputs = 64;
constexpr size_t kMaxMachineAutomationWorkOrderOutputs = 64;
constexpr size_t kMaxMachineJobOwnerAccountBytes = 256;
constexpr size_t kMaxMachineAutomationRecipeIdBytes = 256;
constexpr int32_t kMaxMachineStackSize = 1'000'000;
constexpr int64_t kMaxMachineFluidTankCapacity = 1'000'000'000'000LL;
constexpr int32_t kMaxMachineRuntimeTicks = 1'000'000'000;
constexpr uint64_t kMachineAnchorIdFlag = uint64_t{1} << 62u;
constexpr uint64_t kMachineAnchorSerialMask = kMachineAnchorIdFlag - 1u;

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] int floor_div_chunk(int32_t block_coordinate) {
    constexpr int64_t kChunkSize = snt::voxel::VoxelChunk::kChunkSize;
    const int64_t value = block_coordinate;
    return static_cast<int>(value >= 0 ? value / kChunkSize
                                       : -((-value + kChunkSize - 1) / kChunkSize));
}

[[nodiscard]] std::string describe_chunk(const ChunkKey& key) {
    return key.dimension_id + " (" + std::to_string(key.chunk_x) + "," +
           std::to_string(key.chunk_y) + "," + std::to_string(key.chunk_z) + ")";
}

[[nodiscard]] const BlockEntityPlacement* find_machine_anchor(
    const GameChunkSidecar& sidecar,
    EntityId anchor_entity_id) {
    const BlockEntityPlacement* found = nullptr;
    for (const BlockEntityPlacement& placement : sidecar.block_entities) {
        if (placement.id != anchor_entity_id) continue;
        if (found != nullptr) return nullptr;
        found = &placement;
    }
    return found;
}

[[nodiscard]] snt::core::Expected<EntityId> allocate_machine_anchor_id(
    const GameChunkSidecarRegistry& sidecars) {
    std::unordered_set<uint64_t> occupied_ids;
    uint64_t greatest_serial = 0;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const BlockEntityPlacement& placement : sidecar.block_entities) {
            occupied_ids.insert(placement.id.id);
            if ((placement.id.id & kMachineAnchorIdFlag) == 0 ||
                (placement.id.id & (uint64_t{1} << 63u)) != 0) {
                continue;
            }
            greatest_serial = std::max(greatest_serial,
                                       placement.id.id & kMachineAnchorSerialMask);
        }
    });
    if (greatest_serial >= kMachineAnchorSerialMask) {
        return invalid_state("Machine persistence exhausted reserved machine anchor ids");
    }

    for (uint64_t serial = greatest_serial + 1u; serial <= kMachineAnchorSerialMask;
         ++serial) {
        const uint64_t candidate = kMachineAnchorIdFlag | serial;
        if (!occupied_ids.contains(candidate)) return EntityId{candidate};
    }
    return invalid_state("Machine persistence exhausted reserved machine anchor ids");
}

[[nodiscard]] snt::core::Expected<void> validate_anchor(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    EntityId anchor_entity_id) {
    if (!anchor_entity_id.is_valid()) {
        return invalid_argument("Machine persistence anchor entity id must be non-zero");
    }
    const BlockEntityPlacement* anchor = find_machine_anchor(sidecar, anchor_entity_id);
    if (anchor == nullptr) {
        return invalid_state("Machine persistence anchor is missing or duplicated in chunk " +
                             describe_chunk(chunk_key));
    }
    if (anchor->entity_type != BlockEntityType::MACHINE) {
        return invalid_state("Machine persistence anchor is not a MACHINE block entity in chunk " +
                             describe_chunk(chunk_key));
    }
    if (floor_div_chunk(anchor->root_x) != chunk_key.chunk_x ||
        floor_div_chunk(anchor->root_y) != chunk_key.chunk_y ||
        floor_div_chunk(anchor->root_z) != chunk_key.chunk_z) {
        return invalid_state("Machine persistence anchor root is not owned by chunk " +
                             describe_chunk(chunk_key));
    }
    return {};
}

[[nodiscard]] bool is_valid_stack(const ResourceContentStack& stack,
                                  bool allow_empty) {
    if (stack.amount < 0 || stack.amount > kMaxMachineStackSize) {
        return false;
    }
    if (stack.amount == 0) {
        return allow_empty && stack.key.type.empty() && stack.key.id.empty() &&
               stack.key.variant.empty();
    }
    return stack.is_valid();
}

[[nodiscard]] snt::core::Expected<void> validate_recipe(
    const MachineRuntimeRecipeSnapshot& recipe) {
    if (recipe.id.empty() || recipe.inputs.empty() ||
        recipe.inputs.size() > kMaxMachineRecipeInputs ||
        recipe.duration_ticks <= 0 || recipe.duration_ticks > kMaxMachineRuntimeTicks ||
        recipe.energy_per_tick < 0 || recipe.outputs.empty() ||
        recipe.outputs.size() > kMaxMachineRecipeOutputs) {
        return invalid_argument("Machine persistence recipe snapshot is invalid");
    }
    std::unordered_set<ResourceContentKey, ResourceContentKey::Hash> input_keys;
    for (const ResourceContentStack& input : recipe.inputs) {
        if (!is_valid_stack(input, false) ||
            !input_keys.insert(input.key).second) {
            return invalid_argument("Machine persistence recipe input is invalid");
        }
    }
    for (const ResourceContentStack& output : recipe.outputs) {
        if (!is_valid_stack(output, false)) {
            return invalid_argument("Machine persistence recipe output is invalid");
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_automation_work_order(
    const MachineAutomationWorkOrderRecord& work_order,
    const std::optional<MachineRuntimeRecipeSnapshot>& active_recipe) {
    const uint8_t state = static_cast<uint8_t>(work_order.state);
    if (!work_order.identity.is_valid() || work_order.recipe_id.empty() ||
        work_order.recipe_id.size() > kMaxMachineAutomationRecipeIdBytes ||
        work_order.recipe_id.find('\0') != std::string::npos ||
        work_order.expected_outputs.empty() ||
        work_order.expected_outputs.size() > kMaxMachineAutomationWorkOrderOutputs ||
        state > static_cast<uint8_t>(MachineAutomationWorkOrderState::kFailed)) {
        return invalid_argument("Machine automation work order is invalid");
    }
    for (const ResourceContentStack& output : work_order.expected_outputs) {
        if (!is_valid_stack(output, false)) {
            return invalid_argument("Machine automation work order output is invalid");
        }
    }
    if (work_order.state == MachineAutomationWorkOrderState::kRunning &&
        (!active_recipe || active_recipe->id != work_order.recipe_id)) {
        return invalid_state("Running machine automation work order has no matching recipe");
    }
    if (work_order.state == MachineAutomationWorkOrderState::kOutputReady && active_recipe) {
        return invalid_state("Completed machine automation work order still has an active recipe");
    }
    return {};
}

[[nodiscard]] snt::core::Expected<void> validate_record(
    const ChunkKey& chunk_key,
    const GameChunkSidecar& sidecar,
    const MachineRuntimePersistenceRecord& record) {
    if (auto result = validate_anchor(chunk_key, sidecar, record.anchor_entity_id); !result) {
        return result.error();
    }
    if (record.entity_guid == 0 || record.machine_id.empty() ||
        record.input_slots.size() > kMaxMachineInputSlots ||
        record.output_slots.size() > kMaxMachineOutputSlots ||
        record.fluid_tanks.size() > kMaxMachineFluidTanks ||
        record.stored_energy < 0 || record.energy_capacity < 0 ||
        record.max_input_slots <= 0 ||
        record.max_input_slots > static_cast<int32_t>(kMaxMachineInputSlots) ||
        record.max_output_slots <= 0 ||
        record.max_output_slots > static_cast<int32_t>(kMaxMachineOutputSlots) ||
        record.max_stack_size <= 0 || record.max_stack_size > kMaxMachineStackSize ||
        record.progress_ticks < 0 || record.progress_ticks > kMaxMachineRuntimeTicks ||
        record.run_state > static_cast<uint8_t>(MachineRunState::WaitingForOutput)) {
        return invalid_argument("Machine persistence record is invalid in chunk " +
                                describe_chunk(chunk_key));
    }
    const uint8_t residency = static_cast<uint8_t>(record.residency);
    if (residency > static_cast<uint8_t>(MachineRuntimeResidency::kOfflineNetworkIsland)) {
        return invalid_argument("Machine persistence residency is invalid in chunk " +
                                describe_chunk(chunk_key));
    }
    if (record.residency != MachineRuntimeResidency::kOfflineNetworkIsland &&
        record.offline_island_id != 0) {
        return invalid_argument("Only offline network machines may reference a network island");
    }
    if (record.residency == MachineRuntimeResidency::kOfflineNetworkIsland &&
        (record.offline_island_id == 0 || record.offline_epoch == 0)) {
        return invalid_argument("Offline network machine is missing island ownership metadata");
    }
    if (record.input_slots.size() > static_cast<size_t>(record.max_input_slots)) {
        return invalid_argument("Machine persistence input slot count exceeds its configured limit");
    }
    if (record.output_slots.size() > static_cast<size_t>(record.max_output_slots)) {
        return invalid_argument("Machine persistence output slot count exceeds its configured limit");
    }
    for (const ResourceContentStack& input : record.input_slots) {
        if (!is_valid_stack(input, false) ||
            input.amount > record.max_stack_size) {
            return invalid_argument("Machine persistence input slot is invalid");
        }
    }
    for (const ResourceContentStack& output : record.output_slots) {
        if (!is_valid_stack(output, false) ||
            output.amount > record.max_stack_size) {
            return invalid_argument("Machine persistence output slot is invalid");
        }
    }
    for (const MachineFluidTankRecord& tank : record.fluid_tanks) {
        if (!tank.is_valid() || tank.capacity_millibuckets > kMaxMachineFluidTankCapacity) {
            return invalid_argument("Machine persistence fluid tank is invalid");
        }
    }
    if (record.activation_requested &&
        (record.active_recipe ||
         record.run_state != static_cast<uint8_t>(MachineRunState::WaitingForActivation))) {
        return invalid_argument("Machine persistence activation request is not waiting for activation");
    }
    if (record.job_owner_account_id.size() > kMaxMachineJobOwnerAccountBytes ||
        record.job_owner_account_id.find('\0') != std::string::npos ||
        (!record.job_owner_account_id.empty() &&
         !record.activation_requested && !record.active_recipe)) {
        return invalid_argument("Machine persistence job owner is invalid");
    }
    if (record.active_recipe) {
        if (auto result = validate_recipe(*record.active_recipe); !result) return result.error();
    } else if (record.progress_ticks != 0) {
        return invalid_argument("Machine persistence progress requires an active recipe snapshot");
    }
    if (record.automation_work_order) {
        if (auto result = validate_automation_work_order(*record.automation_work_order,
                                                         record.active_recipe);
            !result) {
            return result.error();
        }
    }
    return {};
}

[[nodiscard]] snt::core::Expected<ResourceContentStack> to_persisted_stack(
    const ResourceStack& stack,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!stack.is_valid()) {
        return invalid_state("Machine runtime cannot persist an invalid compact resource stack");
    }
    const auto content = resolve_content_stack(stack, resource_runtime_index);
    if (!content || !content->is_valid()) {
        return invalid_state("Machine runtime compact resource key is absent from its snapshot");
    }
    return *content;
}

[[nodiscard]] snt::core::Expected<ResourceStack> to_runtime_stack(
    const ResourceContentStack& stack,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!stack.is_valid()) {
        return invalid_state("Machine persistence contains an invalid content resource stack");
    }
    const auto runtime = resolve_resource_stack(stack, resource_runtime_index);
    if (!runtime || !runtime->is_valid()) {
        return invalid_state("Machine persistence refers to a resource missing from current content: " +
                             stack.key.id);
    }
    return *runtime;
}

[[nodiscard]] snt::core::Expected<MachineFluidTankRecord> to_persisted_tank(
    const MachineFluidTank& tank,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!tank.is_valid()) {
        return invalid_state("Machine runtime fluid tank is invalid");
    }
    MachineFluidTankRecord result{
        .capacity_millibuckets = tank.capacity_millibuckets,
        .temperature_kelvin = tank.temperature_kelvin,
        .pressure_pascal = tank.pressure_pascal,
        .transport = tank.transport,
        .access = tank.access,
    };
    if (tank.fluid.is_absent()) return result;

    auto persisted = to_persisted_stack(tank.fluid, resource_runtime_index);
    if (!persisted) return persisted.error();
    if (!persisted->is_fluid()) {
        return invalid_state("Machine runtime fluid tank holds a non-fluid resource");
    }
    result.fluid = std::move(*persisted);
    return result;
}

[[nodiscard]] snt::core::Expected<MachineFluidTank> to_runtime_tank(
    const MachineFluidTankRecord& tank,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    if (!tank.is_valid()) {
        return invalid_state("Machine persistence fluid tank is invalid");
    }
    MachineFluidTank result{
        .capacity_millibuckets = tank.capacity_millibuckets,
        .temperature_kelvin = tank.temperature_kelvin,
        .pressure_pascal = tank.pressure_pascal,
        .transport = tank.transport,
        .access = tank.access,
    };
    if (tank.fluid.is_absent()) return result;

    auto runtime = to_runtime_stack(tank.fluid, resource_runtime_index);
    if (!runtime) return runtime.error();
    result.fluid = *runtime;
    return result;
}

[[nodiscard]] snt::core::Expected<MachineRuntimeRecipeSnapshot> to_persisted_recipe(
    const MachineRecipeSnapshot& recipe,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    MachineRuntimeRecipeSnapshot result;
    result.id = recipe.id;
    result.inputs.reserve(recipe.inputs.size());
    for (const ResourceStack& input : recipe.inputs) {
        auto persisted = to_persisted_stack(input, resource_runtime_index);
        if (!persisted) return persisted.error();
        result.inputs.push_back(std::move(*persisted));
    }
    result.duration_ticks = recipe.duration_ticks;
    result.energy_per_tick = recipe.energy_per_tick;
    result.outputs.reserve(recipe.outputs.size());
    for (const ResourceStack& output : recipe.outputs) {
        auto persisted = to_persisted_stack(output, resource_runtime_index);
        if (!persisted) return persisted.error();
        result.outputs.push_back(std::move(*persisted));
    }
    return result;
}

[[nodiscard]] snt::core::Expected<MachineRecipeSnapshot> to_runtime_recipe(
    const MachineRuntimeRecipeSnapshot& recipe,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    MachineRecipeSnapshot result;
    result.id = recipe.id;
    result.inputs.reserve(recipe.inputs.size());
    for (const ResourceContentStack& input : recipe.inputs) {
        auto runtime = to_runtime_stack(input, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.inputs.push_back(*runtime);
    }
    result.duration_ticks = recipe.duration_ticks;
    result.energy_per_tick = recipe.energy_per_tick;
    result.outputs.reserve(recipe.outputs.size());
    for (const ResourceContentStack& output : recipe.outputs) {
        auto runtime = to_runtime_stack(output, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.outputs.push_back(*runtime);
    }
    return result;
}

[[nodiscard]] snt::core::Expected<MachineAutomationWorkOrderRecord>
to_persisted_automation_work_order(
    const MachineAutomationWorkOrder& work_order,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    MachineAutomationWorkOrderRecord result{
        .identity = work_order.identity,
        .recipe_id = work_order.recipe_id,
        .state = work_order.state,
    };
    result.expected_outputs.reserve(work_order.expected_outputs.size());
    for (const ResourceStack& output : work_order.expected_outputs) {
        auto persisted = to_persisted_stack(output, resource_runtime_index);
        if (!persisted) return persisted.error();
        result.expected_outputs.push_back(std::move(*persisted));
    }
    return result;
}

[[nodiscard]] snt::core::Expected<MachineAutomationWorkOrder>
to_runtime_automation_work_order(
    const MachineAutomationWorkOrderRecord& work_order,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    MachineAutomationWorkOrder result{
        .identity = work_order.identity,
        .recipe_id = work_order.recipe_id,
        .state = work_order.state,
    };
    result.expected_outputs.reserve(work_order.expected_outputs.size());
    for (const ResourceContentStack& output : work_order.expected_outputs) {
        auto runtime = to_runtime_stack(output, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.expected_outputs.push_back(*runtime);
    }
    return result;
}

[[nodiscard]] snt::core::Expected<void> copy_runtime_fields(
    MachineRuntimePersistenceRecord& record,
    const MachineRuntimeComponent& runtime) {
    if (!runtime.resource_runtime_index.key_context().is_valid()) {
        return invalid_state("Machine runtime cannot persist without a resource snapshot");
    }
    MachineRuntimePersistenceRecord updated = record;
    updated.machine_id = runtime.machine_id;
    updated.input_slots.clear();
    updated.input_slots.reserve(runtime.input_slots.size());
    for (const ResourceStack& input : runtime.input_slots) {
        auto persisted = to_persisted_stack(input, runtime.resource_runtime_index);
        if (!persisted) return persisted.error();
        updated.input_slots.push_back(std::move(*persisted));
    }
    updated.output_slots.clear();
    updated.output_slots.reserve(runtime.output_slots.size());
    for (const ResourceStack& output : runtime.output_slots) {
        auto persisted = to_persisted_stack(output, runtime.resource_runtime_index);
        if (!persisted) return persisted.error();
        updated.output_slots.push_back(std::move(*persisted));
    }
    updated.fluid_tanks.clear();
    updated.fluid_tanks.reserve(runtime.fluid_tanks.size());
    for (const MachineFluidTank& tank : runtime.fluid_tanks) {
        auto persisted = to_persisted_tank(tank, runtime.resource_runtime_index);
        if (!persisted) return persisted.error();
        updated.fluid_tanks.push_back(std::move(*persisted));
    }
    updated.stored_energy = runtime.stored_energy;
    updated.energy_capacity = runtime.energy_capacity;
    updated.max_input_slots = runtime.max_input_slots;
    updated.max_output_slots = runtime.max_output_slots;
    updated.max_stack_size = runtime.max_stack_size;
    updated.progress_ticks = runtime.progress_ticks;
    updated.active_recipe.reset();
    if (runtime.active_recipe) {
        auto persisted_recipe = to_persisted_recipe(
            *runtime.active_recipe, runtime.resource_runtime_index);
        if (!persisted_recipe) return persisted_recipe.error();
        updated.active_recipe = std::move(*persisted_recipe);
    }
    updated.automation_work_order.reset();
    if (runtime.automation_work_order) {
        auto persisted_work_order = to_persisted_automation_work_order(
            *runtime.automation_work_order, runtime.resource_runtime_index);
        if (!persisted_work_order) return persisted_work_order.error();
        updated.automation_work_order = std::move(*persisted_work_order);
    }
    updated.activation_requested = runtime.activation_requested;
    updated.job_owner_account_id = runtime.job_owner_account_id;
    updated.run_state = static_cast<uint8_t>(runtime.state);
    record = std::move(updated);
    return {};
}

[[nodiscard]] snt::core::Expected<MachineRuntimePersistenceRecord> make_record(
    EntityId anchor_entity_id,
    snt::ecs::EntityGuid entity_guid,
    const MachineRuntimeComponent& runtime) {
    MachineRuntimePersistenceRecord result;
    result.anchor_entity_id = anchor_entity_id;
    result.entity_guid = entity_guid.value;
    if (auto copied = copy_runtime_fields(result, runtime); !copied) {
        return copied.error();
    }
    return result;
}

[[nodiscard]] snt::core::Expected<MachineRuntimeComponent> make_runtime(
    const MachineRuntimePersistenceRecord& record,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    MachineRuntimeComponent result;
    result.machine_id = record.machine_id;
    result.resource_runtime_index = resource_runtime_index;
    result.input_slots.reserve(record.input_slots.size());
    for (const ResourceContentStack& input : record.input_slots) {
        auto runtime = to_runtime_stack(input, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.input_slots.push_back(*runtime);
    }
    result.output_slots.reserve(record.output_slots.size());
    for (const ResourceContentStack& output : record.output_slots) {
        auto runtime = to_runtime_stack(output, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.output_slots.push_back(*runtime);
    }
    result.fluid_tanks.reserve(record.fluid_tanks.size());
    for (const MachineFluidTankRecord& tank : record.fluid_tanks) {
        auto runtime = to_runtime_tank(tank, resource_runtime_index);
        if (!runtime) return runtime.error();
        result.fluid_tanks.push_back(std::move(*runtime));
    }
    result.stored_energy = record.stored_energy;
    result.energy_capacity = record.energy_capacity;
    result.max_input_slots = record.max_input_slots;
    result.max_output_slots = record.max_output_slots;
    result.max_stack_size = record.max_stack_size;
    result.progress_ticks = record.progress_ticks;
    if (record.active_recipe) {
        auto runtime_recipe = to_runtime_recipe(*record.active_recipe, resource_runtime_index);
        if (!runtime_recipe) return runtime_recipe.error();
        result.active_recipe = std::move(*runtime_recipe);
    }
    if (record.automation_work_order) {
        auto runtime_work_order = to_runtime_automation_work_order(
            *record.automation_work_order, resource_runtime_index);
        if (!runtime_work_order) return runtime_work_order.error();
        result.automation_work_order = std::move(*runtime_work_order);
    }
    result.activation_requested = record.activation_requested;
    result.job_owner_account_id = record.job_owner_account_id;
    result.state = static_cast<MachineRunState>(record.run_state);
    return result;
}

struct RecordLocation {
    const ChunkKey* chunk_key = nullptr;
    GameChunkSidecar* sidecar = nullptr;
    size_t record_index = 0;
};

[[nodiscard]] std::vector<RecordLocation> collect_record_locations(
    GameChunkSidecarRegistry& sidecars) {
    std::vector<RecordLocation> locations;
    sidecars.for_each([&locations](const ChunkKey& key, GameChunkSidecar& sidecar) {
        for (size_t index = 0; index < sidecar.machine_runtime_records.size(); ++index) {
            locations.push_back({&key, &sidecar, index});
        }
    });
    return locations;
}

}  // namespace

snt::core::Expected<MachineAnchoredRuntime>
GameMachineRuntimePersistence::create_anchored_machine(
    snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key,
    int32_t root_x,
    int32_t root_y,
    int32_t root_z,
    MachineRuntimeComponent runtime) {
    GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot create an anchored machine without its chunk sidecar");
    }
    auto anchor_entity_id = allocate_machine_anchor_id(sidecars);
    if (!anchor_entity_id) return anchor_entity_id.error();
    if (!runtime.resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("Cannot create an anchored machine without a resource snapshot");
    }

    sidecar->block_entities.push_back({
        .id = *anchor_entity_id,
        .entity_type = BlockEntityType::MACHINE,
        .root_x = root_x,
        .root_y = root_y,
        .root_z = root_z,
        .owned_cell_count = 1,
    });
    if (auto result = validate_anchor(chunk_key, *sidecar, *anchor_entity_id); !result) {
        sidecar->block_entities.pop_back();
        return result.error();
    }

    const entt::entity entity = world.create_entity();
    const snt::ecs::EntityGuid entity_guid = world.guid_of(entity);
    auto record = make_record(*anchor_entity_id, entity_guid, runtime);
    if (!record) {
        world.destroy_entity(entity);
        sidecar->block_entities.pop_back();
        return record.error();
    }
    if (auto result = validate_record(chunk_key, *sidecar, *record); !result) {
        world.destroy_entity(entity);
        sidecar->block_entities.pop_back();
        return result.error();
    }
    try {
        world.add_component<MachineRuntimeComponent>(entity, std::move(runtime));
        sidecar->machine_runtime_records.push_back(std::move(*record));
    } catch (const std::exception& error) {
        world.destroy_entity(entity);
        sidecar->block_entities.pop_back();
        return invalid_state("Anchored machine creation failed: " + std::string(error.what()));
    } catch (...) {
        world.destroy_entity(entity);
        sidecar->block_entities.pop_back();
        return invalid_state("Anchored machine creation failed with an unknown error");
    }
    return MachineAnchoredRuntime{
        .anchor_entity_id = *anchor_entity_id,
        .entity_guid = entity_guid,
    };
}

snt::core::Expected<void> GameMachineRuntimePersistence::remove_anchored_machine(
    snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars,
    snt::ecs::EntityGuid entity_guid) {
    if (!entity_guid.valid()) {
        return invalid_argument("Cannot remove a machine with an invalid EntityGuid");
    }

    RecordLocation located;
    size_t match_count = 0;
    sidecars.for_each([&](const ChunkKey& key, GameChunkSidecar& sidecar) {
        for (size_t index = 0; index < sidecar.machine_runtime_records.size(); ++index) {
            if (sidecar.machine_runtime_records[index].entity_guid != entity_guid.value) continue;
            ++match_count;
            located = {&key, &sidecar, index};
        }
    });
    if (match_count != 1) {
        return invalid_state("Machine EntityGuid does not map to exactly one anchored runtime record");
    }

    const EntityId anchor_entity_id =
        located.sidecar->machine_runtime_records[located.record_index].anchor_entity_id;
    bool has_provider_binding = false;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        has_provider_binding = has_provider_binding || std::any_of(
            sidecar.ae_machine_pattern_provider_records.begin(),
            sidecar.ae_machine_pattern_provider_records.end(),
            [anchor_entity_id](const AeMachinePatternProviderPersistenceRecord& binding) {
                return binding.machine_anchor_entity_id == anchor_entity_id;
            });
    });
    if (has_provider_binding) {
        return invalid_state(
            "Cannot remove a machine while an AE interface owns its pattern-provider binding");
    }
    if (auto result = validate_anchor(*located.chunk_key, *located.sidecar, anchor_entity_id); !result) {
        return result.error();
    }
    const auto anchor = std::find_if(
        located.sidecar->block_entities.begin(), located.sidecar->block_entities.end(),
        [anchor_entity_id](const BlockEntityPlacement& placement) {
            return placement.id == anchor_entity_id;
        });
    if (anchor == located.sidecar->block_entities.end()) {
        return invalid_state("Anchored machine record has no removable MACHINE block entity");
    }

    const entt::entity entity = world.find_entity_by_guid(entity_guid);
    if (entity == entt::null ||
        !world.registry().all_of<MachineRuntimeComponent>(entity)) {
        return invalid_state("Anchored machine runtime entity is unavailable during removal");
    }

    world.destroy_entity(entity);
    auto& records = located.sidecar->machine_runtime_records;
    records.erase(records.begin() + static_cast<std::ptrdiff_t>(located.record_index));
    located.sidecar->block_entities.erase(anchor);
    return {};
}

snt::core::Expected<void> GameMachineRuntimePersistence::restore(
    snt::ecs::World& world,
    const GameChunkSidecarRegistry& sidecars,
    ResourceRuntimeIndex::Snapshot resource_runtime_index) {
    if (!resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("Cannot restore machine runtimes without a resource snapshot");
    }
    std::vector<const MachineRuntimePersistenceRecord*> records;
    std::unordered_set<uint64_t> seen_guids;
    std::unordered_set<uint64_t> seen_anchors;
    std::unordered_set<uint64_t> loaded_guids;
    std::optional<snt::core::Error> error;

    sidecars.for_each([&](const ChunkKey& key, const GameChunkSidecar& sidecar) {
        if (error.has_value()) return;
        for (const MachineRuntimePersistenceRecord& record : sidecar.machine_runtime_records) {
            if (auto result = validate_record(key, sidecar, record); !result) {
                error = result.error();
                return;
            }
            if (!seen_guids.insert(record.entity_guid).second) {
                error = invalid_state("Duplicate persisted machine EntityGuid across chunk sidecars");
                return;
            }
            if (!seen_anchors.insert(record.anchor_entity_id.id).second) {
                error = invalid_state("Duplicate persisted machine block-entity anchor across chunk sidecars");
                return;
            }
            if (record.residency != MachineRuntimeResidency::kLoaded) {
                continue;
            }
            if (world.find_entity_by_guid(snt::ecs::EntityGuid{record.entity_guid}) != entt::null) {
                error = invalid_state("Persisted machine EntityGuid collides with an existing ECS entity");
                return;
            }
            records.push_back(&record);
        }
    });
    if (error.has_value()) return *error;

    std::vector<size_t> order(records.size());
    for (size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&records](size_t lhs, size_t rhs) {
        return records[lhs]->entity_guid < records[rhs]->entity_guid;
    });

    std::vector<entt::entity> created;
    created.reserve(records.size());
    for (const size_t index : order) {
        const MachineRuntimePersistenceRecord& record = *records[index];
        auto runtime = make_runtime(record, resource_runtime_index);
        if (!runtime) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return runtime.error();
        }
        const entt::entity entity = world.create_entity_with_guid(
            snt::ecs::EntityGuid{record.entity_guid});
        if (entity == entt::null) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to recreate persisted machine EntityGuid");
        }
        try {
            world.add_component<MachineRuntimeComponent>(entity, std::move(*runtime));
            created.push_back(entity);
        } catch (const std::exception& exception) {
            world.destroy_entity(entity);
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to restore persisted machine runtime: " +
                                 std::string(exception.what()));
        } catch (...) {
            world.destroy_entity(entity);
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to restore persisted machine runtime");
        }
    }

    if (!records.empty()) {
        SNT_LOG_INFO("Restored %zu chunk-anchored machine runtime record(s)", records.size());
    }
    return {};
}

snt::core::Expected<void> GameMachineRuntimePersistence::restore_chunk(
    snt::ecs::World& world,
    const GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key,
    ResourceRuntimeIndex::Snapshot resource_runtime_index) {
    if (!resource_runtime_index.key_context().is_valid()) {
        return invalid_argument("Cannot restore chunk machine runtimes without a resource snapshot");
    }
    const GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot restore machine runtimes without a chunk sidecar");
    }

    std::vector<const MachineRuntimePersistenceRecord*> records;
    std::unordered_set<uint64_t> seen_guids;
    std::unordered_set<uint64_t> seen_anchors;
    for (const MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
        if (auto result = validate_record(chunk_key, *sidecar, record); !result) {
            return result.error();
        }
        if (!seen_guids.insert(record.entity_guid).second ||
            !seen_anchors.insert(record.anchor_entity_id.id).second) {
            return invalid_state("Chunk has duplicate persisted machine identities");
        }
        if (record.residency != MachineRuntimeResidency::kLoaded) continue;
        if (world.find_entity_by_guid(snt::ecs::EntityGuid{record.entity_guid}) != entt::null) {
            return invalid_state("Chunk machine EntityGuid collides with an existing ECS entity");
        }
        records.push_back(&record);
    }

    std::sort(records.begin(), records.end(), [](const auto* left, const auto* right) {
        return left->entity_guid < right->entity_guid;
    });
    std::vector<entt::entity> created;
    created.reserve(records.size());
    for (const MachineRuntimePersistenceRecord* record : records) {
        auto runtime = make_runtime(*record, resource_runtime_index);
        if (!runtime) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return runtime.error();
        }
        const entt::entity entity = world.create_entity_with_guid(
            snt::ecs::EntityGuid{record->entity_guid});
        if (entity == entt::null) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to recreate a chunk machine EntityGuid");
        }
        try {
            world.add_component<MachineRuntimeComponent>(
                entity, std::move(*runtime));
            created.push_back(entity);
        } catch (const std::exception& exception) {
            world.destroy_entity(entity);
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to restore a chunk machine runtime: " +
                                 std::string(exception.what()));
        } catch (...) {
            world.destroy_entity(entity);
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to restore a chunk machine runtime");
        }
    }
    return {};
}

snt::core::Expected<void> GameMachineRuntimePersistence::capture_chunk(
    const snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key) {
    GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot capture machine runtimes without a chunk sidecar");
    }

    std::unordered_set<uint64_t> seen_guids;
    std::unordered_set<uint64_t> seen_anchors;
    std::vector<std::pair<size_t, MachineRuntimePersistenceRecord>> updates;
    for (size_t index = 0; index < sidecar->machine_runtime_records.size(); ++index) {
        const MachineRuntimePersistenceRecord& record = sidecar->machine_runtime_records[index];
        if (auto result = validate_record(chunk_key, *sidecar, record); !result) {
            return result.error();
        }
        if (!seen_guids.insert(record.entity_guid).second ||
            !seen_anchors.insert(record.anchor_entity_id.id).second) {
            return invalid_state("Chunk has duplicate persisted machine identities");
        }
        if (record.residency != MachineRuntimeResidency::kLoaded) continue;

        const snt::ecs::EntityGuid entity_guid{record.entity_guid};
        const entt::entity entity = world.find_entity_by_guid(entity_guid);
        if (entity == entt::null ||
            !world.registry().all_of<MachineRuntimeComponent>(entity)) {
            return invalid_state("Chunk machine runtime is unavailable during capture");
        }
        MachineRuntimePersistenceRecord updated = record;
        if (auto result = copy_runtime_to_record(
                updated, world.get_component<MachineRuntimeComponent>(entity)); !result) {
            return result.error();
        }
        if (auto result = validate_record(chunk_key, *sidecar, updated); !result) {
            return result.error();
        }
        updates.emplace_back(index, std::move(updated));
    }
    for (auto& [index, record] : updates) {
        sidecar->machine_runtime_records[index] = std::move(record);
    }
    return {};
}

snt::core::Expected<void> GameMachineRuntimePersistence::destroy_chunk_runtimes(
    snt::ecs::World& world,
    const GameChunkSidecarRegistry& sidecars,
    const ChunkKey& chunk_key) {
    const GameChunkSidecar* sidecar = sidecars.get(chunk_key);
    if (sidecar == nullptr) {
        return invalid_state("Cannot destroy machine runtimes without a chunk sidecar");
    }

    std::vector<entt::entity> entities;
    std::unordered_set<uint64_t> seen_guids;
    for (const MachineRuntimePersistenceRecord& record : sidecar->machine_runtime_records) {
        if (auto result = validate_record(chunk_key, *sidecar, record); !result) {
            return result.error();
        }
        if (!seen_guids.insert(record.entity_guid).second) {
            return invalid_state("Chunk has duplicate persisted machine EntityGuids");
        }
        if (record.residency != MachineRuntimeResidency::kLoaded) continue;
        const entt::entity entity = world.find_entity_by_guid(
            snt::ecs::EntityGuid{record.entity_guid});
        if (entity == entt::null ||
            !world.registry().all_of<MachineRuntimeComponent>(entity)) {
            return invalid_state("Chunk machine runtime is unavailable during destruction");
        }
        entities.push_back(entity);
    }
    for (const entt::entity entity : entities) world.destroy_entity(entity);
    return {};
}

snt::core::Expected<void> GameMachineRuntimePersistence::destroy_runtimes(
    snt::ecs::World& world,
    std::span<const uint64_t> entity_guids) {
    std::unordered_set<uint64_t> seen_guids;
    std::vector<entt::entity> entities;
    entities.reserve(entity_guids.size());
    for (const uint64_t raw_guid : entity_guids) {
        if (raw_guid == 0 || !seen_guids.insert(raw_guid).second) {
            return invalid_argument("Machine runtime destruction requires unique non-zero EntityGuids");
        }
        const entt::entity entity = world.find_entity_by_guid(snt::ecs::EntityGuid{raw_guid});
        if (entity == entt::null ||
            !world.registry().all_of<MachineRuntimeComponent>(entity)) {
            return invalid_state("Selected machine runtime is unavailable during destruction");
        }
        entities.push_back(entity);
    }
    for (const entt::entity entity : entities) world.destroy_entity(entity);
    return {};
}

snt::core::Expected<MachineRuntimeComponent>
GameMachineRuntimePersistence::make_runtime_component(
    const MachineRuntimePersistenceRecord& record,
    ResourceRuntimeIndex::Snapshot resource_runtime_index) {
    return make_runtime(record, resource_runtime_index);
}

snt::core::Expected<void> GameMachineRuntimePersistence::copy_runtime_to_record(
    MachineRuntimePersistenceRecord& record,
    const MachineRuntimeComponent& runtime) {
    return copy_runtime_fields(record, runtime);
}

snt::core::Expected<void> GameMachineRuntimePersistence::capture(
    const snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars) {
    std::vector<RecordLocation> locations = collect_record_locations(sidecars);
    std::unordered_set<uint64_t> seen_guids;
    std::unordered_set<uint64_t> seen_anchors;
    std::unordered_set<uint64_t> loaded_guids;
    std::vector<MachineRuntimePersistenceRecord> updated_records;
    updated_records.reserve(locations.size());

    for (const RecordLocation& location : locations) {
        const MachineRuntimePersistenceRecord& existing =
            location.sidecar->machine_runtime_records[location.record_index];
        if (auto result = validate_record(*location.chunk_key, *location.sidecar, existing); !result) {
            return result.error();
        }
        if (!seen_guids.insert(existing.entity_guid).second ||
            !seen_anchors.insert(existing.anchor_entity_id.id).second) {
            return invalid_state("Duplicate machine persistence record encountered during capture");
        }

        if (existing.residency != MachineRuntimeResidency::kLoaded) {
            updated_records.push_back(existing);
            continue;
        }
        loaded_guids.insert(existing.entity_guid);

        const snt::ecs::EntityGuid entity_guid{existing.entity_guid};
        const entt::entity entity = world.find_entity_by_guid(entity_guid);
        if (entity == entt::null ||
            !world.registry().all_of<MachineRuntimeComponent>(entity)) {
            return invalid_state("Anchored machine runtime entity is unavailable during capture");
        }
        MachineRuntimePersistenceRecord updated = existing;
        if (auto result = copy_runtime_fields(
                updated, world.get_component<MachineRuntimeComponent>(entity)); !result) {
            return result.error();
        }
        if (auto result = validate_record(*location.chunk_key, *location.sidecar, updated); !result) {
            return result.error();
        }
        updated_records.push_back(updated);
    }

    const auto live_machines = world.registry().view<MachineRuntimeComponent>();
    for (const entt::entity entity : live_machines) {
        const snt::ecs::EntityGuid entity_guid = world.guid_of(entity);
        if (!entity_guid.valid() || !loaded_guids.contains(entity_guid.value)) {
            return invalid_state("Live MachineRuntimeComponent is not anchored in a chunk sidecar");
        }
    }

    for (size_t index = 0; index < locations.size(); ++index) {
        const RecordLocation& location = locations[index];
        location.sidecar->machine_runtime_records[location.record_index] =
            std::move(updated_records[index]);
    }
    return {};
}

}  // namespace snt::game
