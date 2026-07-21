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

bool resolve_stack_runtime_key(
    MachineItemStack& stack,
    const ResourceRuntimeIndex::Snapshot& resource_index,
    std::string& unresolved_item_key) {
    if (stack.empty()) {
        stack.runtime_key = {};
        return true;
    }
    if (!stack.resource.is_valid()) {
        unresolved_item_key = stack.resource.key.id;
        return false;
    }
    const auto runtime_key = resource_index.resolve_runtime(stack.resource.key);
    if (!runtime_key) {
        unresolved_item_key = stack.resource.key.id;
        return false;
    }
    stack.runtime_key = *runtime_key;
    return true;
}

bool resolve_recipe_runtime_keys(
    MachineRecipeSnapshot& recipe,
    const ResourceRuntimeIndex::Snapshot& resource_index,
    std::string& unresolved_item_key) {
    for (MachineItemStack& input : recipe.inputs) {
        if (!resolve_stack_runtime_key(input, resource_index, unresolved_item_key)) return false;
    }
    for (MachineItemStack& output : recipe.outputs) {
        if (!resolve_stack_runtime_key(output, resource_index, unresolved_item_key)) return false;
    }
    recipe.resource_runtime_index = resource_index;
    recipe.resource_runtime_generation = resource_index.generation();
    return true;
}

bool resolve_machine_runtime_keys(
    MachineRuntimeComponent& machine,
    const ResourceRuntimeIndex::Snapshot& current_resource_index,
    std::string& unresolved_item_key) {
    // An active recipe owns the mapping with which it was started. This keeps
    // its worker-only IDs coherent when a successful script reload publishes
    // a different current item table before the job completes.
    const auto& resource_index =
        machine.active_recipe && !machine.active_recipe->resource_runtime_index.empty()
            ? machine.active_recipe->resource_runtime_index
            : current_resource_index;
    for (MachineItemStack& input : machine.input_slots) {
        if (!resolve_stack_runtime_key(input, resource_index, unresolved_item_key)) return false;
    }
    for (MachineItemStack& output : machine.output_slots) {
        if (!resolve_stack_runtime_key(output, resource_index, unresolved_item_key)) return false;
    }
    if (machine.active_recipe &&
        !resolve_recipe_runtime_keys(*machine.active_recipe, resource_index, unresolved_item_key)) {
        return false;
    }
    return true;
}

std::optional<MachineRecipeSnapshot> make_snapshot(
    const RecipeDefinition& recipe,
    const ResourceRuntimeIndex::Snapshot& resource_index,
    std::string& unresolved_item_key) {
    MachineRecipeSnapshot snapshot;
    snapshot.id = recipe.id;
    snapshot.inputs.reserve(recipe.inputs.size());
    for (const auto& input : recipe.inputs) {
        snapshot.inputs.push_back(MachineItemStack::item(input.item_id, input.count));
    }
    snapshot.duration_ticks = recipe.duration_ticks;
    snapshot.energy_per_tick = recipe.energy_per_tick;
    snapshot.outputs.reserve(recipe.outputs.size());
    for (const auto& output : recipe.outputs) {
        snapshot.outputs.push_back(MachineItemStack::item(output.item_id, output.count));
    }
    if (!resolve_recipe_runtime_keys(snapshot, resource_index, unresolved_item_key)) {
        return std::nullopt;
    }
    return snapshot;
}

void normalize_stack(MachineItemStack& stack) {
    if (!stack.resource.is_valid() ||
        !stack.runtime_key.is_valid()) {
        stack.resource = {};
        stack.runtime_key = {};
    }
}

bool runtime_empty(const MachineItemStack& stack) {
    return !stack.resource.is_valid() ||
           !stack.runtime_key.is_valid();
}

void normalize_output_slots(MachineRuntimeComponent& machine) {
    for (auto& slot : machine.output_slots) normalize_stack(slot);
    std::erase_if(machine.output_slots, [](const MachineItemStack& slot) {
        return slot.empty();
    });
}

void normalize_input_slots(MachineRuntimeComponent& machine) {
    for (auto& slot : machine.input_slots) normalize_stack(slot);
    std::erase_if(machine.input_slots, [](const MachineItemStack& slot) {
        return slot.empty();
    });
}

bool has_required_inputs(const std::vector<MachineItemStack>& slots,
                         const std::vector<MachineItemStack>& requirements) {
    for (const MachineItemStack& requirement : requirements) {
        if (runtime_empty(requirement)) return false;
        int64_t available = 0;
        for (const MachineItemStack& slot : slots) {
            if (slot.runtime_key == requirement.runtime_key) available += slot.resource.amount;
        }
        if (available < requirement.resource.amount) return false;
    }
    return true;
}

bool reserve_inputs(MachineRuntimeComponent& machine,
                    const std::vector<MachineItemStack>& requirements) {
    if (!has_required_inputs(machine.input_slots, requirements)) return false;
    for (const MachineItemStack& requirement : requirements) {
        int64_t remaining = requirement.resource.amount;
        for (MachineItemStack& slot : machine.input_slots) {
            if (slot.runtime_key != requirement.runtime_key || remaining == 0) continue;
            const int64_t consumed = std::min(slot.resource.amount, remaining);
            slot.resource.amount -= consumed;
            remaining -= consumed;
        }
    }
    normalize_input_slots(machine);
    return true;
}

bool insert_outputs(std::vector<MachineItemStack>& slots,
                    int32_t max_slots,
                    int32_t max_stack_size,
                    const std::vector<MachineItemStack>& outputs) {
    if (max_slots <= 0 || max_stack_size <= 0) return false;

    for (const auto& output : outputs) {
        if (runtime_empty(output)) return false;
        auto existing = std::find_if(slots.begin(), slots.end(), [&output](const auto& slot) {
            return slot.runtime_key == output.runtime_key;
        });
        if (existing != slots.end()) {
            if (output.resource.amount > max_stack_size - existing->resource.amount) return false;
            existing->resource.amount += output.resource.amount;
            continue;
        }
        if (slots.size() >= static_cast<size_t>(max_slots) ||
            output.resource.amount > max_stack_size) {
            return false;
        }
        slots.push_back(output);
    }
    return true;
}

bool can_accept_outputs(const MachineRuntimeComponent& machine,
                        const MachineRecipeSnapshot& recipe) {
    std::vector<MachineItemStack> candidate = machine.output_slots;
    return insert_outputs(candidate,
                          machine.max_output_slots,
                          machine.max_stack_size,
                          recipe.outputs);
}

bool commit_outputs(MachineRuntimeComponent& machine,
                    const MachineRecipeSnapshot& recipe) {
    std::vector<MachineItemStack> candidate = machine.output_slots;
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
    std::string unresolved_item_key;
    if (!resolve_machine_runtime_keys(input.machine, resource_index, unresolved_item_key)) {
        return snt::core::Error{
            snt::core::ErrorCode::kInvalidState,
            "Machine execution has an unresolved item key: " + unresolved_item_key};
    }

    if (!input.machine.active_recipe && !input.machine.input_slots.empty()) {
        for (const RecipeDefinition& recipe :
             content_registry.recipes_for_machine(input.machine.machine_id)) {
            const auto snapshot = make_snapshot(recipe, resource_index, unresolved_item_key);
            if (!snapshot) {
                return snt::core::Error{
                    snt::core::ErrorCode::kInvalidState,
                    "Machine execution recipe has an unresolved item key: " + unresolved_item_key};
            }
            input.recipes.push_back(*snapshot);
        }
    }
    if (const MachineDefinition* definition =
            content_registry.find_machine(input.machine.machine_id)) {
        input.requires_manual_activation = definition->requires_manual_activation;
    }
    return input;
}

MachineExecutionResult advance_machine_execution(
    MachineExecutionInput input, uint64_t first_tick_index, uint64_t tick_count) {
    MachineTickResult result{
        .entity_guid = input.entity_guid,
        .machine = std::move(input.machine),
    };
    const bool started_with_active_recipe = result.machine.active_recipe.has_value();
    uint64_t advanced_ticks = 0;
    for (uint64_t offset = 0; offset < tick_count; ++offset) {
        // A manual machine may finish an already-reserved job offline, but it
        // never starts another one without a new authoritative activation.
        if (!input.allow_new_jobs && !result.machine.active_recipe) break;
        tick_machine(result, input.recipes, input.requires_manual_activation,
                     first_tick_index + offset);
        ++advanced_ticks;
        // An active recipe can carry an older item-runtime snapshot across a
        // content reload. Stop at its completion so a later execution input
        // rebuilds candidate recipes against the current snapshot instead of
        // mixing the two generations in one batch.
        if (started_with_active_recipe && !result.machine.active_recipe) break;
    }
    return {
        .machine = std::move(result.machine),
        .events = std::move(result.events),
        .advanced_ticks = advanced_ticks,
    };
}

MachineTickSystem::MachineTickSystem(GameContentRegistry& content_registry,
                                     IMachineTickEventSink* event_sink)
    : content_registry_(content_registry), event_sink_(event_sink) {}

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
    for (const entt::entity entity : entities) {
        const MachineRuntimeComponent& machine =
            world.get_component<MachineRuntimeComponent>(entity);
        MachineWorkItem work_item{
            world.guid_of(entity),
            machine,
            {},
            false,
        };
        std::string unresolved_item_key;
        if (!resolve_machine_runtime_keys(
                work_item.machine, current_resource_index, unresolved_item_key)) {
            const uint64_t generation =
                work_item.machine.active_recipe &&
                !work_item.machine.active_recipe->resource_runtime_index.empty()
                    ? work_item.machine.active_recipe->resource_runtime_generation
                    : current_resource_index.generation();
            log_unresolved_item_key(generation, unresolved_item_key);
            continue;
        }

        // GameContentRegistry is main-thread-only. Copy only the candidate
        // recipes needed by an idle machine. The copied recipe snapshots and
        // all captured stacks compare only RuntimeIds on workers.
        if (!work_item.machine.active_recipe && !work_item.machine.input_slots.empty()) {
            for (const RecipeDefinition& recipe :
                 content_registry_.recipes_for_machine(work_item.machine.machine_id)) {
                const auto snapshot = make_snapshot(
                    recipe, current_resource_index, unresolved_item_key);
                if (!snapshot) {
                    log_unresolved_item_key(current_resource_index.generation(), unresolved_item_key);
                    continue;
                }
                work_item.recipes.push_back(*snapshot);
            }
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
