// Dedicated-server automation-controller replication source.
//
// The simulation-owned runtime provides active, AOI-indexed controller
// presentation. This adapter only encodes that value set for the outer player
// baseline; it owns no sidecars, executors, storage, or editor authority.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"

#include <memory>
#include <vector>

namespace snt::game {
class AutomationControllerRuntimeService;
}

namespace snt::game::replication {

class GameServerAutomationControllerReplication final : public IGameReplicationValueSource {
public:
    [[nodiscard]] static snt::core::Expected<
        std::unique_ptr<GameServerAutomationControllerReplication>>
    create(const AutomationControllerRuntimeService& runtime);

    GameServerAutomationControllerReplication(
        const GameServerAutomationControllerReplication&) = delete;
    GameServerAutomationControllerReplication& operator=(
        const GameServerAutomationControllerReplication&) = delete;

    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context,
        GameReplicationValueCollectionPhase phase) override;

private:
    explicit GameServerAutomationControllerReplication(
        const AutomationControllerRuntimeService& runtime) noexcept;

    const AutomationControllerRuntimeService* runtime_ = nullptr;
};

}  // namespace snt::game::replication
