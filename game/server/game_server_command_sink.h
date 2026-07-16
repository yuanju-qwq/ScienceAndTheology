// Dedicated-server authoritative gameplay command sink.
//
// This server-composition module translates typed game-network commands into
// deterministic, simulation-main-thread mutations. It deliberately depends
// on QuestRegistry rather than an ECS World or transport implementation: the
// network handler owns admission and the simulation session owns game state.

#pragma once

#include "game/network/game_replication_services.h"
#include "game/quest/quest_registry.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {

class IGameServerPlayerMovementInputSink;

class GameServerCommandSink final : public IGameReplicationCommandSink {
public:
    explicit GameServerCommandSink(
        QuestRegistry& quests,
        IGameServerPlayerMovementInputSink* player_movement = nullptr);

    GameServerCommandSink(const GameServerCommandSink&) = delete;
    GameServerCommandSink& operator=(const GameServerCommandSink&) = delete;

    // Called by GameServerReplicationHandler before gameplay fixed tick.
    // It validates protocol-level shape and records a value-owned command;
    // semantic application waits for apply_pending_commands().
    [[nodiscard]] snt::core::Expected<void> enqueue_client_command(
        const GameAuthenticatedPeer& peer, GameClientCommand command,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<void> enqueue_player_movement_input(
        const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

    // Called once on the simulation main thread after inbound replication and
    // before QuestRegistry::tick(). Commands are ordered by account id, peer,
    // and client sequence, so socket poll order cannot alter same-tick game
    // state. Gameplay denials are aggregate-logged rather than terminating a
    // healthy dedicated server connection.
    [[nodiscard]] snt::core::Expected<void> apply_pending_commands(uint64_t tick_index);

    [[nodiscard]] size_t pending_command_count() const noexcept {
        return pending_.size() + pending_movement_.size();
    }

private:
    struct PeerSequenceState {
        uint64_t last_sequence = 0;
        bool has_sequence = false;
    };

    struct PendingQuestAccept {
        std::string account_id;
        snt::network::PeerId peer = snt::network::kInvalidPeerId;
        uint64_t client_sequence = 0;
        std::string quest_id;
    };

    struct PendingMovementInput {
        GameAuthenticatedPeer peer;
        uint64_t client_sequence = 0;
        GamePlayerMovementInput input;
    };

    [[nodiscard]] snt::core::Expected<void> validate_and_advance_sequence(
        const GameAuthenticatedPeer& peer, uint64_t client_sequence);
    void record_gameplay_rejection(uint64_t tick_index, const PendingQuestAccept& command,
                                   const snt::core::Error& error) noexcept;

    QuestRegistry* quests_ = nullptr;
    IGameServerPlayerMovementInputSink* player_movement_ = nullptr;
    std::map<snt::network::PeerId, PeerSequenceState> sequences_;
    std::vector<PendingQuestAccept> pending_;
    std::map<snt::network::PeerId, PendingMovementInput> pending_movement_;
    uint64_t last_rejection_log_tick_ = 0;
    uint32_t suppressed_rejections_ = 0;
    bool has_rejection_log_tick_ = false;
};

}  // namespace snt::game::replication
