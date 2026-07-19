// Dedicated-server player inventory replication and transfer service.
//
// Ownership: this composition-owned module is the only bridge between the
// typed SNTG inventory command, GameServerPlayerState's atomic slot transfer,
// dirty-checkpoint marking, and the player-only inventory replication value.
// It contains no transport, UI, or ECS handles outside GameServerPlayerState.

#pragma once

#include "core/expected.h"
#include "game/network/game_inventory_replication.h"
#include "game/network/game_replication_services.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {

class GameServerPlayerState;
class IGameServerPlayerStateCheckpointSink;

class GameServerInventoryReplication final : public IGameReplicationValueSource {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerInventoryReplication>>
    create(GameServerPlayerState& player_state,
           IGameServerPlayerStateCheckpointSink* checkpoint_sink = nullptr);

    GameServerInventoryReplication(const GameServerInventoryReplication&) = delete;
    GameServerInventoryReplication& operator=(const GameServerInventoryReplication&) = delete;

    // Applies a client request only through GameServerPlayerState. Semantic
    // rejection is returned to that player as a compact replicated response,
    // while infrastructure failures remain Expected errors for the command
    // sink's low-frequency aggregate diagnostics.
    [[nodiscard]] snt::core::Expected<void> submit_slot_transfer(
        const GameAuthenticatedPeer& peer, const GameInventorySlotTransferCommand& command);

    // Cross-container gameplay services use these two operations to preserve
    // the same player-private revision and response ordering as a normal
    // inventory drag. They never expose the AccountState implementation.
    [[nodiscard]] snt::core::Expected<bool> matches_inventory_revision(
        const GameAuthenticatedPeer& peer, uint64_t expected_revision);
    [[nodiscard]] snt::core::Expected<void> record_command_response(
        const GameAuthenticatedPeer& peer, GameInventoryCommandKind kind,
        uint64_t request_id, GameInventorySlotTransferOutcome outcome,
        std::string rejection_reason = {});

    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context,
        GameReplicationValueCollectionPhase phase) override;
    void on_values_committed(const GameAuthenticatedPeer& peer,
                             GameReplicationValueCollectionPhase phase,
                             std::span<const GameReplicationValue> values) noexcept override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

private:
    struct AccountState {
        GamePlayerInventory inventory;
        uint64_t inventory_revision = 0;
        uint64_t response_revision = 0;
        GameInventoryCommandResponse response;
        bool initialized = false;
    };

    struct PendingValue {
        GamePlayerInventory inventory;
        uint64_t inventory_revision = 0;
        uint64_t response_revision = 0;
        GameInventoryCommandResponse response;
        GameReplicationValueCollectionPhase phase =
            GameReplicationValueCollectionPhase::kInitialSnapshot;
        std::vector<std::byte> payload;
    };

    struct ObserverState {
        std::string account_id;
        GamePlayerInventory inventory;
        uint64_t inventory_revision = 0;
        uint64_t response_revision = 0;
        bool initialized = false;
        std::vector<std::byte> last_payload;
        std::optional<PendingValue> pending;
    };

    GameServerInventoryReplication(GameServerPlayerState& player_state,
                                   IGameServerPlayerStateCheckpointSink* checkpoint_sink) noexcept;

    [[nodiscard]] snt::core::Expected<AccountState*> synchronize_account(
        const GameAuthenticatedPeer& peer);
    [[nodiscard]] snt::core::Expected<void> record_response(
        AccountState& state, GameInventoryCommandKind kind, uint64_t request_id,
        GameInventorySlotTransferOutcome outcome,
        std::string rejection_reason = {});
    [[nodiscard]] snt::core::Expected<GameReplicationValue> prepare_snapshot_value(
        const GameAuthenticatedPeer& peer, const AccountState& account,
        ObserverState& observer, GameReplicationValueCollectionPhase phase);
    [[nodiscard]] snt::core::Expected<GameReplicationValue> prepare_delta_value(
        const GameAuthenticatedPeer& peer, const AccountState& account,
        ObserverState& observer);

    GameServerPlayerState* player_state_ = nullptr;
    IGameServerPlayerStateCheckpointSink* checkpoint_sink_ = nullptr;
    std::map<std::string, AccountState, std::less<>> accounts_;
    std::map<snt::network::PeerId, ObserverState> observers_;
};

}  // namespace snt::game::replication
