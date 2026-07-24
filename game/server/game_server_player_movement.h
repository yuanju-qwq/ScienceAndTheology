// Dedicated-server authoritative player movement.
//
// This module owns the simulation of authenticated movement intent. Clients
// submit only bounded axes/look values; voxel collision, gravity, jump timing,
// stale-input handling, and the replicated block-grid position stay on the
// server main thread.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/player/player_motion_simulation.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace snt::voxel {
class ChunkRegistry;
}

namespace snt::game::replication {

class GameServerPlayerState;

struct GameServerPlayerMovementConfig {
    GamePlayerMotionConfig motion;
    bool missing_chunks_are_solid = true;
};

// Command intake depends only on this narrow boundary. It keeps protocol
// parsing independent from concrete player physics and lets later movement
// implementations replace the service without changing the replication
// handler or task-command admission contract.
class IGameServerPlayerMovementInputSink {
public:
    virtual ~IGameServerPlayerMovementInputSink() = default;

    virtual snt::core::Expected<void> enqueue_player_movement_input(
        const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
        const snt::network::ReplicationTickContext& context) = 0;
    virtual void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                                      std::string_view reason) noexcept = 0;
};

// Teleports such as respawn must discard continuous velocity and stale input
// before the next fixed tick. Death/respawn code uses this narrow lifecycle
// boundary instead of reaching into a concrete movement cache.
class IGameServerPlayerMotionReset {
public:
    virtual ~IGameServerPlayerMotionReset() = default;

    virtual snt::core::Expected<void> reset_player_motion(
        const GameAuthenticatedPeer& peer) = 0;
};

class GameServerPlayerMovement final : public IGameServerPlayerMovementInputSink,
                                       public IGameServerPlayerMotionReset,
                                       public IGameAuthoritativePlayerMotionSource {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerMovement>> create(
        GameServerPlayerState& player_state, const snt::voxel::ChunkRegistry& chunks,
        GameServerPlayerMovementConfig config = {});

    GameServerPlayerMovement(const GameServerPlayerMovement&) = delete;
    GameServerPlayerMovement& operator=(const GameServerPlayerMovement&) = delete;

    // Called after the network handler has authenticated the peer and the
    // command sink has accepted its shared client sequence. Later packets for
    // one peer replace earlier movement intent instead of accumulating work.
    [[nodiscard]] snt::core::Expected<void> enqueue_player_movement_input(
        const GameAuthenticatedPeer& peer, GamePlayerMovementInput input,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;
    [[nodiscard]] snt::core::Expected<void> reset_player_motion(
        const GameAuthenticatedPeer& peer) override;

    // Runs once before the simulation systems for each authoritative tick.
    // Its position writes feed the existing player AOI snapshot/delta source.
    [[nodiscard]] snt::core::Expected<void> tick(
        const snt::network::ReplicationTickContext& context);

    [[nodiscard]] std::optional<GamePlayerMotionState> motion_snapshot_for_player(
        snt::ecs::EntityGuid entity_guid) const override;

    [[nodiscard]] size_t active_motion_count() const noexcept { return motions_.size(); }

private:
    struct TimedInput {
        GamePlayerMovementInput input;
        uint64_t received_tick = 0;
    };

    GameServerPlayerMovement(GameServerPlayerState& player_state,
                             const snt::voxel::ChunkRegistry& chunks,
                             GameServerPlayerMovementConfig config);

    GameServerPlayerState* player_state_ = nullptr;
    const snt::voxel::ChunkRegistry* chunks_ = nullptr;
    GameServerPlayerMovementConfig config_;
    std::map<snt::network::PeerId, TimedInput> inputs_;
    std::map<uint64_t, GamePlayerMotionState> motions_;
    // Entity identity survives an in-process account takeover while peer ids
    // do not. Track the binding so a new UDP sequence space cannot inherit a
    // previous connection's acknowledgement or consumed jump edge.
    std::map<uint64_t, snt::network::PeerId> motion_peers_;
};

}  // namespace snt::game::replication
