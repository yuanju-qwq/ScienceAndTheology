// Durable AE interface-to-machine pattern-provider binding implementation.

#define SNT_LOG_CHANNEL "game.ae_machine_pattern_provider_persistence"
#include "game/simulation/ae_machine_pattern_provider_persistence.h"

#include "core/error.h"
#include "game/client/game_content_registry.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] const AeNetworkNodePersistenceRecord* find_interface_node(
    const GameChunkSidecar& sidecar, EntityId anchor) {
    const AeNetworkNodePersistenceRecord* result = nullptr;
    for (const AeNetworkNodePersistenceRecord& node : sidecar.ae_network_node_records) {
        if (node.anchor_entity_id != anchor || node.type != AeNetworkNodeType::kInterface) {
            continue;
        }
        if (result != nullptr) return nullptr;
        result = &node;
    }
    return result;
}

[[nodiscard]] std::vector<ResourceContentStack> canonical_recipe_outputs(
    const RecipeDefinition& recipe) {
    std::unordered_map<ResourceContentKey, int64_t, ResourceContentKey::Hash> amounts;
    amounts.reserve(recipe.outputs.size());
    for (const RecipeOutputDefinition& output : recipe.outputs) {
        amounts[ResourceContentKey::item(output.item_id)] += output.count;
    }
    std::vector<ResourceContentStack> result;
    result.reserve(amounts.size());
    for (const auto& [key, amount] : amounts) result.push_back({.key = key, .amount = amount});
    std::sort(result.begin(), result.end(), [](const ResourceContentStack& left,
                                               const ResourceContentStack& right) {
        if (left.key.type != right.key.type) return left.key.type < right.key.type;
        if (left.key.id != right.key.id) return left.key.id < right.key.id;
        return left.key.variant < right.key.variant;
    });
    return result;
}

struct ValidationIndex {
    std::unordered_map<uint64_t, const MachineRuntimePersistenceRecord*> machines;
    std::unordered_map<uint64_t, const AeMachinePatternProviderPersistenceRecord*> bindings;
};

[[nodiscard]] snt::core::Expected<ValidationIndex> build_index(
    const GameChunkSidecarRegistry& sidecars) {
    ValidationIndex index;
    std::unordered_set<uint64_t> machine_anchors;
    std::unordered_set<uint64_t> interface_anchors;
    std::unordered_set<uint64_t> bound_machines;
    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const MachineRuntimePersistenceRecord& machine : sidecar.machine_runtime_records) {
            if (!machine.anchor_entity_id.is_valid() ||
                !machine_anchors.insert(machine.anchor_entity_id.id).second) {
                error = invalid_state("AE pattern-provider validation found duplicate machine anchors");
                return;
            }
            index.machines.emplace(machine.anchor_entity_id.id, &machine);
        }
        for (const AeMachinePatternProviderPersistenceRecord& binding :
             sidecar.ae_machine_pattern_provider_records) {
            if (!binding.interface_anchor_entity_id.is_valid() ||
                !binding.machine_anchor_entity_id.is_valid() ||
                binding.interface_anchor_entity_id == binding.machine_anchor_entity_id ||
                binding.next_job_serial == 0 || binding.revision == 0) {
                error = invalid_argument("AE machine pattern-provider binding is invalid");
                return;
            }
            if (find_interface_node(sidecar, binding.interface_anchor_entity_id) == nullptr) {
                error = invalid_state("AE machine pattern-provider binding has no interface node owner");
                return;
            }
            if (!interface_anchors.insert(binding.interface_anchor_entity_id.id).second) {
                error = invalid_state("AE interface has more than one machine pattern-provider binding");
                return;
            }
            if (!bound_machines.insert(binding.machine_anchor_entity_id.id).second) {
                error = invalid_state("AE machine pattern-provider binding duplicates a machine owner");
                return;
            }
            index.bindings.emplace(binding.interface_anchor_entity_id.id, &binding);
        }
    });
    if (error) return *error;
    for (const auto& [interface_anchor, binding] : index.bindings) {
        static_cast<void>(interface_anchor);
        if (!index.machines.contains(binding->machine_anchor_entity_id.id)) {
            return invalid_state("AE machine pattern-provider binding names a missing machine anchor");
        }
    }
    return index;
}

}  // namespace

snt::core::Expected<AeMachinePatternProviderBindingAnchor>
GameAeMachinePatternProviderPersistence::create_binding(
    GameChunkSidecarRegistry& sidecars,
    const ChunkKey& interface_chunk,
    EntityId interface_anchor_entity_id,
    EntityId machine_anchor_entity_id,
    int32_t priority) {
    GameChunkSidecar* const sidecar = sidecars.get(interface_chunk);
    if (sidecar == nullptr) {
        return invalid_state("Cannot create an AE machine pattern-provider binding without its interface sidecar");
    }
    AeMachinePatternProviderPersistenceRecord record{
        .interface_anchor_entity_id = interface_anchor_entity_id,
        .machine_anchor_entity_id = machine_anchor_entity_id,
        .enabled = true,
        .priority = priority,
        .next_job_serial = 1,
        .revision = 1,
    };
    sidecar->ae_machine_pattern_provider_records.push_back(std::move(record));
    if (auto result = validate_all(sidecars); !result) {
        sidecar->ae_machine_pattern_provider_records.pop_back();
        return result.error();
    }
    return AeMachinePatternProviderBindingAnchor{.interface_anchor_entity_id =
                                                      interface_anchor_entity_id};
}

snt::core::Expected<void> GameAeMachinePatternProviderPersistence::remove_binding(
    GameChunkSidecarRegistry& sidecars, EntityId interface_anchor_entity_id) {
    if (!interface_anchor_entity_id.is_valid()) {
        return invalid_argument("AE machine pattern-provider removal requires an interface anchor");
    }
    GameChunkSidecar* owner = nullptr;
    size_t record_index = 0;
    size_t matches = 0;
    sidecars.for_each([&](const ChunkKey&, GameChunkSidecar& sidecar) {
        for (size_t index = 0; index < sidecar.ae_machine_pattern_provider_records.size();
             ++index) {
            if (sidecar.ae_machine_pattern_provider_records[index].interface_anchor_entity_id !=
                interface_anchor_entity_id) {
                continue;
            }
            ++matches;
            owner = &sidecar;
            record_index = index;
        }
    });
    if (matches != 1 || owner == nullptr) {
        return invalid_state("AE interface does not map to exactly one pattern-provider binding");
    }
    bool has_active_work_order = false;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        for (const MachineRuntimePersistenceRecord& machine : sidecar.machine_runtime_records) {
            if (machine.automation_work_order &&
                machine.automation_work_order->identity.provider_anchor_entity_id ==
                    interface_anchor_entity_id) {
                has_active_work_order = true;
                return;
            }
        }
    });
    if (has_active_work_order) {
        return invalid_state("Cannot remove an AE machine provider binding with an active work order");
    }
    owner->ae_machine_pattern_provider_records.erase(
        owner->ae_machine_pattern_provider_records.begin() +
        static_cast<std::ptrdiff_t>(record_index));
    return {};
}

snt::core::Expected<void> GameAeMachinePatternProviderPersistence::validate_all(
    const GameChunkSidecarRegistry& sidecars) {
    auto index = build_index(sidecars);
    if (!index) return index.error();

    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const MachineRuntimePersistenceRecord& machine : sidecar.machine_runtime_records) {
            if (!machine.automation_work_order) continue;
            const MachineAutomationWorkOrderRecord& work_order =
                *machine.automation_work_order;
            const auto binding = index->bindings.find(
                work_order.identity.provider_anchor_entity_id.id);
            if (binding == index->bindings.end() ||
                binding->second->machine_anchor_entity_id != machine.anchor_entity_id ||
                work_order.identity.provider_job_serial >= binding->second->next_job_serial) {
                error = invalid_state(
                    "Machine automation work order has no matching AE provider serial owner");
                return;
            }
        }
    });
    if (error) return *error;
    return {};
}

snt::core::Expected<void>
GameAeMachinePatternProviderPersistence::validate_content_references(
    const GameChunkSidecarRegistry& sidecars,
    const GameContentRegistry& content) {
    auto index = build_index(sidecars);
    if (!index) return index.error();
    for (const auto& [interface_anchor, binding] : index->bindings) {
        static_cast<void>(interface_anchor);
        const auto machine = index->machines.find(binding->machine_anchor_entity_id.id);
        if (machine == index->machines.end()) {
            return invalid_state("AE machine pattern-provider binding lost its machine record");
        }
        const MachineDefinition* const definition = content.find_machine(machine->second->machine_id);
        if (definition == nullptr) {
            return invalid_state("AE machine pattern-provider machine content is missing: " +
                                 machine->second->machine_id);
        }
        if (definition->requires_manual_activation) {
            return invalid_argument("Manual machines cannot be exposed by AE pattern providers: " +
                                    machine->second->machine_id);
        }
        if (content.recipes_for_machine(machine->second->machine_id).empty()) {
            return invalid_state("AE machine pattern-provider machine has no recipes: " +
                                 machine->second->machine_id);
        }
    }

    std::optional<snt::core::Error> error;
    sidecars.for_each([&](const ChunkKey&, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const MachineRuntimePersistenceRecord& machine : sidecar.machine_runtime_records) {
            if (!machine.automation_work_order) continue;
            const MachineAutomationWorkOrderRecord& work_order =
                *machine.automation_work_order;
            const RecipeDefinition* const recipe = content.find_recipe(work_order.recipe_id);
            if (recipe == nullptr || recipe->machine_id != machine.machine_id ||
                canonical_recipe_outputs(*recipe) != work_order.expected_outputs) {
                error = invalid_state(
                    "AE machine automation work order no longer matches its current recipe content");
                return;
            }
        }
    });
    if (error) return *error;
    return {};
}

}  // namespace snt::game
