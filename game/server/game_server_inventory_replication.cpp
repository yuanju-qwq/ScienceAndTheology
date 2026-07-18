// Dedicated-server player inventory replication and transfer implementation.

#define SNT_LOG_CHANNEL "game.server_inventory"
#include "game/server/game_server_inventory_replication.h"

#include "game/server/game_server_player_lifecycle.h"
#include "game/server/game_server_player_state.h"

#include "core/error.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

[[nodiscard]] bool same_inventory(const GamePlayerInventory& left,
                                  const GamePlayerInventory& right) noexcept {
    return left == right;
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerInventoryReplication>>
GameServerInventoryReplication::create(
    GameServerPlayerState& player_state,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink) {
    return std::unique_ptr<GameServerInventoryReplication>(
        new GameServerInventoryReplication(player_state, checkpoint_sink));
}

GameServerInventoryReplication::GameServerInventoryReplication(
    GameServerPlayerState& player_state,
    IGameServerPlayerStateCheckpointSink* checkpoint_sink) noexcept
    : player_state_(&player_state), checkpoint_sink_(checkpoint_sink) {}

snt::core::Expected<void> GameServerInventoryReplication::submit_slot_transfer(
    const GameAuthenticatedPeer& peer, const GameInventorySlotTransferCommand& command) {
    if (player_state_ == nullptr) {
        return invalid_state("Player inventory replication has no authoritative player state");
    }
    if (auto result = validate_game_inventory_slot_transfer_command(command); !result) {
        return result.error();
    }
    auto account = synchronize_account(peer);
    if (!account) return account.error();
    if (command.expected_inventory_revision != (*account)->inventory_revision) {
        return record_response(**account, command.request_id,
                               GameInventorySlotTransferOutcome::kRejected,
                               "inventory revision is stale");
    }

    const GamePlayerInventorySlotTransfer transfer{
        .source_slot = command.source_slot,
        .target_slot = command.target_slot,
        .count = command.count,
        .expected_source = command.expected_source,
        .expected_target = command.expected_target,
    };
    auto applicable = player_state_->can_apply_inventory_slot_transfer(peer, transfer);
    if (!applicable) {
        return applicable.error();
    }
    if (!*applicable) {
        return record_response(**account, command.request_id,
                               GameInventorySlotTransferOutcome::kRejected,
                               "authoritative inventory rejected the slot transfer");
    }
    if (checkpoint_sink_ != nullptr) {
        if (auto result = checkpoint_sink_->mark_player_state_dirty(peer); !result) {
            return result.error();
        }
    }
    if (auto result = player_state_->apply_inventory_slot_transfer(peer, transfer); !result) {
        return result.error();
    }
    account = synchronize_account(peer);
    if (!account) return account.error();
    return record_response(**account, command.request_id,
                           GameInventorySlotTransferOutcome::kAccepted);
}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerInventoryReplication::collect_values(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest&,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&,
    GameReplicationValueCollectionPhase phase) {
    if (player_state_ == nullptr) {
        return invalid_state("Player inventory replication has no authoritative player state");
    }
    if (peer.peer == snt::network::kInvalidPeerId) {
        return invalid_state("Player inventory replication requires an authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) return result.error();
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }
    auto account = synchronize_account(peer);
    if (!account) return account.error();

    ObserverState& observer = observers_[peer.peer];
    if (observer.account_id != peer.identity.account_id) {
        observer = {};
        observer.account_id = peer.identity.account_id;
    }
    auto value = phase == GameReplicationValueCollectionPhase::kInitialSnapshot
        ? prepare_snapshot_value(peer, **account, observer, phase)
        : prepare_delta_value(peer, **account, observer);
    if (!value) return value.error();
    return std::vector<GameReplicationValue>{std::move(*value)};
}

void GameServerInventoryReplication::on_values_committed(
    const GameAuthenticatedPeer& peer, GameReplicationValueCollectionPhase phase,
    std::span<const GameReplicationValue> values) noexcept {
    const auto observer = observers_.find(peer.peer);
    if (observer == observers_.end() || !observer->second.pending.has_value()) return;
    PendingValue& pending = *observer->second.pending;
    if (pending.phase != phase) return;
    const auto accepted = std::find_if(
        values.begin(), values.end(), [&pending](const GameReplicationValue& value) {
            return value.kind == GameReplicationValueKind::kPlayerInventory &&
                   value.operation == GameReplicationValueOperation::kUpsert &&
                   value.payload == pending.payload;
        });
    if (accepted == values.end()) return;

    ObserverState& state = observer->second;
    state.inventory = std::move(pending.inventory);
    state.inventory_revision = pending.inventory_revision;
    state.response_revision = pending.response_revision;
    state.last_payload = std::move(pending.payload);
    state.initialized = true;
    state.pending.reset();
}

void GameServerInventoryReplication::on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                                           std::string_view) noexcept {
    observers_.erase(peer.peer);
}

snt::core::Expected<GameServerInventoryReplication::AccountState*>
GameServerInventoryReplication::synchronize_account(const GameAuthenticatedPeer& peer) {
    if (player_state_ == nullptr) {
        return invalid_state("Player inventory replication has no authoritative player state");
    }
    if (auto result = validate_player_identity(peer.identity); !result) return result.error();
    auto inventory = player_state_->inventory_for_peer(peer);
    if (!inventory) return inventory.error();

    AccountState& state = accounts_[peer.identity.account_id];
    if (!state.initialized) {
        state.inventory = std::move(*inventory);
        state.inventory_revision = 1;
        state.initialized = true;
        return &state;
    }
    if (!same_inventory(state.inventory, *inventory)) {
        if (state.inventory_revision == std::numeric_limits<uint64_t>::max()) {
            return invalid_state("Player inventory revision sequence is exhausted");
        }
        state.inventory = std::move(*inventory);
        ++state.inventory_revision;
    }
    return &state;
}

snt::core::Expected<void> GameServerInventoryReplication::record_response(
    AccountState& state, uint64_t request_id, GameInventorySlotTransferOutcome outcome,
    std::string rejection_reason) {
    if (request_id == 0 ||
        (outcome != GameInventorySlotTransferOutcome::kAccepted &&
         outcome != GameInventorySlotTransferOutcome::kRejected)) {
        return invalid_state("Player inventory transfer response has an invalid identity");
    }
    if (state.response_revision == std::numeric_limits<uint64_t>::max()) {
        return invalid_state("Player inventory response revision sequence is exhausted");
    }
    if (outcome == GameInventorySlotTransferOutcome::kAccepted) rejection_reason.clear();
    if (rejection_reason.size() > kMaxGameInventoryResponseReasonBytes) {
        rejection_reason.resize(kMaxGameInventoryResponseReasonBytes);
    }
    ++state.response_revision;
    state.response = {
        .request_id = request_id,
        .outcome = outcome,
        .rejection_reason = std::move(rejection_reason),
    };
    return {};
}

snt::core::Expected<GameReplicationValue>
GameServerInventoryReplication::prepare_snapshot_value(
    const GameAuthenticatedPeer& peer, const AccountState& account,
    ObserverState& observer, GameReplicationValueCollectionPhase phase) {
    auto payload = encode_game_inventory_snapshot({
        .account_id = peer.identity.account_id,
        .inventory_revision = account.inventory_revision,
        .response_revision = account.response_revision,
        .response = account.response,
        .inventory = account.inventory,
    });
    if (!payload) return payload.error();
    observer.pending = PendingValue{
        .inventory = account.inventory,
        .inventory_revision = account.inventory_revision,
        .response_revision = account.response_revision,
        .response = account.response,
        .phase = phase,
        .payload = *payload,
    };
    return GameReplicationValue{
        .kind = GameReplicationValueKind::kPlayerInventory,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = std::move(*payload),
    };
}

snt::core::Expected<GameReplicationValue>
GameServerInventoryReplication::prepare_delta_value(
    const GameAuthenticatedPeer& peer, const AccountState& account, ObserverState& observer) {
    if (!observer.initialized) {
        return prepare_snapshot_value(peer, account, observer,
                                      GameReplicationValueCollectionPhase::kDelta);
    }
    if (observer.pending.has_value()) {
        return GameReplicationValue{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = observer.pending->payload,
        };
    }
    if (observer.inventory.max_slots != account.inventory.max_slots ||
        observer.inventory.max_stack_size != account.inventory.max_stack_size ||
        observer.inventory.slots.size() != account.inventory.slots.size()) {
        return prepare_snapshot_value(peer, account, observer,
                                      GameReplicationValueCollectionPhase::kDelta);
    }

    const bool inventory_changed = observer.inventory_revision != account.inventory_revision ||
        !same_inventory(observer.inventory, account.inventory);
    const bool response_changed = observer.response_revision != account.response_revision;
    if (!inventory_changed && !response_changed) {
        if (observer.last_payload.empty()) {
            return invalid_state("Player inventory observer has no committed replication payload");
        }
        return GameReplicationValue{
            .kind = GameReplicationValueKind::kPlayerInventory,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = observer.last_payload,
        };
    }

    GameInventoryDelta delta{
        .account_id = peer.identity.account_id,
        .inventory_revision = account.inventory_revision,
        .response_revision = account.response_revision,
        .response = response_changed ? account.response : GameInventorySlotTransferResponse{},
    };
    if (inventory_changed) {
        for (size_t index = 0; index < account.inventory.slots.size(); ++index) {
            if (observer.inventory.slots[index] != account.inventory.slots[index]) {
                delta.changed_slots.push_back({
                    .slot_index = static_cast<uint16_t>(index),
                    .stack = account.inventory.slots[index],
                });
            }
        }
    }
    auto payload = encode_game_inventory_delta(delta);
    if (!payload) return payload.error();
    observer.pending = PendingValue{
        .inventory = account.inventory,
        .inventory_revision = account.inventory_revision,
        .response_revision = account.response_revision,
        .response = account.response,
        .phase = GameReplicationValueCollectionPhase::kDelta,
        .payload = *payload,
    };
    return GameReplicationValue{
        .kind = GameReplicationValueKind::kPlayerInventory,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = std::move(*payload),
    };
}

}  // namespace snt::game::replication
