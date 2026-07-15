// Game-owned client replication session.
//
// This module owns the client-side login state machine and composes a game
// protocol handler with an injected snt_network transport. It intentionally
// has no SDL, Vulkan, Steamworks, or ECS World dependency. A graphical host
// drives it around its fixed tick, while a future Steamworks package can pass
// a SteamP2PReplicationTransport through create() without duplicating the
// authentication lifecycle.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_protocol.h"
#include "game/player/player_identity.h"
#include "network/replication.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace snt::network {
struct TcpUdpConnectConfig;
}  // namespace snt::network

namespace snt::game::replication {

enum class GameClientConnectionState : uint8_t {
    kTransportConnecting,
    kAwaitingLoginAccepted,
    kAuthenticated,
    kDisconnected,
    kStopped,
};

// This is an immutable input at session creation. The account id is used only
// to verify the server's accepted identity; it is never encoded into the
// login request. credential remains opaque and is not logged.
struct GameClientAuthentication {
    PlayerIdentity local_identity;
    std::vector<std::byte> credential;
};

// A value snapshot avoids exposing the handler's mutable state to the client
// host. Future presentation code may observe it without gaining a transport
// or World pointer.
struct GameClientReplicationStatus {
    GameClientConnectionState state = GameClientConnectionState::kStopped;
    std::optional<PlayerIdentity> authenticated_identity;
};

// Direct TCP+UDP assigns kServerPeerId to the remote server. Steam P2P uses
// its platform peer id instead, so the generic factory accepts that identity
// explicitly instead of assuming every transport has the direct-socket value.
struct GameClientReplicationSessionConfig {
    snt::network::PeerId server_peer = snt::network::kServerPeerId;
};

class GameClientReplicationSession final {
public:
    // Generic factory for direct sockets, Steam P2P, or a test transport.
    // The session takes ownership of transport and calls its shutdown method.
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameClientReplicationSession>>
    create(std::unique_ptr<snt::network::IReplicationTransport> transport,
           GameClientAuthentication authentication,
           GameClientReplicationSessionConfig config = {});

    // Current graphical-host convenience path. It preserves the generic
    // factory above so future Steamworks integration does not become a second
    // client authentication implementation.
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameClientReplicationSession>>
    connect_tcp_udp(snt::network::TcpUdpConnectConfig config,
                    GameClientAuthentication authentication);

    ~GameClientReplicationSession();

    GameClientReplicationSession(const GameClientReplicationSession&) = delete;
    GameClientReplicationSession& operator=(const GameClientReplicationSession&) = delete;

    // These calls run on the client simulation main thread. They mirror the
    // authoritative server's inbound -> simulation -> outbound ordering.
    [[nodiscard]] snt::core::Expected<void> poll_inbound(
        const snt::network::ReplicationTickContext& context);
    [[nodiscard]] snt::core::Expected<void> emit_outbound(
        const snt::network::ReplicationTickContext& context);

    // Command semantics remain game-owned. Callers use typed APIs such as
    // enqueue_quest_accept() for implemented commands; this generic boundary
    // remains available for a future typed command codec without exposing the
    // transport or handler state.
    [[nodiscard]] snt::core::Expected<void> enqueue_command(GameClientCommand command);
    [[nodiscard]] snt::core::Expected<void> enqueue_quest_accept(
        uint64_t client_sequence, GameQuestAcceptCommand command);

    [[nodiscard]] GameClientReplicationStatus status() const;
    void shutdown() noexcept;

private:
    struct Impl;

    explicit GameClientReplicationSession(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace snt::game::replication
