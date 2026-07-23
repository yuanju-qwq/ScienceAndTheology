// Active AE interface-to-machine pattern-provider implementation.

#define SNT_LOG_CHANNEL "game.ae_machine_pattern_provider_runtime"
#include "game/simulation/ae_machine_pattern_provider_runtime.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"
#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
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

[[nodiscard]] bool resource_key_less(const ResourceKey& left,
                                      const ResourceKey& right) noexcept {
    if (left.kind != right.kind) return left.kind < right.kind;
    if (left.runtime_id != right.runtime_id) return left.runtime_id < right.runtime_id;
    return left.variant < right.variant;
}

[[nodiscard]] bool same_stacks(std::vector<ResourceStack> left,
                               std::vector<ResourceStack> right) {
    const auto less = [](const ResourceStack& first, const ResourceStack& second) {
        return resource_key_less(first.key, second.key);
    };
    std::sort(left.begin(), left.end(), less);
    std::sort(right.begin(), right.end(), less);
    return left == right;
}

[[nodiscard]] snt::core::Expected<std::vector<ResourceStack>> canonical_recipe_outputs(
    const RecipeDefinition& recipe,
    const ResourceRuntimeIndex::Snapshot& snapshot) {
    std::unordered_map<ResourceKey, int64_t, ResourceKey::Hash> amounts;
    amounts.reserve(recipe.outputs.size());
    for (const RecipeOutputDefinition& output : recipe.outputs) {
        const auto resolved = resolve_resource_stack(
            ResourceContentStack::item(output.item_id, output.count), snapshot);
        if (!resolved || !resolved->is_valid()) {
            return invalid_state("AE machine provider recipe has an unresolved output: " +
                                 output.item_id);
        }
        int64_t& total = amounts[resolved->key];
        if (resolved->amount > std::numeric_limits<int64_t>::max() - total) {
            return invalid_state("AE machine provider recipe output amount overflows");
        }
        total += resolved->amount;
    }
    std::vector<ResourceStack> result;
    result.reserve(amounts.size());
    for (const auto& [key, amount] : amounts) result.push_back({.key = key, .amount = amount});
    std::sort(result.begin(), result.end(), [](const ResourceStack& left,
                                               const ResourceStack& right) {
        return resource_key_less(left.key, right.key);
    });
    return result;
}

[[nodiscard]] snt::core::Expected<std::vector<ResourceContentStack>>
canonical_recipe_outputs(const RecipeDefinition& recipe) {
    std::unordered_map<ResourceContentKey, int64_t, ResourceContentKey::Hash> amounts;
    amounts.reserve(recipe.outputs.size());
    for (const RecipeOutputDefinition& output : recipe.outputs) {
        ResourceContentKey key = ResourceContentKey::item(output.item_id);
        int64_t& total = amounts[key];
        if (output.count > std::numeric_limits<int64_t>::max() - total) {
            return invalid_state("AE machine provider stable recipe output amount overflows");
        }
        total += output.count;
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

[[nodiscard]] snt::core::Expected<std::vector<ResourceStack>> split_machine_inputs(
    const std::vector<ResourceStack>& inputs,
    int32_t max_slots,
    int32_t max_stack_size) {
    if (max_slots <= 0 || max_stack_size <= 0) {
        return invalid_state("AE machine provider target has invalid input capacity");
    }
    std::vector<ResourceStack> result;
    for (const ResourceStack& input : inputs) {
        if (!input.is_valid()) {
            return invalid_argument("AE machine provider received an invalid input stack");
        }
        int64_t remaining = input.amount;
        while (remaining != 0) {
            if (result.size() >= static_cast<size_t>(max_slots)) {
                return invalid_state("AE machine provider target has insufficient input slots");
            }
            const int64_t amount = std::min<int64_t>(remaining, max_stack_size);
            result.push_back({.key = input.key, .amount = amount});
            remaining -= amount;
        }
    }
    return result;
}

[[nodiscard]] bool can_machine_hold_outputs(const MachineRuntimeComponent& machine,
                                            const std::vector<ResourceStack>& outputs) {
    if (machine.max_output_slots <= 0 || machine.max_stack_size <= 0) return false;
    std::vector<ResourceStack> candidate = machine.output_slots;
    for (const ResourceStack& output : outputs) {
        if (!output.is_valid()) return false;
        auto existing = std::find_if(candidate.begin(), candidate.end(),
                                     [&output](const ResourceStack& value) {
                                         return value.key == output.key;
                                     });
        if (existing != candidate.end()) {
            if (output.amount > machine.max_stack_size - existing->amount) return false;
            existing->amount += output.amount;
            continue;
        }
        if (candidate.size() >= static_cast<size_t>(machine.max_output_slots) ||
            output.amount > machine.max_stack_size) {
            return false;
        }
        candidate.push_back(output);
    }
    return true;
}

[[nodiscard]] bool contains_stacks(const std::vector<ResourceStack>& slots,
                                   const std::vector<ResourceStack>& required) {
    for (const ResourceStack& expected : required) {
        int64_t available = 0;
        for (const ResourceStack& actual : slots) {
            if (actual.key != expected.key) continue;
            if (actual.amount > std::numeric_limits<int64_t>::max() - available) {
                return false;
            }
            available += actual.amount;
        }
        if (available < expected.amount) return false;
    }
    return true;
}

[[nodiscard]] bool remove_stacks(std::vector<ResourceStack>& slots,
                                 const std::vector<ResourceStack>& required) {
    if (!contains_stacks(slots, required)) return false;
    for (const ResourceStack& expected : required) {
        int64_t remaining = expected.amount;
        for (ResourceStack& actual : slots) {
            if (actual.key != expected.key || remaining == 0) continue;
            const int64_t removed = std::min(actual.amount, remaining);
            actual.amount -= removed;
            remaining -= removed;
        }
    }
    std::erase_if(slots, [](const ResourceStack& value) { return !value.is_valid(); });
    return true;
}

class CancelledComponentAccess final : public IAeAutocraftingResourceAccess {
public:
    explicit CancelledComponentAccess(ResourceKeyContext context) : context_(std::move(context)) {}

    [[nodiscard]] ResourceKeyContext key_context() const noexcept override { return context_; }
    [[nodiscard]] int64_t amount_of(const ResourceKeyContext&, const ResourceKey&) const override {
        return 0;
    }
    [[nodiscard]] int64_t insert(const ResourceKeyContext&, const ResourceStack&,
                                 ResourceTransferMode) override {
        return 0;
    }
    [[nodiscard]] int64_t extract(const ResourceKeyContext&, const ResourceStack&,
                                  ResourceTransferMode) override {
        return 0;
    }

private:
    ResourceKeyContext context_;
};

}  // namespace

struct AeMachinePatternProviderRuntimeService::MachineLocation {
    ChunkKey chunk_key;
    uint64_t entity_guid = 0;
    std::string machine_id;
};

struct AeMachinePatternProviderRuntimeService::ResolvedOperation {
    std::string recipe_id;
    std::vector<ResourceStack> inputs;
    std::vector<ResourceStack> expected_outputs;
};

struct AeMachinePatternProviderRuntimeService::ActiveJob {
    EntityId interface_anchor_entity_id;
    uint64_t scope_id = 0;
};

struct AeMachinePatternProviderRuntimeService::LiveProvider {
    ChunkKey chunk_key;
    size_t record_index = 0;
    EntityId interface_anchor_entity_id;
    EntityId machine_anchor_entity_id;
    std::unique_ptr<MachineEndpoint> endpoint;
    std::optional<AePatternProviderHandle> dispatcher_handle;
    uint64_t scope_id = 0;
    // A topology boundary can arrive while this endpoint has a real machine
    // operation in flight. Keep the requested change local to this provider
    // and retry it when that operation reaches a terminal dispatcher state.
    bool registration_refresh_pending = false;
};

class AeMachinePatternProviderRuntimeService::MachineEndpoint final
    : public IAePatternProviderEndpoint {
public:
    MachineEndpoint(AeMachinePatternProviderRuntimeService& owner,
                    EntityId interface_anchor_entity_id,
                    EntityId machine_anchor_entity_id) noexcept
        : owner_(&owner),
          interface_anchor_entity_id_(interface_anchor_entity_id),
          machine_anchor_entity_id_(machine_anchor_entity_id) {}

    [[nodiscard]] AePatternProviderStartState can_start_operation(
        const AePatternProviderOperationRequest& request) const noexcept override {
        if (owner_ == nullptr) return AePatternProviderStartState::kUnavailable;
        const AeMachinePatternProviderPersistenceRecord* const binding =
            owner_->find_binding(interface_anchor_entity_id_);
        const MachineRuntimeComponent* const machine =
            owner_->find_live_machine(machine_anchor_entity_id_);
        if (binding == nullptr || !binding->enabled || machine == nullptr) {
            return AePatternProviderStartState::kUnavailable;
        }
        if (machine->automation_work_order || machine->active_recipe ||
            machine->activation_requested || !machine->input_slots.empty() ||
            !machine->output_slots.empty()) {
            return AePatternProviderStartState::kBusy;
        }
        const auto operation = owner_->resolve_operation(interface_anchor_entity_id_, request);
        if (!operation || !machine->resource_runtime_index.key_context().matches(
                              owner_->content_->resource_runtime_index().key_context()) ||
            !can_machine_hold_outputs(*machine, operation->expected_outputs)) {
            return AePatternProviderStartState::kUnavailable;
        }
        const auto input_slots = split_machine_inputs(
            operation->inputs, machine->max_input_slots, machine->max_stack_size);
        return input_slots ? AePatternProviderStartState::kReady
                           : AePatternProviderStartState::kUnavailable;
    }

    [[nodiscard]] snt::core::Expected<AePatternProviderOperationHandle> start_operation(
        AePatternProviderOperationRequest request) override {
        const AePatternProviderStartState readiness = can_start_operation(request);
        if (readiness != AePatternProviderStartState::kReady) {
            return invalid_state("AE machine provider target cannot start the requested operation");
        }
        auto operation = owner_->resolve_operation(interface_anchor_entity_id_, request);
        if (!operation) return operation.error();
        MachineRuntimeComponent* const machine =
            owner_->find_live_machine(machine_anchor_entity_id_);
        if (machine == nullptr) {
            return invalid_state("AE machine provider target disappeared before operation start");
        }
        auto input_slots = split_machine_inputs(
            operation->inputs,
            machine->max_input_slots,
            machine->max_stack_size);
        if (!input_slots) return input_slots.error();

        AeMachinePatternProviderPersistenceRecord* const binding =
            owner_->find_binding(interface_anchor_entity_id_);
        if (binding == nullptr || machine == nullptr ||
            binding->next_job_serial == std::numeric_limits<uint64_t>::max() ||
            binding->revision == std::numeric_limits<uint64_t>::max()) {
            return invalid_state("AE machine provider durable work-order serial is exhausted");
        }

        const uint64_t serial = binding->next_job_serial;
        machine->input_slots = std::move(*input_slots);
        machine->automation_work_order = MachineAutomationWorkOrder{
            .identity = {
                .provider_anchor_entity_id = interface_anchor_entity_id_,
                .provider_job_serial = serial,
            },
            .recipe_id = operation->recipe_id,
            .expected_outputs = std::move(operation->expected_outputs),
            .state = MachineAutomationWorkOrderState::kQueued,
        };
        machine->activation_requested = false;
        machine->job_owner_account_id.clear();
        machine->state = MachineRunState::Idle;
        ++binding->next_job_serial;
        ++binding->revision;
        owner_->note_work_order_started(machine->automation_work_order->identity);
        SNT_LOG_INFO("Queued AE machine work order interface=%llu serial=%llu machine=%llu recipe=%s",
                     static_cast<unsigned long long>(interface_anchor_entity_id_.id),
                     static_cast<unsigned long long>(serial),
                     static_cast<unsigned long long>(machine_anchor_entity_id_.id),
                     machine->automation_work_order->recipe_id.c_str());
        return AePatternProviderOperationHandle{.value = serial};
    }

    [[nodiscard]] AePatternProviderOperationState operation_state(
        AePatternProviderOperationHandle handle) const noexcept override {
        const MachineRuntimeComponent* const machine =
            owner_ == nullptr ? nullptr : owner_->find_live_machine(machine_anchor_entity_id_);
        if (machine == nullptr) return AePatternProviderOperationState::kBlocked;
        if (!handle.is_valid() || !machine->automation_work_order ||
            machine->automation_work_order->identity.provider_anchor_entity_id !=
                interface_anchor_entity_id_ ||
            machine->automation_work_order->identity.provider_job_serial != handle.value) {
            return AePatternProviderOperationState::kMissing;
        }
        switch (machine->automation_work_order->state) {
            case MachineAutomationWorkOrderState::kQueued:
            case MachineAutomationWorkOrderState::kRunning:
                return machine->state == MachineRunState::WaitingForEnergy ||
                       machine->state == MachineRunState::WaitingForOutput
                    ? AePatternProviderOperationState::kBlocked
                    : AePatternProviderOperationState::kRunning;
            case MachineAutomationWorkOrderState::kOutputReady:
                return AePatternProviderOperationState::kCompleted;
            case MachineAutomationWorkOrderState::kFailed:
                return AePatternProviderOperationState::kFailed;
        }
        return AePatternProviderOperationState::kFailed;
    }

    [[nodiscard]] snt::core::Expected<std::vector<ResourceStack>> completed_outputs(
        AePatternProviderOperationHandle handle,
        const ResourceKeyContext& context) const override {
        const MachineRuntimeComponent* const machine =
            owner_ == nullptr ? nullptr : owner_->find_live_machine(machine_anchor_entity_id_);
        if (machine == nullptr || !context.matches(machine->resource_runtime_index.key_context()) ||
            !handle.is_valid() || !machine->automation_work_order ||
            machine->automation_work_order->identity.provider_anchor_entity_id !=
                interface_anchor_entity_id_ ||
            machine->automation_work_order->identity.provider_job_serial != handle.value ||
            machine->automation_work_order->state !=
                MachineAutomationWorkOrderState::kOutputReady) {
            return invalid_state("AE machine provider completed-output request is not ready");
        }
        if (!contains_stacks(machine->output_slots,
                             machine->automation_work_order->expected_outputs)) {
            return invalid_state("AE machine provider work order output is absent from machine slots");
        }
        return machine->automation_work_order->expected_outputs;
    }

    [[nodiscard]] snt::core::Expected<void> acknowledge_completion(
        AePatternProviderOperationHandle handle,
        const ResourceKeyContext& context) override {
        MachineRuntimeComponent* const machine =
            owner_ == nullptr ? nullptr : owner_->find_live_machine(machine_anchor_entity_id_);
        if (machine == nullptr || !context.matches(machine->resource_runtime_index.key_context()) ||
            !handle.is_valid() || !machine->automation_work_order ||
            machine->automation_work_order->identity.provider_anchor_entity_id !=
                interface_anchor_entity_id_ ||
            machine->automation_work_order->identity.provider_job_serial != handle.value ||
            machine->automation_work_order->state !=
                MachineAutomationWorkOrderState::kOutputReady) {
            return invalid_state("AE machine provider acknowledgement is not ready");
        }
        const std::vector<ResourceStack> outputs =
            machine->automation_work_order->expected_outputs;
        if (!remove_stacks(machine->output_slots, outputs)) {
            return invalid_state("AE machine provider could not remove acknowledged machine output");
        }
        const MachineAutomationWorkOrderIdentity identity =
            machine->automation_work_order->identity;
        machine->automation_work_order.reset();
        machine->state = MachineRunState::Idle;
        owner_->forget_work_order(identity);
        SNT_LOG_INFO("Acknowledged AE machine work order interface=%llu serial=%llu machine=%llu",
                     static_cast<unsigned long long>(interface_anchor_entity_id_.id),
                     static_cast<unsigned long long>(handle.value),
                     static_cast<unsigned long long>(machine_anchor_entity_id_.id));
        return {};
    }

private:
    AeMachinePatternProviderRuntimeService* owner_ = nullptr;
    EntityId interface_anchor_entity_id_;
    EntityId machine_anchor_entity_id_;
};

AeMachinePatternProviderRuntimeService::AeMachinePatternProviderRuntimeService(
    GameContentRegistry& content,
    snt::ecs::World& world,
    GameChunkSidecarRegistry& sidecars,
    AeNetworkRuntimeService& network_runtime)
    : content_(&content),
      world_(&world),
      sidecars_(&sidecars),
      network_runtime_(&network_runtime),
      dispatcher_(content.resource_runtime_index()) {}

AeMachinePatternProviderRuntimeService::~AeMachinePatternProviderRuntimeService() {
    for (auto& [anchor, provider] : providers_) {
        static_cast<void>(anchor);
        if (provider->dispatcher_handle &&
            !dispatcher_.unregister_provider(*provider->dispatcher_handle)) {
            SNT_LOG_WARN("AE machine provider shutdown retained an in-flight endpoint interface=%llu",
                         static_cast<unsigned long long>(provider->interface_anchor_entity_id.id));
        }
    }
    providers_.clear();
    active_jobs_.clear();
    known_work_orders_.clear();
}

void AeMachinePatternProviderRuntimeService::note_work_order_started(
    MachineAutomationWorkOrderIdentity identity) noexcept {
    if (identity.is_valid()) known_work_orders_.insert(identity);
}

void AeMachinePatternProviderRuntimeService::forget_work_order(
    MachineAutomationWorkOrderIdentity identity) noexcept {
    if (identity.is_valid()) known_work_orders_.erase(identity);
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::rebuild_machine_index() {
    std::unordered_map<uint64_t, MachineLocation> next;
    std::optional<snt::core::Error> error;
    sidecars_->for_each([&](const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
        if (error) return;
        for (const MachineRuntimePersistenceRecord& machine : sidecar.machine_runtime_records) {
            if (!machine.anchor_entity_id.is_valid() || machine.entity_guid == 0 ||
                machine.machine_id.empty() ||
                !next.emplace(machine.anchor_entity_id.id,
                              MachineLocation{.chunk_key = chunk_key,
                                              .entity_guid = machine.entity_guid,
                                              .machine_id = machine.machine_id})
                     .second) {
                error = invalid_state("AE machine provider found duplicate or invalid machine anchors");
                return;
            }
        }
    });
    if (error) return *error;
    machines_ = std::move(next);
    return {};
}

const AeMachinePatternProviderRuntimeService::MachineLocation*
AeMachinePatternProviderRuntimeService::find_machine_location(
    EntityId machine_anchor_entity_id) const noexcept {
    const auto found = machines_.find(machine_anchor_entity_id.id);
    return machine_anchor_entity_id.is_valid() && found != machines_.end() ? &found->second
                                                                             : nullptr;
}

MachineRuntimeComponent* AeMachinePatternProviderRuntimeService::find_live_machine(
    EntityId machine_anchor_entity_id) noexcept {
    const MachineLocation* const location = find_machine_location(machine_anchor_entity_id);
    if (location == nullptr || world_ == nullptr) return nullptr;
    const entt::entity entity = world_->find_entity_by_guid(
        snt::ecs::EntityGuid{location->entity_guid});
    if (entity == entt::null || !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
        return nullptr;
    }
    return &world_->get_component<MachineRuntimeComponent>(entity);
}

const MachineRuntimeComponent* AeMachinePatternProviderRuntimeService::find_live_machine(
    EntityId machine_anchor_entity_id) const noexcept {
    return const_cast<AeMachinePatternProviderRuntimeService*>(this)->find_live_machine(
        machine_anchor_entity_id);
}

AeMachinePatternProviderRuntimeService::LiveProvider*
AeMachinePatternProviderRuntimeService::find_provider(
    EntityId interface_anchor_entity_id) noexcept {
    const auto found = providers_.find(interface_anchor_entity_id.id);
    return interface_anchor_entity_id.is_valid() && found != providers_.end()
        ? found->second.get()
        : nullptr;
}

const AeMachinePatternProviderRuntimeService::LiveProvider*
AeMachinePatternProviderRuntimeService::find_provider(
    EntityId interface_anchor_entity_id) const noexcept {
    return const_cast<AeMachinePatternProviderRuntimeService*>(this)->find_provider(
        interface_anchor_entity_id);
}

AeMachinePatternProviderPersistenceRecord*
AeMachinePatternProviderRuntimeService::find_binding(
    EntityId interface_anchor_entity_id) noexcept {
    LiveProvider* const provider = find_provider(interface_anchor_entity_id);
    if (provider == nullptr || sidecars_ == nullptr) return nullptr;
    GameChunkSidecar* const sidecar = sidecars_->get(provider->chunk_key);
    if (sidecar == nullptr ||
        provider->record_index >= sidecar->ae_machine_pattern_provider_records.size()) {
        return nullptr;
    }
    AeMachinePatternProviderPersistenceRecord& record =
        sidecar->ae_machine_pattern_provider_records[provider->record_index];
    return record.interface_anchor_entity_id == interface_anchor_entity_id ? &record : nullptr;
}

const AeMachinePatternProviderPersistenceRecord*
AeMachinePatternProviderRuntimeService::find_binding(
    EntityId interface_anchor_entity_id) const noexcept {
    return const_cast<AeMachinePatternProviderRuntimeService*>(this)->find_binding(
        interface_anchor_entity_id);
}

snt::core::Expected<std::vector<AeAutocraftingPatternDefinition>>
AeMachinePatternProviderRuntimeService::make_patterns(
    EntityId interface_anchor_entity_id) const {
    const LiveProvider* const provider = find_provider(interface_anchor_entity_id);
    const AeMachinePatternProviderPersistenceRecord* const binding =
        find_binding(interface_anchor_entity_id);
    if (provider == nullptr || binding == nullptr || content_ == nullptr) {
        return invalid_state("AE machine provider has no live durable binding");
    }
    const MachineLocation* const machine = find_machine_location(binding->machine_anchor_entity_id);
    if (machine == nullptr) return invalid_state("AE machine provider has no target machine record");
    const MachineDefinition* const definition = content_->find_machine(machine->machine_id);
    if (definition == nullptr || definition->requires_manual_activation) {
        return invalid_state("AE machine provider target is absent or requires manual activation");
    }

    std::vector<AeAutocraftingPatternDefinition> patterns;
    const std::vector<RecipeDefinition> recipes = content_->recipes_for_machine(machine->machine_id);
    patterns.reserve(recipes.size());
    for (const RecipeDefinition& recipe : recipes) {
        AeAutocraftingPatternDefinition pattern{
            .id = recipe.id,
            .ticks_per_operation = static_cast<uint32_t>(recipe.duration_ticks),
        };
        if (recipe.duration_ticks <= 0) {
            return invalid_state("AE machine provider recipe has a non-positive duration: " +
                                 recipe.id);
        }
        pattern.inputs.reserve(recipe.inputs.size());
        for (const RecipeInputDefinition& input : recipe.inputs) {
            pattern.inputs.push_back(ResourceContentStack::item(input.item_id, input.count));
        }
        auto outputs = canonical_recipe_outputs(recipe);
        if (!outputs) return outputs.error();
        pattern.outputs = std::move(*outputs);
        patterns.push_back(std::move(pattern));
    }
    if (patterns.empty()) {
        return invalid_state("AE machine provider target has no current recipe patterns");
    }
    return patterns;
}

snt::core::Expected<AeMachinePatternProviderRuntimeService::ResolvedOperation>
AeMachinePatternProviderRuntimeService::resolve_operation(
    EntityId interface_anchor_entity_id,
    const AePatternProviderOperationRequest& request) const {
    const AeMachinePatternProviderPersistenceRecord* const binding =
        find_binding(interface_anchor_entity_id);
    const MachineLocation* const machine = binding == nullptr
        ? nullptr
        : find_machine_location(binding->machine_anchor_entity_id);
    if (binding == nullptr || machine == nullptr || content_ == nullptr || request.pattern_id.empty()) {
        return invalid_state("AE machine provider operation has no valid binding or target");
    }
    const RecipeDefinition* const recipe = content_->find_recipe(request.pattern_id);
    if (recipe == nullptr || recipe->machine_id != machine->machine_id) {
        return invalid_state("AE machine provider operation does not name a target-machine recipe");
    }
    const ResourceRuntimeIndex::Snapshot snapshot = content_->resource_runtime_index();
    ResolvedOperation result{.recipe_id = recipe->id};
    result.inputs.reserve(recipe->inputs.size());
    for (const RecipeInputDefinition& input : recipe->inputs) {
        const auto resolved = resolve_resource_stack(
            ResourceContentStack::item(input.item_id, input.count), snapshot);
        if (!resolved || !resolved->is_valid()) {
            return invalid_state("AE machine provider recipe has an unresolved input: " +
                                 input.item_id);
        }
        result.inputs.push_back(*resolved);
    }
    std::sort(result.inputs.begin(), result.inputs.end(), [](const ResourceStack& left,
                                                              const ResourceStack& right) {
        return resource_key_less(left.key, right.key);
    });
    auto outputs = canonical_recipe_outputs(*recipe, snapshot);
    if (!outputs) return outputs.error();
    result.expected_outputs = std::move(*outputs);
    if (!same_stacks(request.inputs, result.inputs) ||
        !same_stacks(request.expected_outputs, result.expected_outputs)) {
        return invalid_state("AE machine provider operation does not match its registered recipe pattern");
    }
    return result;
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::materialize_chunk(
    const ChunkKey& chunk_key, const GameChunkSidecar& sidecar) {
    if (content_ == nullptr || world_ == nullptr || sidecars_ == nullptr ||
        network_runtime_ == nullptr) {
        return invalid_state("AE machine provider runtime has no session owners");
    }
    if (auto rebuilt = rebuild_machine_index(); !rebuilt) return rebuilt.error();
    std::vector<uint64_t> inserted;
    for (size_t index = 0; index < sidecar.ae_machine_pattern_provider_records.size(); ++index) {
        const AeMachinePatternProviderPersistenceRecord& binding =
            sidecar.ae_machine_pattern_provider_records[index];
        const auto existing = providers_.find(binding.interface_anchor_entity_id.id);
        if (existing != providers_.end()) {
            if (!(existing->second->chunk_key == chunk_key)) {
                return invalid_state("AE machine provider interface is materialized by another chunk");
            }
            continue;
        }
        auto provider = std::make_unique<LiveProvider>();
        provider->chunk_key = chunk_key;
        provider->record_index = index;
        provider->interface_anchor_entity_id = binding.interface_anchor_entity_id;
        provider->machine_anchor_entity_id = binding.machine_anchor_entity_id;
        provider->endpoint = std::make_unique<MachineEndpoint>(
            *this, binding.interface_anchor_entity_id, binding.machine_anchor_entity_id);
        providers_.emplace(binding.interface_anchor_entity_id.id, std::move(provider));
        inserted.push_back(binding.interface_anchor_entity_id.id);
    }
    if (auto refreshed = refresh_topology(); !refreshed) {
        for (const uint64_t interface_anchor : inserted) {
            const auto found = providers_.find(interface_anchor);
            if (found == providers_.end()) continue;
            if (found->second->dispatcher_handle) {
                static_cast<void>(dispatcher_.unregister_provider(
                    *found->second->dispatcher_handle));
            }
            providers_.erase(found);
        }
        return refreshed.error();
    }
    if (!inserted.empty()) {
        SNT_LOG_INFO("Materialized %zu AE machine pattern provider(s) for chunk (%s,%d,%d,%d)",
                     inserted.size(), chunk_key.dimension_id.c_str(), chunk_key.chunk_x,
                     chunk_key.chunk_y, chunk_key.chunk_z);
    }
    return {};
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::dematerialize_chunk(
    const ChunkKey& chunk_key) {
    std::vector<uint64_t> removals;
    for (const auto& [anchor, provider] : providers_) {
        if (provider->chunk_key == chunk_key) removals.push_back(anchor);
    }
    std::sort(removals.begin(), removals.end());
    for (const uint64_t interface_anchor : removals) {
        const auto found = providers_.find(interface_anchor);
        if (found == providers_.end()) continue;
        if (found->second->dispatcher_handle &&
            !dispatcher_.unregister_provider(*found->second->dispatcher_handle)) {
            return invalid_state(
                "Cannot dematerialize an AE interface with an in-flight machine operation");
        }
        providers_.erase(found);
    }
    if (!removals.empty()) {
        SNT_LOG_INFO("Dematerialized %zu AE machine pattern provider(s) for chunk (%s,%d,%d,%d)",
                     removals.size(), chunk_key.dimension_id.c_str(), chunk_key.chunk_x,
                     chunk_key.chunk_y, chunk_key.chunk_z);
    }
    return {};
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::refresh_provider_registration(
    LiveProvider& provider) {
    const AeMachinePatternProviderPersistenceRecord* const binding =
        find_binding(provider.interface_anchor_entity_id);
    const AeNetworkRuntimeNodePresentation* const node = network_runtime_->find_node(
        provider.interface_anchor_entity_id);
    const bool should_register = binding != nullptr && binding->enabled && node != nullptr &&
        node->type == AeNetworkNodeType::kInterface && node->online && node->component_id != 0;
    if (!should_register) {
        if (provider.dispatcher_handle) {
            if (!dispatcher_.unregister_provider(*provider.dispatcher_handle)) {
                dispatcher_.cancel_jobs_for_scope(provider.scope_id);
                provider.registration_refresh_pending = true;
                SNT_LOG_INFO(
                    "Deferred AE machine provider unregister until its in-flight operation settles interface=%llu",
                    static_cast<unsigned long long>(provider.interface_anchor_entity_id.id));
                return {};
            }
            provider.dispatcher_handle.reset();
            provider.scope_id = 0;
        }
        provider.registration_refresh_pending = false;
        return {};
    }

    if (!provider.dispatcher_handle) {
        auto patterns = make_patterns(provider.interface_anchor_entity_id);
        if (!patterns) return patterns.error();
        auto registered = dispatcher_.register_provider({
            .provider_key = "ae.interface." +
                std::to_string(provider.interface_anchor_entity_id.id),
            .scope_id = node->component_id,
            .priority = binding->priority,
            .patterns = std::move(*patterns),
            .endpoint = provider.endpoint.get(),
        });
        if (!registered) return registered.error();
        provider.dispatcher_handle = *registered;
        provider.scope_id = node->component_id;
        provider.registration_refresh_pending = false;
        return {};
    }
    if (provider.scope_id != node->component_id) {
        if (auto moved = dispatcher_.update_provider_scope(
                *provider.dispatcher_handle, node->component_id); !moved) {
            dispatcher_.cancel_jobs_for_scope(provider.scope_id);
            provider.registration_refresh_pending = true;
            SNT_LOG_INFO(
                "Deferred AE machine provider scope move until its in-flight operation settles interface=%llu old_scope=%llu new_scope=%u",
                static_cast<unsigned long long>(provider.interface_anchor_entity_id.id),
                static_cast<unsigned long long>(provider.scope_id),
                static_cast<unsigned int>(node->component_id));
            return {};
        }
        provider.scope_id = node->component_id;
    }
    provider.registration_refresh_pending = false;
    return {};
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::refresh_topology() {
    std::vector<uint64_t> anchors;
    anchors.reserve(providers_.size());
    for (const auto& [anchor, provider] : providers_) {
        static_cast<void>(provider);
        anchors.push_back(anchor);
    }
    std::sort(anchors.begin(), anchors.end());
    for (const uint64_t anchor : anchors) {
        const auto found = providers_.find(anchor);
        if (found == providers_.end()) continue;
        if (auto refreshed = refresh_provider_registration(*found->second); !refreshed) {
            return refreshed.error();
        }
    }
    return recover_persisted_work_orders();
}

snt::core::Expected<void>
AeMachinePatternProviderRuntimeService::recover_persisted_work_orders() {
    if (content_ == nullptr) {
        return invalid_state("AE machine provider recovery has no content registry");
    }
    std::vector<uint64_t> anchors;
    anchors.reserve(providers_.size());
    for (const auto& [anchor, provider] : providers_) {
        static_cast<void>(provider);
        anchors.push_back(anchor);
    }
    std::sort(anchors.begin(), anchors.end());
    const ResourceRuntimeIndex::Snapshot snapshot = content_->resource_runtime_index();
    for (const uint64_t anchor : anchors) {
        const auto found = providers_.find(anchor);
        if (found == providers_.end()) continue;
        LiveProvider& provider = *found->second;
        if (!provider.dispatcher_handle || provider.scope_id == 0) continue;
        MachineRuntimeComponent* const machine = find_live_machine(
            provider.machine_anchor_entity_id);
        if (machine == nullptr || !machine->automation_work_order) continue;
        const MachineAutomationWorkOrder& work_order = *machine->automation_work_order;
        if (!work_order.identity.is_valid() ||
            work_order.identity.provider_anchor_entity_id !=
                provider.interface_anchor_entity_id ||
            work_order.expected_outputs.empty()) {
            return invalid_state("Persisted AE machine work order does not match its provider");
        }
        if (known_work_orders_.contains(work_order.identity)) continue;
        switch (work_order.state) {
            case MachineAutomationWorkOrderState::kQueued:
            case MachineAutomationWorkOrderState::kRunning:
            case MachineAutomationWorkOrderState::kOutputReady:
                break;
            case MachineAutomationWorkOrderState::kFailed:
                note_work_order_started(work_order.identity);
                SNT_LOG_WARN(
                    "Retained failed durable AE machine work order interface=%llu serial=%llu",
                    static_cast<unsigned long long>(work_order.identity.provider_anchor_entity_id.id),
                    static_cast<unsigned long long>(work_order.identity.provider_job_serial));
                continue;
        }
        if (!machine->resource_runtime_index.key_context().matches(snapshot.key_context())) {
            return invalid_state("Persisted AE machine work order has a stale resource snapshot");
        }
        const auto requested = resolve_content_stack(work_order.expected_outputs.front(), snapshot);
        if (!requested || !requested->is_valid()) {
            return invalid_state("Persisted AE machine work order has an unresolved output");
        }
        auto adopted = dispatcher_.adopt_operation(
            *provider.dispatcher_handle,
            {.value = work_order.identity.provider_job_serial},
            *requested,
            work_order.recipe_id,
            work_order.expected_outputs);
        if (!adopted) return adopted.error();
        if (!active_jobs_.emplace(job_token(*adopted), ActiveJob{
                .interface_anchor_entity_id = provider.interface_anchor_entity_id,
                .scope_id = provider.scope_id,
            }).second) {
            return invalid_state("AE machine provider recovery produced a duplicate job handle");
        }
        note_work_order_started(work_order.identity);
        SNT_LOG_INFO("Recovered durable AE machine work order interface=%llu serial=%llu machine=%llu",
                     static_cast<unsigned long long>(work_order.identity.provider_anchor_entity_id.id),
                     static_cast<unsigned long long>(work_order.identity.provider_job_serial),
                     static_cast<unsigned long long>(provider.machine_anchor_entity_id.id));
    }
    return {};
}

snt::core::Expected<void>
AeMachinePatternProviderRuntimeService::refresh_content_definitions() {
    std::vector<uint64_t> anchors;
    anchors.reserve(providers_.size());
    for (const auto& [anchor, provider] : providers_) {
        static_cast<void>(provider);
        anchors.push_back(anchor);
    }
    std::sort(anchors.begin(), anchors.end());
    for (const uint64_t anchor : anchors) {
        const auto found = providers_.find(anchor);
        if (found == providers_.end() || !found->second->dispatcher_handle) continue;
        auto patterns = make_patterns(found->second->interface_anchor_entity_id);
        if (!patterns) return patterns.error();
        if (auto updated = dispatcher_.update_provider_patterns(
                *found->second->dispatcher_handle, std::move(*patterns)); !updated) {
            return updated.error();
        }
    }
    return refresh_topology();
}

snt::core::Expected<AeNetworkComponentStorage>
AeMachinePatternProviderRuntimeService::component_storage_for(
    EntityId interface_anchor_entity_id, uint64_t expected_scope_id) const {
    const AeNetworkRuntimeNodePresentation* const node = network_runtime_->find_node(
        interface_anchor_entity_id);
    if (node == nullptr || node->type != AeNetworkNodeType::kInterface || !node->online ||
        node->component_id == 0 || node->component_id != expected_scope_id) {
        return invalid_state("AE machine provider interface is not in its expected live component");
    }
    return AeNetworkComponentStorage{*network_runtime_, interface_anchor_entity_id};
}

snt::core::Expected<AePatternProviderPlan> AeMachinePatternProviderRuntimeService::plan(
    EntityId interface_anchor_entity_id,
    ResourceContentStack requested) const {
    const LiveProvider* const provider = find_provider(interface_anchor_entity_id);
    if (provider == nullptr || !provider->dispatcher_handle || provider->scope_id == 0) {
        return invalid_state("AE interface has no active machine pattern provider");
    }
    auto storage = component_storage_for(interface_anchor_entity_id, provider->scope_id);
    if (!storage) return storage.error();
    AeAutocraftingStorageAccess access{*storage};
    return dispatcher_.plan(access, provider->scope_id, std::move(requested));
}

snt::core::Expected<AePatternProviderJobHandle>
AeMachinePatternProviderRuntimeService::submit_job(
    EntityId interface_anchor_entity_id,
    ResourceContentStack requested) {
    const LiveProvider* const provider = find_provider(interface_anchor_entity_id);
    if (provider == nullptr || !provider->dispatcher_handle || provider->scope_id == 0) {
        return invalid_state("AE interface has no active machine pattern provider");
    }
    auto storage = component_storage_for(interface_anchor_entity_id, provider->scope_id);
    if (!storage) return storage.error();
    AeAutocraftingStorageAccess access{*storage};
    auto submitted = dispatcher_.submit_job(access, provider->scope_id, std::move(requested));
    if (!submitted) return submitted.error();
    active_jobs_.emplace(job_token(*submitted), ActiveJob{
        .interface_anchor_entity_id = interface_anchor_entity_id,
        .scope_id = provider->scope_id,
    });
    return *submitted;
}

snt::core::Expected<AePatternProviderJobSnapshot>
AeMachinePatternProviderRuntimeService::tick_job(
    AePatternProviderJobHandle handle, uint32_t max_operations) {
    const auto active = active_jobs_.find(job_token(handle));
    if (active == active_jobs_.end()) {
        return invalid_state("AE machine provider job is not active in this runtime");
    }
    auto storage = component_storage_for(active->second.interface_anchor_entity_id,
                                         active->second.scope_id);
    if (!storage) {
        dispatcher_.cancel_jobs_for_scope(active->second.scope_id);
        CancelledComponentAccess cancelled{content_->resource_runtime_index().key_context()};
        return dispatcher_.tick(handle, cancelled, max_operations);
    }
    AeAutocraftingStorageAccess access{*storage};
    return dispatcher_.tick(handle, access, max_operations);
}

std::optional<AePatternProviderJobSnapshot>
AeMachinePatternProviderRuntimeService::find_job(AePatternProviderJobHandle handle) const noexcept {
    return dispatcher_.find_job(handle);
}

bool AeMachinePatternProviderRuntimeService::remove_job(
    AePatternProviderJobHandle handle) noexcept {
    const bool removed = dispatcher_.remove_job(handle);
    if (removed) active_jobs_.erase(job_token(handle));
    return removed;
}

snt::core::Expected<void> AeMachinePatternProviderRuntimeService::tick(
    uint64_t tick_index, uint32_t max_operations_per_job) {
    if (max_operations_per_job == 0) {
        return invalid_argument("AE machine provider tick requires a positive operation budget");
    }
    std::vector<uint64_t> jobs;
    jobs.reserve(active_jobs_.size());
    for (const auto& [token, active] : active_jobs_) {
        static_cast<void>(active);
        jobs.push_back(token);
    }
    std::sort(jobs.begin(), jobs.end());
    for (const uint64_t token : jobs) {
        const auto active = active_jobs_.find(token);
        if (active == active_jobs_.end()) continue;
        const AePatternProviderJobHandle handle{
            .slot = static_cast<uint32_t>(token),
            .generation = static_cast<uint32_t>(token >> 32u),
        };
        auto stepped = tick_job(handle, max_operations_per_job);
        if (!stepped) {
            SNT_LOG_ERROR("AE machine provider job tick failed tick=%llu slot=%u: %s",
                          static_cast<unsigned long long>(tick_index),
                          static_cast<unsigned int>(handle.slot),
                          stepped.error().format().c_str());
            active_jobs_.erase(token);
            continue;
        }
        switch (stepped->state) {
            case AePatternProviderJobState::kCompleted:
            case AePatternProviderJobState::kCancelledByContentReload:
            case AePatternProviderJobState::kFailed: {
                const EntityId interface_anchor_entity_id =
                    active->second.interface_anchor_entity_id;
                active_jobs_.erase(token);
                LiveProvider* const provider = find_provider(interface_anchor_entity_id);
                if (provider != nullptr && provider->registration_refresh_pending) {
                    if (auto refreshed = refresh_provider_registration(*provider); !refreshed) {
                        return refreshed.error();
                    }
                }
                break;
            }
            case AePatternProviderJobState::kQueued:
            case AePatternProviderJobState::kWaitingForProvider:
            case AePatternProviderJobState::kBlockedMissingInput:
            case AePatternProviderJobState::kBlockedProvider:
            case AePatternProviderJobState::kBlockedOutputCapacity:
                break;
        }
    }
    return {};
}

snt::core::Expected<void>
AeMachinePatternProviderRuntimeService::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    return dispatcher_.prepare_resource_runtime_snapshot(std::move(next_snapshot));
}

void AeMachinePatternProviderRuntimeService::commit_resource_runtime_snapshot() noexcept {
    dispatcher_.commit_resource_runtime_snapshot();
}

void AeMachinePatternProviderRuntimeService::cancel_resource_runtime_snapshot() noexcept {
    dispatcher_.cancel_resource_runtime_snapshot();
}

uint64_t AeMachinePatternProviderRuntimeService::job_token(
    AePatternProviderJobHandle handle) noexcept {
    return (static_cast<uint64_t>(handle.generation) << 32u) | handle.slot;
}

}  // namespace snt::game
