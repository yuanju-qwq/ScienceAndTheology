// Game-owned manual-machine interaction implementation.

#define SNT_LOG_CHANNEL "game.machine_interaction"
#include "game/simulation/machine_interaction_service.h"

#include "game/client/game_content_registry.h"
#include "game/client/machine_tick_system.h"

#include "core/error.h"
#include "core/log.h"
#include "ecs/world.h"

#include <algorithm>
#include <string>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool has_tool_tag(const MachineActivationContext& context,
                                 const std::string& required_tool_tag) {
    return std::find(context.held_tool_tags.begin(), context.held_tool_tags.end(),
                     required_tool_tag) != context.held_tool_tags.end();
}

}  // namespace

MachineInteractionService::MachineInteractionService(
    GameContentRegistry& content_registry) noexcept
    : content_registry_(content_registry) {}

snt::core::Expected<void> MachineInteractionService::request_manual_activation(
    snt::ecs::World& world,
    snt::ecs::EntityGuid machine_guid,
    const MachineActivationContext& context) {
    if (!machine_guid.valid()) {
        return invalid_argument("Manual machine activation requires a valid machine guid");
    }
    if (!context.target_is_reachable) {
        return invalid_state("Manual machine activation target is not reachable");
    }

    const entt::entity entity = world.find_entity_by_guid(machine_guid);
    if (entity == entt::null ||
        !world.registry().all_of<MachineRuntimeComponent>(entity)) {
        return invalid_state("Manual machine activation target does not exist");
    }

    MachineRuntimeComponent& runtime = world.get_component<MachineRuntimeComponent>(entity);
    const MachineDefinition* definition = content_registry_.find_machine(runtime.machine_id);
    if (definition == nullptr) {
        return invalid_state("Manual machine activation target has no registered machine definition");
    }
    if (!definition->requires_manual_activation) {
        return invalid_state("Machine does not require manual activation");
    }
    if (runtime.active_recipe.has_value() ||
        runtime.state != MachineRunState::WaitingForActivation) {
        return invalid_state("Machine is not waiting for manual activation");
    }
    if (runtime.activation_requested) {
        return invalid_state("Machine manual activation is already queued");
    }

    const MachineActivationRequirements& requirements =
        definition->activation_requirements;
    if (requirements.requires_cover && !context.cover_is_present) {
        return invalid_state("Machine activation requires cover");
    }
    if (requirements.requires_ignition && !context.ignition_is_present) {
        return invalid_state("Machine activation requires ignition");
    }
    if (requirements.requires_valid_structure && !context.structure_is_valid) {
        return invalid_state("Machine activation requires a valid structure");
    }
    if (!requirements.required_tool_tag.empty() &&
        !has_tool_tag(context, requirements.required_tool_tag)) {
        return invalid_state("Machine activation requires tool tag: " +
                             requirements.required_tool_tag);
    }

    runtime.activation_requested = true;
    // This state changes at most once per machine job. Rejecting routine
    // player actions is intentionally returned to the caller without logging
    // to avoid turning repeated input into high-frequency diagnostics.
    SNT_LOG_INFO("Queued manual machine activation guid=%llu machine=%s",
                 static_cast<unsigned long long>(machine_guid.value),
                 runtime.machine_id.c_str());
    return {};
}

}  // namespace snt::game
