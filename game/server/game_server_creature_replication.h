// Dedicated-server creature presentation replication source.
//
// The server records presentation events from the authoritative wildlife
// system, then exposes only the current subset within the observer's chunk
// AOI. It does not create gameplay agents for far visuals.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"
#include "game/world/defs/creature_presentation.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace snt::game::replication {

struct GameServerCreaturePresentationReplicationConfig {
    uint32_t max_visible_creatures = 1024;
};

class GameServerCreaturePresentationReplication final
    : public IGameReplicationValueSource,
      public IGameCreaturePresentationSink {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerCreaturePresentationReplication>>
    create(GameServerCreaturePresentationReplicationConfig config = {});

    GameServerCreaturePresentationReplication(
        const GameServerCreaturePresentationReplication&) = delete;
    GameServerCreaturePresentationReplication& operator=(
        const GameServerCreaturePresentationReplication&) = delete;

    void on_creature_presentation_event(
        const GameCreaturePresentationEvent& event) override;
    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context,
        GameReplicationValueCollectionPhase phase) override;

    [[nodiscard]] size_t tracked_creature_count() const noexcept { return creatures_.size(); }

private:
    explicit GameServerCreaturePresentationReplication(
        GameServerCreaturePresentationReplicationConfig config) noexcept;

    GameServerCreaturePresentationReplicationConfig config_;
    std::map<uint64_t, GameCreaturePresentationState> creatures_;
    uint64_t latest_source_tick_ = 0;
};

}  // namespace snt::game::replication
