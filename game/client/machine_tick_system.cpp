// Game-owned deterministic machine worker implementation.

#define SNT_LOG_CHANNEL "gameplay"
#include "machine_tick_system.h"

#include "game_content_registry.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/log.h"
#include "ecs/world_command_queue.h"
#include "ecs/world.h"

namespace snt::game {
namespace {

struct MachineWorkItem {
    snt::ecs::EntityGuid entity_guid;
    MachineRuntimeComponent machine;
    std::vector<MachineRecipeSnapshot> recipes;
    bool requires_manual_activation = false;
};

struct MachineTickResult {
    snt::ecs::EntityGuid entity_guid;
    MachineRuntimeComponent machine;
    std::vector<MachineTickEvent> events;
};

// A task captures one machine at a time. Below this size, task allocation and
// synchronization cost more than the independent tick computation; above it,
// 16-machine tiles give the worker pool useful work without producing a job
// for every single entity.
constexpr size_t kMachineParallelForThreshold = 32;
constexpr int32_t kMachineParallelForBatchSize = 16;

const char* state_name(MachineRunState state) {
    switch (state) {
        case MachineRunState::Idle: return "idle";
        case MachineRunState::Running: return "running";
        case MachineRunState::NoMatchingRecipe: return "no_matching_recipe";
        case MachineRunState::WaitingForActivation: return "waiting_for_activation";
        case MachineRunState::WaitingForEnergy: return "waiting_for_energy";
        case MachineRunState::WaitingForOutput: return "waiting_for_output";
    }
    return "unknown";
}

bool rebind_stack(
    ResourceStack& stack,
    const ResourceRuntimeIndex::Snapshot& previous_resource_index,
    const ResourceRuntimeIndex::Snapshot& next_resource_index) {
    if (stack.is_absent()) return true;
    const auto rebound = rebind_resource_stack(
        stack, previous_resource_index, next_resource_index);
    if (!rebound || !rebound->is_valid()) return false;
    stack = *rebound;
    return true;
}

bool rebind_fluid_tank(
    MachineFluidTank& tank,
    const ResourceRuntimeIndex::Snapshot& previous_resource_index,
    const ResourceRuntimeIndex::Snapshot& next_resource_index) {
    if (tank.fluid.is_absent()) return true;
    if (!rebind_stack(tank.fluid, previous_resource_index, next_resource_index)) return false;
    const auto content = next_resource_index.resolve_content(tank.fluid.key);
    return content.has_value() && content->is_fluid();
}

[[nodiscard]] bool has_compact_fluid_resources(
    const std::vector<MachineFluidTank>& tanks) noexcept {
    return std::any_of(tanks.begin(), tanks.end(), [](const MachineFluidTank& tank) {
        return !tank.fluid.is_absent();
    });
}

[[nodiscard]] snt::core::Expected<MachineRecipeSnapshot> make_snapshot(
    const RecipeDefinition& recipe,
    const ResourceRuntimeIndex::Snapshot& resource_index) {
    MachineRecipeSnapshot snapshot;
    snapshot.id = recipe.id;
    snapshot.inputs.reserve(recipe.inputs.size());
    for (const auto& input : recipe.inputs) {
        const auto runtime_stack = resolve_resource_stack(
            ResourceContentStack::item(input.item_id, input.count), resource_index);
        if (!runtime_stack) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Machine recipe has an unresolved input resource: " + input.item_id};
        }
        snapshot.inputs.push_back(*runtime_stack);
    }
    snapshot.duration_ticks = recipe.duration_ticks;
    snapshot.energy_per_tick = recipe.energy_per_tick;
    snapshot.outputs.reserve(recipe.outputs.size());
    for (const auto& output : recipe.outputs) {
        const auto runtime_stack = resolve_resource_stack(
            ResourceContentStack::item(output.item_id, output.count), resource_index);
        if (!runtime_stack) {
            return snt::core::Error{
                snt::core::ErrorCode::kInvalidState,
                "Machine recipe has an unresolved output resource: " + output.item_id};
        }
        snapshot.outputs.push_back(*runtime_stack);
    }
    return snapshot;
}

[[nodiscard]] snt::core::Expected<void> rebind_machine_runtime(
    MachineRuntimeComponent& machine,
    const ResourceRuntimeIndex::Snapshot& next_resource_index) {
    const bool has_compact_resources =
        !machine.input_slots.empty() || !machine.output_slots.empty() ||
        machine.active_recipe.has_value() || has_compact_fluid_resources(machine.fluid_tanks);
    const ResourceRuntimeIndex::Snapshot previous_resource_index =
        machine.resource_runtime_index;
    if (!previous_resource_index.key_context().is_valid()) {
        if (!has_compact_resources) {
            machine.resource_runtime_index = next_resource_index;
            return {};
        }
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "Machine runtime has compact stacks without a source resource snapshot: " +
                machine.machine_id};
    }

    const auto rebind_stacks = [&previous_resource_index, &next_resource_index](
                                   std::vector<ResourceStack>& stacks) {
        for (ResourceStack& stack : stacks) {
            if (!rebind_stack(stack, previous_resource_index, next_resource_index)) {
                return false;
            }
        }
        return true;
    };
    if (!rebind_stacks(machine.input_slots) || !rebind_stacks(machine.output_slots) ||
        !std::all_of(machine.fluid_tanks.begin(), machine.fluid_tanks.end(),
                     [&previous_resource_index, &next_resource_index](MachineFluidTank& tank) {
                         return rebind_fluid_tank(
                             tank, previous_resource_index, next_resource_index);
                     }) ||
        (machine.active_recipe &&
         (!rebind_stacks(machine.active_recipe->inputs) ||
          !rebind_stacks(machine.active_recipe->outputs)))) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "Machine runtime references a resource removed by content reload: " +
                machine.machine_id};
    }
    machine.resource_runtime_index = next_resource_index;
    return {};
}

void normalize_stack(ResourceStack& stack) {
    if (!stack.is_valid()) stack = {};
}

bool runtime_empty(const ResourceStack& stack) {
    return !stack.is_valid();
}

void normalize_output_slots(MachineRuntimeComponent& machine) {
    for (auto& slot : machine.output_slots) normalize_stack(slot);
    std::erase_if(machine.output_slots, [](const ResourceStack& slot) {
        return slot.is_empty();
    });
}

void normalize_input_slots(MachineRuntimeComponent& machine) {
    for (auto& slot : machine.input_slots) normalize_stack(slot);
    std::erase_if(machine.input_slots, [](const ResourceStack& slot) {
        return slot.is_empty();
    });
}

bool has_required_inputs(const std::vector<ResourceStack>& slots,
                         const std::vector<ResourceStack>& requirements) {
    for (const ResourceStack& requirement : requirements) {
        if (runtime_empty(requirement)) return false;
        int64_t available = 0;
        for (const ResourceStack& slot : slots) {
            if (slot.key == requirement.key) available += slot.amount;
        }
        if (available < requirement.amount) return false;
    }
    return true;
}

bool reserve_inputs(MachineRuntimeComponent& machine,
                    const std::vector<ResourceStack>& requirements) {
    if (!has_required_inputs(machine.input_slots, requirements)) return false;
    for (const ResourceStack& requirement : requirements) {
        int64_t remaining = requirement.amount;
        for (ResourceStack& slot : machine.input_slots) {
            if (slot.key != requirement.key || remaining == 0) continue;
            const int64_t consumed = std::min(slot.amount, remaining);
            slot.amount -= consumed;
            remaining -= consumed;
        }
    }
    normalize_input_slots(machine);
    return true;
}

bool insert_outputs(std::vector<ResourceStack>& slots,
                    int32_t max_slots,
                    int32_t max_stack_size,
                    const std::vector<ResourceStack>& outputs) {
    if (max_slots <= 0 || max_stack_size <= 0) return false;

    for (const auto& output : outputs) {
        if (runtime_empty(output)) return false;
        auto existing = std::find_if(slots.begin(), slots.end(), [&output](const auto& slot) {
            return slot.key == output.key;
        });
        if (existing != slots.end()) {
            if (output.amount > max_stack_size - existing->amount) return false;
            existing->amount += output.amount;
            continue;
        }
        if (slots.size() >= static_cast<size_t>(max_slots) ||
            output.amount > max_stack_size) {
            return false;
        }
        slots.push_back(output);
    }
    return true;
}

bool can_accept_outputs(const MachineRuntimeComponent& machine,
                        const MachineRecipeSnapshot& recipe) {
    std::vector<ResourceStack> candidate = machine.output_slots;
    return insert_outputs(candidate,
                          machine.max_output_slots,
                          machine.max_stack_size,
                          recipe.outputs);
}

bool commit_outputs(MachineRuntimeComponent& machine,
                    const MachineRecipeSnapshot& recipe) {
    std::vector<ResourceStack> candidate = machine.output_slots;
    if (!insert_outputs(candidate,
                        machine.max_output_slots,
                        machine.max_stack_size,
                        recipe.outputs)) {
        return false;
    }
    machine.output_slots = std::move(candidate);
    return true;
}

bool is_diagnostic_state(MachineRunState state) {
    return state == MachineRunState::NoMatchingRecipe ||
           state == MachineRunState::WaitingForEnergy ||
           state == MachineRunState::WaitingForOutput;
}

void transition(MachineTickResult& result,
                MachineRunState state,
                const std::string& recipe_id,
                uint64_t tick_index) {
    MachineRuntimeComponent& machine = result.machine;
    if (machine.state == state) return;

    const MachineRunState previous_state = machine.state;
    machine.state = state;
    result.events.push_back({
        .kind = MachineTickEventKind::StateChanged,
        .tick_index = tick_index,
        .entity_guid = result.entity_guid,
        .machine_id = machine.machine_id,
        .recipe_id = recipe_id,
        .previous_state = previous_state,
        .state = state,
    });
}

void publish_completion(MachineTickResult& result,
                        const MachineRecipeSnapshot& recipe,
                        uint64_t tick_index) {
    result.events.push_back({
        .kind = MachineTickEventKind::RecipeCompleted,
        .tick_index = tick_index,
        .entity_guid = result.entity_guid,
        .machine_id = result.machine.machine_id,
        .recipe_id = recipe.id,
        .account_id = result.machine.job_owner_account_id,
        .outputs = recipe.outputs,
        .resource_runtime_index = result.machine.resource_runtime_index,
        .completed_jobs = 1,
        .previous_state = result.machine.state,
        .state = result.machine.state,
    });
}

void tick_machine(MachineTickResult& result,
                  const std::vector<MachineRecipeSnapshot>& recipes,
                  bool requires_manual_activation,
                  uint64_t tick_index) {
    MachineRuntimeComponent& machine = result.machine;
    normalize_input_slots(machine);
    normalize_output_slots(machine);
    machine.stored_energy = std::max(machine.stored_energy, 0);

    if (!machine.active_recipe) {
        if (machine.input_slots.empty()) {
            machine.activation_requested = false;
            machine.job_owner_account_id.clear();
            transition(result, MachineRunState::Idle, "", tick_index);
            return;
        }

        const auto recipe = std::find_if(
            recipes.begin(), recipes.end(), [&machine](const MachineRecipeSnapshot& value) {
                return has_required_inputs(machine.input_slots, value.inputs);
            });
        if (recipe == recipes.end()) {
            machine.activation_requested = false;
            machine.job_owner_account_id.clear();
            transition(result, MachineRunState::NoMatchingRecipe, "", tick_index);
            return;
        }

        MachineRecipeSnapshot snapshot = *recipe;
        if (requires_manual_activation && !machine.activation_requested) {
            machine.job_owner_account_id.clear();
            transition(result, MachineRunState::WaitingForActivation, snapshot.id, tick_index);
            return;
        }
        if (!can_accept_outputs(machine, snapshot)) {
            machine.activation_requested = false;
            machine.job_owner_account_id.clear();
            transition(result, MachineRunState::WaitingForOutput, snapshot.id, tick_index);
            return;
        }

        // Reserve every recipe input at start. This copy is later applied as
        // one deterministic World command at the fixed-tick barrier.
        if (!reserve_inputs(machine, snapshot.inputs)) {
            machine.job_owner_account_id.clear();
            transition(result, MachineRunState::NoMatchingRecipe, snapshot.id, tick_index);
            return;
        }
        if (!requires_manual_activation) machine.job_owner_account_id.clear();
        machine.activation_requested = false;
        machine.progress_ticks = 0;
        machine.active_recipe = std::move(snapshot);
    }

    MachineRecipeSnapshot& active = *machine.active_recipe;
    if (machine.progress_ticks >= active.duration_ticks) {
        if (!commit_outputs(machine, active)) {
            transition(result, MachineRunState::WaitingForOutput, active.id, tick_index);
            return;
        }
        publish_completion(result, active, tick_index);
        const std::string recipe_id = active.id;
        machine.active_recipe.reset();
        machine.progress_ticks = 0;
        machine.job_owner_account_id.clear();
        transition(result, MachineRunState::Idle, recipe_id, tick_index);
        return;
    }

    if (active.energy_per_tick > machine.stored_energy) {
        transition(result, MachineRunState::WaitingForEnergy, active.id, tick_index);
        return;
    }

    machine.stored_energy -= active.energy_per_tick;
    ++machine.progress_ticks;
    transition(result, MachineRunState::Running, active.id, tick_index);

    if (machine.progress_ticks < active.duration_ticks) return;

    if (!commit_outputs(machine, active)) {
        transition(result, MachineRunState::WaitingForOutput, active.id, tick_index);
        return;
    }
    publish_completion(result, active, tick_index);
    const std::string recipe_id = active.id;
    machine.active_recipe.reset();
    machine.progress_ticks = 0;
    machine.job_owner_account_id.clear();
    transition(result, MachineRunState::Idle, recipe_id, tick_index);
}

MachineTickResult compute_tick_result(const MachineWorkItem& work_item, uint64_t tick_index) {
    MachineTickResult result{
        work_item.entity_guid,
        work_item.machine,
        {},
    };
    tick_machine(result, work_item.recipes, work_item.requires_manual_activation, tick_index);
    return result;
}

void publish_events(const std::vector<MachineTickEvent>& events,
                    IMachineTickEventSink* event_sink) {
    for (const MachineTickEvent& event : events) {
        if (event.kind == MachineTickEventKind::StateChanged &&
            (is_diagnostic_state(event.previous_state) || is_diagnostic_state(event.state))) {
            SNT_LOG_INFO("P7 machine %llu (%s) state %s -> %s%s%s",
                         static_cast<unsigned long long>(event.entity_guid.value),
                         event.machine_id.c_str(),
                         state_name(event.previous_state),
                         state_name(event.state),
                         event.recipe_id.empty() ? "" : " recipe=",
                         event.recipe_id.empty() ? "" : event.recipe_id.c_str());
        }
        if (event_sink) event_sink->on_machine_tick_event(event);
    }
}

class MachineTickTask final : public snt::ecs::IWorkerTask {
public:
    MachineTickTask(std::vector<MachineWorkItem> work_items,
                    IMachineTickEventSink* event_sink,
                    uint64_t tick_index)
        : work_items_(std::move(work_items)), event_sink_(event_sink), tick_index_(tick_index) {}

    void execute(snt::ecs::WorkerCommandContext& commands) override {
        std::vector<MachineTickResult> results;
        results.reserve(work_items_.size());

        const bool can_parallelize =
            work_items_.size() >= kMachineParallelForThreshold &&
            work_items_.size() <=
                static_cast<size_t>(std::numeric_limits<int32_t>::max());
        if (can_parallelize) {
            // Every child owns one preallocated result slot. It cannot touch
            // World, the command sequence, or the event sink; the parent
            // enqueues all commands after this synchronous compute phase.
            results.resize(work_items_.size());
            const auto* work_items = &work_items_;
            commands.parallel_for(
                static_cast<int32_t>(work_items_.size()),
                [work_items, &results, tick_index = tick_index_](int32_t, int32_t item_index) {
                    const size_t index = static_cast<size_t>(item_index);
                    results[index] = compute_tick_result((*work_items)[index], tick_index);
                },
                kMachineParallelForBatchSize);
        } else {
            for (const MachineWorkItem& work_item : work_items_) {
                results.push_back(compute_tick_result(work_item, tick_index_));
            }
        }

        // capture() sorted work_items_ by EntityGuid. Serial enqueue preserves
        // that order even when the pure computation tiles completed in a
        // different order on worker threads.
        for (MachineTickResult& result : results) {
            commands.enqueue([result = std::move(result), event_sink = event_sink_]
                             (snt::ecs::World& world) mutable {
                const entt::entity entity = world.find_entity_by_guid(result.entity_guid);
                if (entity == entt::null ||
                    !world.registry().all_of<MachineRuntimeComponent>(entity)) {
                    return;
                }

                world.get_component<MachineRuntimeComponent>(entity) =
                    std::move(result.machine);
                publish_events(result.events, event_sink);
            });
        }
    }

private:
    std::vector<MachineWorkItem> work_items_;
    IMachineTickEventSink* event_sink_ = nullptr;
    uint64_t tick_index_ = 0;
};

}  // namespace

snt::core::Expected<MachineExecutionInput> make_machine_execution_input(
    GameContentRegistry& content_registry,
    snt::ecs::EntityGuid entity_guid,
    MachineRuntimeComponent machine) {
    MachineExecutionInput input{
        .entity_guid = entity_guid,
        .machine = std::move(machine),
    };
    const ResourceRuntimeIndex::Snapshot resource_index =
        content_registry.resource_runtime_index();
    if (!input.machine.resource_runtime_index.key_context().is_valid() &&
        input.machine.input_slots.empty() && input.machine.output_slots.empty() &&
        !input.machine.active_recipe &&
        !has_compact_fluid_resources(input.machine.fluid_tanks)) {
        input.machine.resource_runtime_index = resource_index;
    }
    if (!input.machine.resource_runtime_index.key_context().matches(
            resource_index.key_context())) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "Machine execution resource snapshot does not match current content"};
    }

    if (!input.machine.active_recipe && !input.machine.input_slots.empty()) {
        auto recipes = compile_machine_recipe_snapshots(
            content_registry, input.machine.machine_id, resource_index);
        if (!recipes) return recipes.error();
        input.recipes = std::move(*recipes);
    }
    if (const MachineDefinition* definition =
            content_registry.find_machine(input.machine.machine_id)) {
        input.requires_manual_activation = definition->requires_manual_activation;
    }
    return input;
}

snt::core::Expected<std::vector<MachineRecipeSnapshot>>
compile_machine_recipe_snapshots(
    GameContentRegistry& content_registry,
    std::string_view machine_id,
    const ResourceRuntimeIndex::Snapshot& resource_runtime_index) {
    std::vector<MachineRecipeSnapshot> recipes;
    for (const RecipeDefinition& recipe : content_registry.recipes_for_machine(machine_id)) {
        auto snapshot = make_snapshot(recipe, resource_runtime_index);
        if (!snapshot) return snapshot.error();
        recipes.push_back(std::move(*snapshot));
    }
    return recipes;
}

MachineExecutionResult advance_machine_execution(
    snt::ecs::EntityGuid entity_guid,
    MachineRuntimeComponent machine,
    const std::vector<MachineRecipeSnapshot>& recipes,
    bool requires_manual_activation,
    bool allow_new_jobs,
    uint64_t first_tick_index,
    uint64_t tick_count) {
    MachineTickResult result{
        .entity_guid = entity_guid,
        .machine = std::move(machine),
    };
    uint64_t advanced_ticks = 0;
    for (uint64_t offset = 0; offset < tick_count; ++offset) {
        // A manual machine may finish an already-reserved job offline, but it
        // never starts another one without a new authoritative activation.
        if (!allow_new_jobs && !result.machine.active_recipe) break;
        tick_machine(result, recipes, requires_manual_activation,
                     first_tick_index + offset);
        ++advanced_ticks;
    }
    return {
        .machine = std::move(result.machine),
        .events = std::move(result.events),
        .advanced_ticks = advanced_ticks,
    };
}

MachineExecutionResult advance_machine_execution(
    MachineExecutionInput input, uint64_t first_tick_index, uint64_t tick_count) {
    return advance_machine_execution(
        input.entity_guid, std::move(input.machine), input.recipes,
        input.requires_manual_activation, input.allow_new_jobs,
        first_tick_index, tick_count);
}

MachineTickSystem::MachineTickSystem(GameContentRegistry& content_registry,
                                     IMachineTickEventSink* event_sink)
    : content_registry_(content_registry), event_sink_(event_sink) {}

snt::core::Expected<std::vector<MachineRecipeSnapshot>>
MachineTickSystem::compile_recipes_for_machine(
    std::string_view machine_id,
    const ResourceRuntimeIndex::Snapshot& resource_index) const {
    return compile_machine_recipe_snapshots(content_registry_, machine_id, resource_index);
}

snt::core::Expected<void> MachineTickSystem::ensure_recipe_cache(
    const ResourceRuntimeIndex::Snapshot& resource_index) {
    if (recipe_cache_ && recipe_cache_->resource_runtime_index.key_context().matches(
            resource_index.key_context())) {
        return {};
    }

    RecipeCache next_cache;
    next_cache.resource_runtime_index = resource_index;
    if (world_ != nullptr) {
        const auto view = world_->registry().view<MachineRuntimeComponent>();
        for (const entt::entity entity : view) {
            const MachineRuntimeComponent& runtime =
                world_->get_component<MachineRuntimeComponent>(entity);
            if (next_cache.by_machine.contains(runtime.machine_id)) continue;
            auto recipes = compile_recipes_for_machine(runtime.machine_id, resource_index);
            if (!recipes) return recipes.error();
            next_cache.by_machine.emplace(runtime.machine_id, std::move(*recipes));
        }
    }
    recipe_cache_ = std::move(next_cache);
    return {};
}

snt::core::Expected<void> MachineTickSystem::prepare_resource_runtime_snapshot(
    ResourceRuntimeIndex::Snapshot next_snapshot) {
    if (pending_resource_runtime_snapshot_) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "Machine runtime snapshot publication is already prepared"};
    }

    PendingResourceRuntimeSnapshot pending;
    pending.recipe_cache.resource_runtime_index = next_snapshot;
    if (world_ != nullptr) {
        const auto view = world_->registry().view<MachineRuntimeComponent>();
        for (const entt::entity entity : view) {
            MachineRuntimeComponent runtime =
                world_->get_component<MachineRuntimeComponent>(entity);
            if (auto result = rebind_machine_runtime(runtime, next_snapshot); !result) {
                auto error = result.error();
                error.with_context("MachineTickSystem::prepare_resource_runtime_snapshot");
                return error;
            }
            if (!pending.recipe_cache.by_machine.contains(runtime.machine_id)) {
                auto recipes = compile_recipes_for_machine(runtime.machine_id, next_snapshot);
                if (!recipes) {
                    auto error = recipes.error();
                    error.with_context(
                        "MachineTickSystem::prepare_resource_runtime_snapshot(recipe cache)");
                    return error;
                }
                pending.recipe_cache.by_machine.emplace(runtime.machine_id, std::move(*recipes));
            }
            pending.machines.push_back({world_->guid_of(entity), std::move(runtime)});
        }
    }
    pending_resource_runtime_snapshot_ = std::move(pending);
    return {};
}

void MachineTickSystem::commit_resource_runtime_snapshot() noexcept {
    if (!pending_resource_runtime_snapshot_) return;

    PendingResourceRuntimeSnapshot pending = std::move(*pending_resource_runtime_snapshot_);
    pending_resource_runtime_snapshot_.reset();
    if (world_ != nullptr) {
        for (PendingResourceRuntimeSnapshot::Machine& machine : pending.machines) {
            const entt::entity entity = world_->find_entity_by_guid(machine.entity_guid);
            if (entity == entt::null ||
                !world_->registry().all_of<MachineRuntimeComponent>(entity)) {
                continue;
            }
            world_->get_component<MachineRuntimeComponent>(entity) = std::move(machine.runtime);
        }
    }
    recipe_cache_ = std::move(pending.recipe_cache);
}

void MachineTickSystem::cancel_resource_runtime_snapshot() noexcept {
    pending_resource_runtime_snapshot_.reset();
}

void MachineTickSystem::log_unresolved_item_key(uint64_t generation,
                                                 std::string_view key) {
    if (unresolved_item_log_generation_ != generation) {
        unresolved_item_log_generation_ = generation;
        unresolved_item_keys_logged_.clear();
    }
    if (!unresolved_item_keys_logged_.emplace(key).second) return;
    SNT_LOG_WARN("Machine worker skipped unresolved item key '%.*s' in runtime generation %llu",
                 static_cast<int>(key.size()), key.data(),
                 static_cast<unsigned long long>(generation));
}

std::unique_ptr<snt::ecs::IWorkerTask> MachineTickSystem::capture(
    const snt::ecs::World& world, float /*dt*/) {
    std::vector<entt::entity> entities;
    const auto view = world.registry().view<MachineRuntimeComponent>();
    for (const entt::entity entity : view) entities.push_back(entity);

    // Registry storage order is not a cross-run simulation contract. Stable
    // EntityGuid order also becomes the command order after the worker barrier.
    std::sort(entities.begin(), entities.end(), [&world](entt::entity lhs, entt::entity rhs) {
        return world.guid_of(lhs).value < world.guid_of(rhs).value;
    });

    std::vector<MachineWorkItem> work_items;
    work_items.reserve(entities.size());
    const ResourceRuntimeIndex::Snapshot current_resource_index =
        content_registry_.resource_runtime_index();
    if (auto result = ensure_recipe_cache(current_resource_index); !result) {
        SNT_LOG_ERROR("Machine worker could not build recipe cache for runtime generation %llu: %s",
                      static_cast<unsigned long long>(current_resource_index.generation()),
                      result.error().format().c_str());
        return nullptr;
    }
    for (const entt::entity entity : entities) {
        const MachineRuntimeComponent& machine =
            world.get_component<MachineRuntimeComponent>(entity);
        MachineWorkItem work_item{
            world.guid_of(entity),
            machine,
            {},
            false,
        };
        if (!work_item.machine.resource_runtime_index.key_context().is_valid() &&
            work_item.machine.input_slots.empty() && work_item.machine.output_slots.empty() &&
            !work_item.machine.active_recipe &&
            !has_compact_fluid_resources(work_item.machine.fluid_tanks)) {
            work_item.machine.resource_runtime_index = current_resource_index;
        }
        if (!work_item.machine.resource_runtime_index.key_context().matches(
                current_resource_index.key_context())) {
            log_unresolved_item_key(current_resource_index.generation(),
                                    "resource snapshot mismatch for " +
                                        work_item.machine.machine_id);
            continue;
        }

        // GameContentRegistry is main-thread-only. Recipes are compiled to
        // compact ResourceStack values when a snapshot is prepared, then
        // copied here without any string-key resolution on the tick path.
        if (!work_item.machine.active_recipe && !work_item.machine.input_slots.empty()) {
            auto cached = recipe_cache_->by_machine.find(work_item.machine.machine_id);
            if (cached == recipe_cache_->by_machine.end()) {
                auto recipes = compile_recipes_for_machine(
                    work_item.machine.machine_id, current_resource_index);
                if (!recipes) {
                    log_unresolved_item_key(current_resource_index.generation(),
                                            work_item.machine.machine_id);
                    continue;
                }
                cached = recipe_cache_->by_machine.emplace(
                    work_item.machine.machine_id, std::move(*recipes)).first;
            }
            work_item.recipes = cached->second;
            if (const MachineDefinition* definition =
                    content_registry_.find_machine(work_item.machine.machine_id)) {
                work_item.requires_manual_activation = definition->requires_manual_activation;
            }
        }
        work_items.push_back(std::move(work_item));
    }

    if (work_items.empty()) return nullptr;
    return std::make_unique<MachineTickTask>(std::move(work_items), event_sink_, tick_index_);
}

}  // namespace snt::game
