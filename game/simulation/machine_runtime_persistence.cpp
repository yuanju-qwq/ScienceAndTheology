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
constexpr size_t kMaxMachineJobOwnerAccountBytes = 256;
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

[[nodiscard]] bool is_valid_stack(const MachineRuntimeItemStack& stack,
                                  bool allow_empty) {
    if (stack.resource.amount < 0 || stack.resource.amount > kMaxMachineStackSize) {
        return false;
    }
    if (stack.resource.amount == 0) {
        return allow_empty && stack.resource.key.type.empty() && stack.resource.key.id.empty() &&
               stack.resource.key.variant.empty();
    }
    return stack.resource.is_valid();
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
    for (const MachineRuntimeItemStack& input : recipe.inputs) {
        if (!is_valid_stack(input, false) ||
            !input_keys.insert(input.resource.key).second) {
            return invalid_argument("Machine persistence recipe input is invalid");
        }
    }
    for (const MachineRuntimeItemStack& output : recipe.outputs) {
        if (!is_valid_stack(output, false)) {
            return invalid_argument("Machine persistence recipe output is invalid");
        }
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
    for (const MachineRuntimeItemStack& input : record.input_slots) {
        if (!is_valid_stack(input, false) ||
            input.resource.amount > record.max_stack_size) {
            return invalid_argument("Machine persistence input slot is invalid");
        }
    }
    for (const MachineRuntimeItemStack& output : record.output_slots) {
        if (!is_valid_stack(output, false) ||
            output.resource.amount > record.max_stack_size) {
            return invalid_argument("Machine persistence output slot is invalid");
        }
    }
    for (const MachineFluidTank& tank : record.fluid_tanks) {
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
    return {};
}

[[nodiscard]] MachineRuntimeItemStack to_persisted_stack(const MachineItemStack& stack) {
    return {.resource = stack.resource};
}

[[nodiscard]] MachineItemStack to_runtime_stack(const MachineRuntimeItemStack& stack) {
    return {.resource = stack.resource};
}

[[nodiscard]] MachineRuntimeRecipeSnapshot to_persisted_recipe(
    const MachineRecipeSnapshot& recipe) {
    MachineRuntimeRecipeSnapshot result;
    result.id = recipe.id;
    result.inputs.reserve(recipe.inputs.size());
    for (const MachineItemStack& input : recipe.inputs) {
        result.inputs.push_back(to_persisted_stack(input));
    }
    result.duration_ticks = recipe.duration_ticks;
    result.energy_per_tick = recipe.energy_per_tick;
    result.outputs.reserve(recipe.outputs.size());
    for (const MachineItemStack& output : recipe.outputs) {
        result.outputs.push_back(to_persisted_stack(output));
    }
    return result;
}

[[nodiscard]] MachineRecipeSnapshot to_runtime_recipe(
    const MachineRuntimeRecipeSnapshot& recipe) {
    MachineRecipeSnapshot result;
    result.id = recipe.id;
    result.inputs.reserve(recipe.inputs.size());
    for (const MachineRuntimeItemStack& input : recipe.inputs) {
        result.inputs.push_back(to_runtime_stack(input));
    }
    result.duration_ticks = recipe.duration_ticks;
    result.energy_per_tick = recipe.energy_per_tick;
    result.outputs.reserve(recipe.outputs.size());
    for (const MachineRuntimeItemStack& output : recipe.outputs) {
        result.outputs.push_back(to_runtime_stack(output));
    }
    return result;
}

void copy_runtime_fields(MachineRuntimePersistenceRecord& record,
                         const MachineRuntimeComponent& runtime) {
    record.machine_id = runtime.machine_id;
    record.input_slots.clear();
    record.input_slots.reserve(runtime.input_slots.size());
    for (const MachineItemStack& input : runtime.input_slots) {
        record.input_slots.push_back(to_persisted_stack(input));
    }
    record.output_slots.clear();
    record.output_slots.reserve(runtime.output_slots.size());
    for (const MachineItemStack& output : runtime.output_slots) {
        record.output_slots.push_back(to_persisted_stack(output));
    }
    record.fluid_tanks = runtime.fluid_tanks;
    record.stored_energy = runtime.stored_energy;
    record.energy_capacity = runtime.energy_capacity;
    record.max_input_slots = runtime.max_input_slots;
    record.max_output_slots = runtime.max_output_slots;
    record.max_stack_size = runtime.max_stack_size;
    record.progress_ticks = runtime.progress_ticks;
    record.active_recipe.reset();
    if (runtime.active_recipe) {
        record.active_recipe = to_persisted_recipe(*runtime.active_recipe);
    }
    record.activation_requested = runtime.activation_requested;
    record.job_owner_account_id = runtime.job_owner_account_id;
    record.run_state = static_cast<uint8_t>(runtime.state);
}

[[nodiscard]] MachineRuntimePersistenceRecord make_record(
    EntityId anchor_entity_id,
    snt::ecs::EntityGuid entity_guid,
    const MachineRuntimeComponent& runtime) {
    MachineRuntimePersistenceRecord result;
    result.anchor_entity_id = anchor_entity_id;
    result.entity_guid = entity_guid.value;
    copy_runtime_fields(result, runtime);
    return result;
}

[[nodiscard]] MachineRuntimeComponent make_runtime(
    const MachineRuntimePersistenceRecord& record) {
    MachineRuntimeComponent result;
    result.machine_id = record.machine_id;
    result.input_slots.reserve(record.input_slots.size());
    for (const MachineRuntimeItemStack& input : record.input_slots) {
        result.input_slots.push_back(to_runtime_stack(input));
    }
    result.output_slots.reserve(record.output_slots.size());
    for (const MachineRuntimeItemStack& output : record.output_slots) {
        result.output_slots.push_back(to_runtime_stack(output));
    }
    result.fluid_tanks = record.fluid_tanks;
    result.stored_energy = record.stored_energy;
    result.energy_capacity = record.energy_capacity;
    result.max_input_slots = record.max_input_slots;
    result.max_output_slots = record.max_output_slots;
    result.max_stack_size = record.max_stack_size;
    result.progress_ticks = record.progress_ticks;
    if (record.active_recipe) {
        result.active_recipe = to_runtime_recipe(*record.active_recipe);
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
    MachineRuntimePersistenceRecord record = make_record(*anchor_entity_id, entity_guid, runtime);
    if (auto result = validate_record(chunk_key, *sidecar, record); !result) {
        world.destroy_entity(entity);
        sidecar->block_entities.pop_back();
        return result.error();
    }
    try {
        world.add_component<MachineRuntimeComponent>(entity, std::move(runtime));
        sidecar->machine_runtime_records.push_back(std::move(record));
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
    const GameChunkSidecarRegistry& sidecars) {
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
        const entt::entity entity = world.create_entity_with_guid(
            snt::ecs::EntityGuid{record.entity_guid});
        if (entity == entt::null) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to recreate persisted machine EntityGuid");
        }
        try {
            world.add_component<MachineRuntimeComponent>(entity, make_runtime(record));
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
    const ChunkKey& chunk_key) {
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
        const entt::entity entity = world.create_entity_with_guid(
            snt::ecs::EntityGuid{record->entity_guid});
        if (entity == entt::null) {
            for (const entt::entity created_entity : created) world.destroy_entity(created_entity);
            return invalid_state("Failed to recreate a chunk machine EntityGuid");
        }
        try {
            world.add_component<MachineRuntimeComponent>(
                entity, make_runtime_component(*record));
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
        copy_runtime_to_record(updated, world.get_component<MachineRuntimeComponent>(entity));
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

MachineRuntimeComponent GameMachineRuntimePersistence::make_runtime_component(
    const MachineRuntimePersistenceRecord& record) {
    return make_runtime(record);
}

void GameMachineRuntimePersistence::copy_runtime_to_record(
    MachineRuntimePersistenceRecord& record,
    const MachineRuntimeComponent& runtime) {
    copy_runtime_fields(record, runtime);
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
        copy_runtime_fields(updated, world.get_component<MachineRuntimeComponent>(entity));
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
