// Dedicated-server task-book replication source implementation.

#define SNT_LOG_CHANNEL "game.server_quest_book_replication"
#include "game/server/game_server_quest_book_replication.h"

#include "core/error.h"
#include "game/network/game_quest_book_replication.h"
#include "game/player/player_identity.h"
#include "game/quest/quest_registry.h"

#include <string>
#include <utility>

namespace snt::game::replication {
namespace {

[[nodiscard]] snt::core::Error invalid_state(std::string message) {
    return {snt::core::ErrorCode::kInvalidState, std::move(message)};
}

}  // namespace

snt::core::Expected<std::unique_ptr<GameServerQuestBookReplication>>
GameServerQuestBookReplication::create(const QuestRegistry& quests) {
    return std::unique_ptr<GameServerQuestBookReplication>(
        new GameServerQuestBookReplication(quests));
}

GameServerQuestBookReplication::GameServerQuestBookReplication(
    const QuestRegistry& quests) noexcept
    : quests_(&quests) {}

snt::core::Expected<std::vector<GameReplicationValue>>
GameServerQuestBookReplication::collect_values(
    const GameAuthenticatedPeer& peer, const GameReplicationInterest&,
    const GameReplicationBudget& budget, const snt::network::ReplicationTickContext&) {
    if (quests_ == nullptr) {
        return invalid_state("Task-book replication has no quest registry");
    }
    if (peer.peer == snt::network::kInvalidPeerId) {
        return invalid_state("Task-book replication requires an authenticated transport peer");
    }
    if (auto result = validate_player_identity(peer.identity); !result) {
        auto error = result.error();
        error.with_context("GameServerQuestBookReplication::collect_values(peer identity)");
        return error;
    }
    if (budget.max_reliable_bytes_per_tick == 0 || budget.max_value_snapshots_per_tick == 0) {
        return std::vector<GameReplicationValue>{};
    }

    QuestBookSnapshot snapshot{
        .account_id = peer.identity.account_id,
        .content_fingerprint = quests_->content_fingerprint(),
        .progress_revision = quests_->progress_revision(peer.identity.account_id),
        .progress = quests_->snapshot_progress(peer.identity.account_id),
    };
    auto payload = encode_game_quest_book_snapshot(snapshot);
    if (!payload) {
        auto error = payload.error();
        error.with_context("GameServerQuestBookReplication::collect_values(encode task book)");
        return error;
    }
    return std::vector<GameReplicationValue>{
        {
            .kind = GameReplicationValueKind::kQuestBook,
            .operation = GameReplicationValueOperation::kUpsert,
            .payload = std::move(*payload),
        },
    };
}

}  // namespace snt::game::replication
