// Game-owned graphical-client block interaction mapping implementation.

#include "client_block_interaction.h"

#include "core/error.h"

#include <limits>
#include <utility>

namespace snt::game {
namespace {

[[nodiscard]] snt::core::Error invalid_argument(std::string message) {
    return {snt::core::ErrorCode::kInvalidArgument, std::move(message)};
}

[[nodiscard]] bool valid_target(const GameClientBlockInteractionTarget& target) noexcept {
    return !target.dimension_id.empty() && target.hit_material <= 255;
}

}  // namespace

GameClientBlockInteractionController::GameClientBlockInteractionController(
    GameClientInteractionConfig config)
    : config_(std::move(config)) {}

snt::core::Expected<void> GameClientBlockInteractionController::handle_input(
    const GameClientBlockInteractionInput& input,
    const std::optional<GameClientBlockInteractionTarget>& target,
    std::string selected_item_id,
    const std::optional<GameClientMachineInteractionTarget>& machine,
    IGameClientBlockInteractionCommandSink& sink) const {
    if (!input.mine_pressed && !input.context_pressed) return {};
    if (!target.has_value()) return {};
    if (!valid_target(*target)) {
        return invalid_argument("Client block interaction target is invalid");
    }

    if (input.mine_pressed) {
        return sink.submit_block_interaction(
            make_hit_command(replication::GameBlockInteractionAction::kMine, *target));
    }

    if (machine.has_value()) {
        if (machine->has_collectible_output) {
            return sink.submit_block_interaction(make_hit_command(
                replication::GameBlockInteractionAction::kCollectMachineOutput, *target));
        }
        if (!machine->requires_manual_activation) return {};

        auto command = make_hit_command(
            replication::GameBlockInteractionAction::kActivateMachine, *target);
        command.selected_item_id = std::move(selected_item_id);
        command.client_hints = machine->activation_hints;
        return sink.submit_block_interaction(std::move(command));
    }

    if (target->hit_material == config_.bed_material_id) {
        return sink.submit_block_interaction(
            make_hit_command(replication::GameBlockInteractionAction::kUse, *target));
    }
    if (selected_item_id.empty() || !target->placement.has_value()) return {};

    const GameClientBlockInteractionTarget::PlacementCell& placement = *target->placement;
    auto command = make_hit_command(replication::GameBlockInteractionAction::kPlace, *target);
    command.block_x = placement.x;
    command.block_y = placement.y;
    command.block_z = placement.z;
    command.expected_material = placement.expected_material;
    command.selected_item_id = std::move(selected_item_id);
    return sink.submit_block_interaction(std::move(command));
}

replication::GameBlockInteractionCommand
GameClientBlockInteractionController::make_hit_command(
    replication::GameBlockInteractionAction action,
    const GameClientBlockInteractionTarget& target) const {
    return {
        .action = action,
        .dimension_id = target.dimension_id,
        .block_x = target.hit_x,
        .block_y = target.hit_y,
        .block_z = target.hit_z,
        .expected_material = target.hit_material,
    };
}

}  // namespace snt::game
