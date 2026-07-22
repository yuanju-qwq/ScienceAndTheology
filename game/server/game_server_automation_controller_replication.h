// Dedicated-server automation-controller replication source.
//
// The simulation-owned runtime provides active, AOI-indexed controller
// presentation. This adapter only encodes that value set for the outer player
// baseline; it owns no sidecars, executors, storage, or editor authority.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
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
    void on_peer_disconnected(const GameAuthenticatedPeer& peer,
                              std::string_view reason) noexcept override;

private:
    struct OmissionLogState {
        size_t candidate_count = 0;
        size_t omitted_count = 0;
        size_t payload_limit = 0;
    };

    explicit GameServerAutomationControllerReplication(
        const AutomationControllerRuntimeService& runtime) noexcept;
    void update_omission_log(uint64_t peer_id, size_t candidate_count,
                             size_t omitted_count, size_t payload_limit,
                             GameReplicationValueCollectionPhase phase) noexcept;

    const AutomationControllerRuntimeService* runtime_ = nullptr;
    // Logging is state-based per peer, never once per replication tick.
    std::unordered_map<uint64_t, OmissionLogState> omission_log_states_;
};

}  // namespace snt::game::replication
