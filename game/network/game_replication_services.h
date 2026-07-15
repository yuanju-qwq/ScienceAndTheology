// Game-owned replication service contracts.
//
// This is the game boundary for player authentication, deterministic command
// intake, AOI calculation, and snapshot production. Implementations belong to
// game systems; snt_engine only provides transport and the fixed-tick
// replication phase around these calls.

#pragma once

#include "ecs/entity_guid.h"
#include "game/network/game_replication_protocol.h"
#include "voxel/data/voxel_chunk.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {

// A stable game account/profile identity plus its current transport session.
// identity is intentionally not an entt handle or a process-local EntityGuid;
// a concrete game service maps it to persistent player state. peer is set by
// GameServerReplicationHandler after authentication and is only valid for the
// current connection, allowing command sequence state to be discarded on
// disconnect or account takeover.
struct GameAuthenticatedPeer {
    snt::network::PeerId peer = snt::network::kInvalidPeerId;
    PlayerIdentity identity;
};

class IGamePeerAuthenticator {
public:
    virtual ~IGamePeerAuthenticator() = default;

    virtual snt::core::Expected<GameAuthenticatedPeer> authenticate(
        snt::network::PeerId peer, const GameLoginRequest& request,
        const snt::network::ReplicationTickContext& context) = 0;
    virtual void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                      std::string_view reason) noexcept = 0;
};

// Owns game-player lifecycle work that is deliberately outside transport
// admission and command parsing. Implementations run only on the simulation
// main thread: a dedicated server can load per-player state on first login,
// transfer an in-memory account during takeover, and persist it on departure
// without making fixed-tick systems perform filesystem I/O.
class IGamePlayerSessionLifecycle {
public:
    virtual ~IGamePlayerSessionLifecycle() = default;

    virtual snt::core::Expected<void> on_peer_authenticated(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) = 0;

    // Replaces one authenticated transport session with another for the same
    // stable account. This is distinct from disconnect + login: a lifecycle
    // implementation must retain authoritative in-memory state rather than
    // save and immediately reload it while the takeover tick is still active.
    virtual snt::core::Expected<void> on_peer_replaced(
        const GameAuthenticatedPeer& previous_peer,
        const GameAuthenticatedPeer& replacement_peer,
        const snt::network::ReplicationTickContext& context) = 0;

    virtual void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                      std::string_view reason) noexcept = 0;
};

// Called on the simulation main thread before gameplay fixed-tick systems.
// A concrete implementation validates game command ids and queues only
// deterministic mutations for the matching tick.
class IGameReplicationCommandSink {
public:
    virtual ~IGameReplicationCommandSink() = default;

    virtual snt::core::Expected<void> enqueue_client_command(
        const GameAuthenticatedPeer& peer, GameClientCommand command,
        const snt::network::ReplicationTickContext& context) = 0;

    // The handler invokes this before it discards an authenticated session.
    // Implementations cancel queued commands and discard per-peer sequence
    // state; persistent game progress remains keyed by peer.identity.account_id.
    virtual void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                      std::string_view reason) noexcept = 0;
};

// Current AOI identity uses the current game ChunkKey and stable entity Guid.
// When the planned Sector coordinate model replaces ChunkKey, this latest-only
// interface changes with it instead of carrying a legacy conversion layer.
struct GameReplicationInterest {
    std::vector<snt::voxel::ChunkKey> chunks;
    std::vector<snt::ecs::EntityGuid> entities;
    std::vector<snt::ecs::EntityGuid> detailed_machine_entities;
};

struct GameReplicationBudget {
    // The handler applies these caps per authenticated peer and outbound tick
    // after it validates each source value with the public codec. A zero cap
    // disables that category; sources must then withhold matching records.
    uint32_t max_reliable_bytes_per_tick = 256u * 1024u;
    uint32_t max_chunk_snapshots_per_tick = 2;
    uint32_t max_entity_snapshots_per_tick = 128;
    uint32_t max_block_deltas_per_tick = 1024;
};

class IGameReplicationInterestProvider {
public:
    virtual ~IGameReplicationInterestProvider() = default;

    virtual snt::core::Expected<GameReplicationInterest> compute_interest(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) = 0;
};

// Implementations produce only server-originated GameReplicationMessage
// values. GameServerReplicationHandler validates their kind and typed payload,
// applies GameReplicationBudget, and inserts a whole batch atomically before
// handing frames to snt_network. An empty initial result keeps the peer in the
// initial-snapshot phase; an empty delta result simply emits no update.
class IGameReplicationSnapshotSource {
public:
    virtual ~IGameReplicationSnapshotSource() = default;

    virtual snt::core::Expected<std::vector<GameReplicationMessage>> build_initial_snapshot(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) = 0;
    virtual snt::core::Expected<std::vector<GameReplicationMessage>> build_deltas(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) = 0;
};

}  // namespace snt::game::replication
