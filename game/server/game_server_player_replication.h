// Dedicated-server player AOI and snapshot source.
//
// This composition-owned service derives visible player presentation values
// from authoritative ECS state. It never accepts positions or equipment from
// clients, and it keeps one bounded delta baseline per authenticated observer.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/player/player_replication.h"
#include "game/server/game_server_player_state.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace snt::game::replication {

struct GameServerPlayerReplicationConfig {
    uint32_t horizontal_aoi_radius_blocks = 96;
    uint32_t vertical_aoi_radius_blocks = 48;
    uint32_t max_visible_players = 64;
};

class GameServerPlayerReplication final : public IGameReplicationInterestProvider,
                                          public IGameReplicationSnapshotSource {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerPlayerReplication>> create(
        GameServerPlayerState& player_state,
        GameServerPlayerReplicationConfig config = {},
        std::vector<IGameReplicationValueSource*> value_sources = {});

    GameServerPlayerReplication(const GameServerPlayerReplication&) = delete;
    GameServerPlayerReplication& operator=(const GameServerPlayerReplication&) = delete;

    [[nodiscard]] snt::core::Expected<GameReplicationInterest> compute_interest(
        const GameAuthenticatedPeer& peer,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationMessage>> build_initial_snapshot(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationMessage>> build_deltas(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override;
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

private:
    struct VisiblePlayer {
        snt::ecs::EntityGuid entity_guid;
        GameReplicatedPlayerState state;
    };

    struct PeerBaseline {
        uint64_t snapshot_id = 0;
        uint64_t next_delta_sequence = 1;
        std::map<uint64_t, GameReplicatedPlayerState> players;
        std::map<uint8_t, GameReplicationValue> values;
    };

    GameServerPlayerReplication(GameServerPlayerState& player_state,
                                GameServerPlayerReplicationConfig config,
                                std::vector<IGameReplicationValueSource*> value_sources);

    [[nodiscard]] snt::core::Expected<std::vector<VisiblePlayer>> visible_players(
        const GameReplicationInterest& interest, const GameReplicationBudget& budget) const;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) const;
    [[nodiscard]] static snt::core::Expected<GameReplicatedPlayerState> make_player_state(
        const GameServerPlayerSnapshot& snapshot);
    [[nodiscard]] static bool same_player_state(const GameReplicatedPlayerState& left,
                                                const GameReplicatedPlayerState& right) noexcept;
    [[nodiscard]] static bool same_replication_value(const GameReplicationValue& left,
                                                      const GameReplicationValue& right) noexcept;

    GameServerPlayerState* player_state_ = nullptr;
    GameServerPlayerReplicationConfig config_;
    std::vector<IGameReplicationValueSource*> value_sources_;
    uint64_t next_snapshot_id_ = 1;
    std::map<snt::network::PeerId, PeerBaseline> peer_baselines_;
};

}  // namespace snt::game::replication
