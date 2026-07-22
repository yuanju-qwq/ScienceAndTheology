// Dedicated-server AE physical-topology replication source.
//
// The game simulation owns active topology and this source only projects a
// bounded, AOI-filtered value snapshot. It never owns a sidecar, a storage
// cell, or an AE runtime handle.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace snt::game {
class AeNetworkRuntimeService;
}

namespace snt::game::replication {

class GameServerAeNetworkReplication final : public IGameReplicationValueSource {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerAeNetworkReplication>>
    create(const AeNetworkRuntimeService& runtime);

    GameServerAeNetworkReplication(const GameServerAeNetworkReplication&) = delete;
    GameServerAeNetworkReplication& operator=(const GameServerAeNetworkReplication&) = delete;

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

    explicit GameServerAeNetworkReplication(const AeNetworkRuntimeService& runtime) noexcept;
    void update_omission_log(uint64_t peer_id, size_t candidate_count,
                             size_t omitted_count, size_t payload_limit,
                             GameReplicationValueCollectionPhase phase) noexcept;

    const AeNetworkRuntimeService* runtime_ = nullptr;
    std::unordered_map<uint64_t, OmissionLogState> omission_log_states_;
};

}  // namespace snt::game::replication
