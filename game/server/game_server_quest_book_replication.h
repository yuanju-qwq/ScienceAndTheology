// Dedicated-server task-book replication source.
//
// QuestRegistry remains authoritative and main-thread-owned. This adapter
// exposes one validated presentation value for the authenticated account;
// GameServerPlayerReplication owns the peer baseline and outbound budget.

#pragma once

#include "core/expected.h"
#include "game/network/game_replication_services.h"

#include <memory>
#include <vector>

namespace snt::game {
class QuestRegistry;
}

namespace snt::game::replication {

class GameServerQuestBookReplication final : public IGameReplicationValueSource {
public:
    [[nodiscard]] static snt::core::Expected<std::unique_ptr<GameServerQuestBookReplication>>
    create(const QuestRegistry& quests);

    GameServerQuestBookReplication(const GameServerQuestBookReplication&) = delete;
    GameServerQuestBookReplication& operator=(const GameServerQuestBookReplication&) = delete;

    [[nodiscard]] snt::core::Expected<std::vector<GameReplicationValue>> collect_values(
        const GameAuthenticatedPeer& peer, const GameReplicationInterest& interest,
        const GameReplicationBudget& budget,
        const snt::network::ReplicationTickContext& context) override;

private:
    explicit GameServerQuestBookReplication(const QuestRegistry& quests) noexcept;

    const QuestRegistry* quests_ = nullptr;
};

}  // namespace snt::game::replication
