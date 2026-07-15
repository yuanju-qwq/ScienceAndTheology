// Game-owned replication service contracts.
//
// This is a declaration-only boundary for future player authentication,
// deterministic command intake, AOI calculation, and snapshot production.
// Implementations belong to game systems; snt_engine only provides transport
// and the fixed-tick replication phase around these calls.

#pragma once

#include "ecs/entity_guid.h"
#include "game/network/game_replication_protocol.h"
#include "voxel/data/voxel_chunk.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace snt::game::replication {

// A stable game account/profile identity. It is intentionally not an entt
// handle or a process-local EntityGuid; a concrete game service maps it to
// persistent player state during authenticated session creation.
struct GameAuthenticatedPeer {
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

// Called on the simulation main thread before gameplay fixed-tick systems.
// A concrete implementation validates game command ids and queues only
// deterministic mutations for the matching tick.
class IGameReplicationCommandSink {
public:
    virtual ~IGameReplicationCommandSink() = default;

    virtual snt::core::Expected<void> enqueue_client_command(
        const GameAuthenticatedPeer& peer, GameClientCommand command,
        const snt::network::ReplicationTickContext& context) = 0;
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
// values. The future handler integration validates their kind/channel and
// applies GameReplicationBudget before handing frames to snt_network.
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
