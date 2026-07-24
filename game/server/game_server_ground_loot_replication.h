// Dedicated-server AOI source for durable chunk-owned ground loot.
//
// The source reads sidecar records directly and filters them by normal terrain
// chunks. It has no inventory or gameplay authority beyond presentation.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/server/game_server_ground_loot.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace snt::game::replication {

struct GameServerGroundLootReplicationConfig {
    uint32_t max_visible_loot = 1024;
};

class GameServerGroundLootReplication final
    : public IGameReplicationValueSource,
      public IGameServerGroundLootStateSink {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerGroundLootReplication>>
    create(const GameChunkSidecarRegistry& sidecars,
           GameServerGroundLootReplicationConfig config = {});

    GameServerGroundLootReplication(const GameServerGroundLootReplication&) = delete;
    GameServerGroundLootReplication& operator=(const GameServerGroundLootReplication&) = delete;

    void on_ground_loot_state_changed(uint64_t source_tick) noexcept override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context,
        GameReplicationValueCollectionPhase phase) override;

private:
    GameServerGroundLootReplication(const GameChunkSidecarRegistry& sidecars,
                                    GameServerGroundLootReplicationConfig config) noexcept;

    const GameChunkSidecarRegistry* sidecars_ = nullptr;
    GameServerGroundLootReplicationConfig config_;
    uint64_t latest_source_tick_ = 0;
};

}  // namespace snt::game::replication
