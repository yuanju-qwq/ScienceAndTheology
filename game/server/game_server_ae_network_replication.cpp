// Dedicated-server AE physical-topology replication source implementation.

#define SNT_LOG_CHANNEL "game.server_ae_network_replication"
#include "game/server/game_server_ae_network_replication.h"

#include "core/error.h"
#include "core/log.h"
#include "game/network/game_ae_network_replication.h"
#include "game/simulation/ae_network_runtime.h"

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

snt::core::Expected<std::unique_ptr<GameServerAeNetworkReplication>>
GameServerAeNetworkReplication::create(const AeNetworkRuntimeService& runtime) {
    return std::unique_ptr<GameServerAeNetworkReplication>(
        new GameServerAeNetworkReplication(runtime));
}

GameServerAeNetworkReplication::GameServerAeNetworkReplication(
    const AeNetworkRuntimeService& runtime) noexcept
    : runtime_(&runtime) {}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerAeNetworkReplication::collect_values(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase phase) {
    if (runtime_ == nullptr) {
        return invalid_state("AE network replication has no active runtime service");
    }
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }

    const std::vector<AeNetworkRuntimeNodePresentation> presentations =
        runtime_->collect_presentations(interest.chunks);
    const size_t payload_limit = std::min(
        static_cast<size_t>(budget.max_reliable_bytes_per_tick),
        kMaxGameAeNetworkReplicationPayloadBytes);
    if (payload_limit < kGameAeNetworkReplicationHeaderBytes) {
        return std::vector<GameReplicationValue>{};
    }

    GameAeNetworkReplicationSnapshot snapshot;
    snapshot.nodes.reserve(presentations.size());
    size_t encoded_size = kGameAeNetworkReplicationHeaderBytes;
    size_t omitted_count = 0;
    for (const AeNetworkRuntimeNodePresentation& presentation : presentations) {
        GameAeNetworkReplicationState state{
            .anchor_chunk = presentation.anchor_chunk,
            .anchor_entity_id = presentation.anchor_entity_id.id,
            .root_x = presentation.root_x,
            .root_y = presentation.root_y,
            .root_z = presentation.root_z,
            .type = presentation.type,
            .enabled = presentation.enabled,
            .online = presentation.online,
            .component_id = presentation.component_id,
            .provided_channels = presentation.provided_channels,
            .authoritative_revision = presentation.authoritative_revision,
            .topology_revision = runtime_->topology_revision(),
        };
        if (presentation.component_id != 0) {
            const auto component = runtime_->find_component(presentation.component_id);
            if (!component) {
                return invalid_state("AE topology node references an unavailable component");
            }
            state.component_node_count = component->node_count;
            state.component_controller_count = component->controller_count;
            state.component_total_channels = component->total_channels;
            state.component_online_devices = component->online_devices;
            state.component_offline_devices = component->offline_devices;
            state.component_powered = component->is_powered;
        }
        auto state_size = measure_game_ae_network_replication_state(state);
        if (!state_size) {
            auto error = state_size.error();
            error.with_context("GameServerAeNetworkReplication::collect_values(measure)");
            return error;
        }
        if (snapshot.nodes.size() >= kMaxGameAeNetworkReplicationStates ||
            *state_size > payload_limit - encoded_size) {
            ++omitted_count;
            continue;
        }
        encoded_size += *state_size;
        snapshot.nodes.push_back(std::move(state));
    }
    update_omission_log(peer.peer, presentations.size(), omitted_count, payload_limit, phase);
    auto payload = encode_game_ae_network_replication_snapshot(snapshot);
    if (!payload) {
        auto error = payload.error();
        error.with_context("GameServerAeNetworkReplication::collect_values(encode)");
        return error;
    }
    return std::vector<GameReplicationValue>{
        {
            .kind = GameReplicationValueKind::kAeNetworkNodes,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*payload),
        },
    };
}

void GameServerAeNetworkReplication::on_peer_disconnected(
    const GameAuthenticatedPeer& peer, std::string_view) noexcept {
    omission_log_states_.erase(peer.peer);
}

void GameServerAeNetworkReplication::update_omission_log(
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
        "AE network %s projection withheld %zu of %zu node(s) for peer %llu; value cap=%zu bytes",
        phase == GameReplicationValueCollectionPhase::kInitialSnapshot ? "initial" : "delta",
        omitted_count, candidate_count, static_cast<unsigned long long>(peer_id), payload_limit);
}

}  // namespace snt::game::replication
