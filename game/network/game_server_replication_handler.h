// Authoritative game replication handler for the dedicated-server host.
//
// Ownership: this game module interprets game payloads and owns per-peer
// admission state. It is invoked only by ReplicationService on the simulation
// main thread, so authenticated command intake can remain deterministic.

#pragma once

#include "game/network/game_replication_services.h"
#include "network/replication.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace snt::game::replication {

// The dedicated server installs this until the game supplies a real account,
// Steam, or LAN authenticator. It keeps a transport-enabled server from
// accidentally admitting unauthenticated players.
class ClosedGamePeerAuthenticator final : public IGamePeerAuthenticator {
public:
    snt::core::Expected<GameAuthenticatedPeer> authenticate(
        snt::network::PeerId peer, const GameLoginRequest& request,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;
};

class GameServerReplicationHandler final : public snt::network::IReplicationHandler {
public:
    explicit GameServerReplicationHandler(IGamePeerAuthenticator& authenticator,
                                          IGameReplicationCommandSink* command_sink = nullptr);
    ~GameServerReplicationHandler() override = default;

    GameServerReplicationHandler(const GameServerReplicationHandler&) = delete;
    GameServerReplicationHandler& operator=(const GameServerReplicationHandler&) = delete;

    snt::core::Expected<void> on_peer_connected(
        snt::network::PeerId peer, const snt::network::ReplicationTickContext& context) override;
    snt::core::Expected<void> on_frame(
        snt::network::PeerId peer, const snt::network::ReplicationFrame& frame,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(snt::network::PeerId peer, std::string_view reason) noexcept override;
    snt::core::Expected<void> emit_outbound(
        const snt::network::ReplicationTickContext& context,
        snt::network::IReplicationFrameSink& sink) override;

private:
    struct PeerState {
        std::optional<GameAuthenticatedPeer> authenticated_peer;
    };

    struct PendingOutboundFrame {
        snt::network::PeerId peer = snt::network::kInvalidPeerId;
        snt::network::ReplicationFrame frame;
    };

    snt::core::Expected<void> handle_login(
        snt::network::PeerId peer, PeerState& state,
        const GameReplicationMessage& message,
        const snt::network::ReplicationTickContext& context);
    snt::core::Expected<void> handle_command(
        snt::network::PeerId peer, const PeerState& state,
        const GameReplicationMessage& message,
        const snt::network::ReplicationTickContext& context);
    [[nodiscard]] bool is_player_id_in_use(std::string_view player_id,
                                            snt::network::PeerId except_peer) const;

    IGamePeerAuthenticator* authenticator_ = nullptr;
    IGameReplicationCommandSink* command_sink_ = nullptr;
    std::unordered_map<snt::network::PeerId, PeerState> peers_;
    std::vector<PendingOutboundFrame> pending_outbound_;
};

}  // namespace snt::game::replication
