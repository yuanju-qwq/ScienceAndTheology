// Dedicated-server automation-controller replication implementation.

#define SNT_LOG_CHANNEL "game.server_automation_controller_replication"
#include "game/server/game_server_automation_controller_replication.h"

#include "core/error.h"
#include "core/log.h"
#include "game/network/game_automation_controller_replication.h"
#include "game/simulation/automation_controller_runtime.h"

#include <algorithm>
#include <cstddef>
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
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase phase) {
    if (runtime_ == nullptr) {
        return invalid_state("Automation controller replication has no active runtime service");
    }
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }

    const std::vector<AutomationControllerRuntimePresentation> presentations =
        runtime_->collect_presentations(interest.chunks);
    const size_t payload_limit = std::min(
        static_cast<size_t>(budget.max_reliable_bytes_per_tick),
        kMaxGameAutomationControllerReplicationPayloadBytes);

    GameAutomationControllerReplicationSnapshot snapshot;
    snapshot.controllers.reserve(presentations.size());
    size_t encoded_size = kGameAutomationControllerReplicationHeaderBytes;
    size_t candidate_count = 0;
    size_t omitted_count = 0;
    for (const AutomationControllerRuntimePresentation& presentation : presentations) {
        if (presentation.kind != AutomationControllerKind::kSfmManager) continue;
        ++candidate_count;
        GameAutomationControllerReplicationState state{
            .anchor_chunk = presentation.anchor_chunk,
            .anchor_entity_id = presentation.anchor_entity_id.id,
            .root_x = presentation.root_x,
            .root_y = presentation.root_y,
            .root_z = presentation.root_z,
            .controller_key = presentation.controller_key,
            .authoritative_revision = presentation.authoritative_revision,
            .online = presentation.online,
            .sfm_program = presentation.sfm_program,
        };
        auto state_size = measure_game_automation_controller_replication_state(state);
        if (!state_size) {
            auto error = state_size.error();
            error.with_context(
                "GameServerAutomationControllerReplication::collect_values(measure)");
            return error;
        }
        if (snapshot.controllers.size() >= kMaxGameAutomationControllerStates ||
            payload_limit < encoded_size || *state_size > payload_limit - encoded_size) {
            ++omitted_count;
            continue;
        }
        encoded_size += *state_size;
        snapshot.controllers.push_back(std::move(state));
    }
    update_omission_log(peer.peer, candidate_count, omitted_count, payload_limit, phase);
    if (payload_limit < kGameAutomationControllerReplicationHeaderBytes) {
        return std::vector<GameReplicationValue>{};
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

void GameServerAutomationControllerReplication::on_peer_disconnected(
    const GameAuthenticatedPeer& peer, std::string_view) noexcept {
    omission_log_states_.erase(peer.peer);
}

void GameServerAutomationControllerReplication::update_omission_log(
    uint64_t peer_id, size_t candidate_count, size_t omitted_count, size_t payload_limit,
    GameReplicationValueCollectionPhase phase) noexcept {
    if (omitted_count == 0) {
        omission_log_states_.erase(peer_id);
        return;
    }
    const OmissionLogState next{
        .candidate_count = candidate_count,
        .omitted_count = omitted_count,
        .payload_limit = payload_limit,
    };
    const auto found = omission_log_states_.find(peer_id);
    if (found != omission_log_states_.end() &&
        found->second.candidate_count == next.candidate_count &&
        found->second.omitted_count == next.omitted_count &&
        found->second.payload_limit == next.payload_limit) {
        return;
    }
    omission_log_states_.insert_or_assign(peer_id, next);
    SNT_LOG_WARN(
        "Automation controller %s projection withheld %zu of %zu graph(s) for peer %llu; value cap=%zu bytes",
        phase == GameReplicationValueCollectionPhase::kInitialSnapshot ? "initial" : "delta",
        omitted_count, candidate_count, static_cast<unsigned long long>(peer_id), payload_limit);
}

}  // namespace snt::game::replication
