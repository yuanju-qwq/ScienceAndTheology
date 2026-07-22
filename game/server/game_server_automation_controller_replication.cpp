// Dedicated-server automation-controller replication implementation.

#define SNT_LOG_CHANNEL "game.server_automation_controller_replication"
#include "game/server/game_server_automation_controller_replication.h"

#include "core/error.h"
#include "game/network/game_automation_controller_replication.h"
#include "game/simulation/automation_controller_runtime.h"

#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerAutomationControllerReplication>>
GameServerAutomationControllerReplication::create(
    const AutomationControllerRuntimeService& runtime) {
    return std::unique_ptr<GameServerAutomationControllerReplication>(
        new GameServerAutomationControllerReplication(runtime));
}

GameServerAutomationControllerReplication::GameServerAutomationControllerReplication(
    const AutomationControllerRuntimeService& runtime) noexcept
    : runtime_(&runtime) {}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerAutomationControllerReplication::collect_values(
    const GameAuthenticatedPeer&, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase) {
    if (runtime_ == nullptr) {
        return invalid_state("Automation controller replication has no active runtime service");
    }
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }

    GameAutomationControllerReplicationSnapshot snapshot;
    const std::vector<AutomationControllerRuntimePresentation> presentations =
        runtime_->collect_presentations(interest.chunks);
    snapshot.controllers.reserve(presentations.size());
    for (const AutomationControllerRuntimePresentation& presentation : presentations) {
        if (presentation.kind != AutomationControllerKind::kSfmManager) continue;
        snapshot.controllers.push_back({
            .anchor_chunk = presentation.anchor_chunk,
            .anchor_entity_id = presentation.anchor_entity_id.id,
            .root_x = presentation.root_x,
            .root_y = presentation.root_y,
            .root_z = presentation.root_z,
            .controller_key = presentation.controller_key,
            .authoritative_revision = presentation.authoritative_revision,
            .online = presentation.online,
            .sfm_program = presentation.sfm_program,
        });
    }
    auto payload = encode_game_automation_controller_replication_snapshot(snapshot);
    if (!payload) {
        auto error = payload.error();
        error.with_context("GameServerAutomationControllerReplication::collect_values(encode)");
        return error;
    }
    return std::vector<GameReplicationValue>{
        {
            .kind = GameReplicationValueKind::kAutomationControllers,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*payload),
        },
    };
}

}  // namespace snt::game::replication
