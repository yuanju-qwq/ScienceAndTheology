// Task-book replication codec, client cache, and server source tests.

#include "game/client/game_content_registry.h"
#include "game/network/game_quest_book_replication.h"
#include "game/player/player_identity.h"
#include "game/quest/quest_registry.h"
#include "game/server/game_server_quest_book_replication.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace {

using snt::game::GameContentRegistry;
using snt::game::PlayerIdentity;
using snt::game::QuestBookSnapshot;
using snt::game::QuestProgressRecord;
using snt::game::QuestRegistry;
using snt::game::QuestState;
using snt::game::make_local_name_player_identity;
using snt::game::replication::GameAuthenticatedPeer;
using snt::game::replication::GameClientQuestBookState;
using snt::game::replication::GameDelta;
using snt::game::replication::GameReplicationBudget;
using snt::game::replication::GameReplicationInterest;
using snt::game::replication::GameReplicationValueKind;
using snt::game::replication::GameReplicationValueOperation;
using snt::game::replication::GameServerQuestBookReplication;
using snt::game::replication::GameSnapshot;

PlayerIdentity make_local_identity(std::string name) {
    auto identity = make_local_name_player_identity(std::move(name));
    return identity ? std::move(*identity) : PlayerIdentity{};
}

QuestBookSnapshot make_snapshot(std::string account_id, uint64_t revision, int32_t progress) {
    QuestProgressRecord record;
    record.quest_id = "p7.task_book.smelt";
    record.state = QuestState::kInProgress;
    record.objective_counts.emplace("smelt_iron", progress);
    record.completed_tick = 12;
    record.completion_count = 2;
    return {
        .account_id = std::move(account_id),
        .content_fingerprint = 0x7a38d95b4c12e6f1ull,
        .progress_revision = revision,
        .progress = {std::move(record)},
    };
}

TEST(GameQuestBookReplicationTest, RoundTripsValidatedSnapshotAndGatesClientRevisions) {
    const QuestBookSnapshot initial_snapshot = make_snapshot("local-name:TaskBook", 4, 2);
    auto encoded_initial = snt::game::replication::encode_game_quest_book_snapshot(initial_snapshot);
    ASSERT_TRUE(encoded_initial) << encoded_initial.error().format();
    auto decoded_initial = snt::game::replication::decode_game_quest_book_snapshot(*encoded_initial);
    ASSERT_TRUE(decoded_initial) << decoded_initial.error().format();
    EXPECT_EQ(decoded_initial->account_id, initial_snapshot.account_id);
    EXPECT_EQ(decoded_initial->content_fingerprint, initial_snapshot.content_fingerprint);
    EXPECT_EQ(decoded_initial->progress_revision, 4u);
    ASSERT_EQ(decoded_initial->progress.size(), 1u);
    EXPECT_EQ(decoded_initial->progress.front().quest_id, "p7.task_book.smelt");
    EXPECT_EQ(decoded_initial->progress.front().objective_counts.at("smelt_iron"), 2);

    auto trailing = *encoded_initial;
    trailing.push_back(std::byte{0});
    EXPECT_FALSE(snt::game::replication::decode_game_quest_book_snapshot(trailing));

    GameClientQuestBookState client_state(initial_snapshot.account_id);
    GameSnapshot initial;
    initial.snapshot_id = 41;
    initial.values.push_back({
        .kind = GameReplicationValueKind::kQuestBook,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = *encoded_initial,
    });
    ASSERT_TRUE(client_state.apply(initial));
    ASSERT_NE(client_state.snapshot(), nullptr);
    EXPECT_EQ(client_state.snapshot()->progress_revision, 4u);

    GameDelta stale;
    stale.base_snapshot_id = 41;
    stale.sequence = 1;
    stale.values.push_back({
        .kind = GameReplicationValueKind::kQuestBook,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = *encoded_initial,
    });
    EXPECT_FALSE(client_state.apply(stale));

    const QuestBookSnapshot progressed_snapshot = make_snapshot(initial_snapshot.account_id, 5, 3);
    auto encoded_progressed = snt::game::replication::encode_game_quest_book_snapshot(progressed_snapshot);
    ASSERT_TRUE(encoded_progressed) << encoded_progressed.error().format();
    GameDelta progressed;
    progressed.base_snapshot_id = 41;
    progressed.sequence = 1;
    progressed.values.push_back({
        .kind = GameReplicationValueKind::kQuestBook,
        .operation = GameReplicationValueOperation::kUpsert,
        .payload = std::move(*encoded_progressed),
    });
    ASSERT_TRUE(client_state.apply(progressed));
    ASSERT_NE(client_state.snapshot(), nullptr);
    EXPECT_EQ(client_state.snapshot()->progress_revision, 5u);
    EXPECT_EQ(client_state.snapshot()->progress.front().objective_counts.at("smelt_iron"), 3);

    GameDelta remove;
    remove.base_snapshot_id = 41;
    remove.sequence = 2;
    remove.values.push_back({
        .kind = GameReplicationValueKind::kQuestBook,
        .operation = GameReplicationValueOperation::kRemove,
    });
    ASSERT_TRUE(client_state.apply(remove));
    EXPECT_EQ(client_state.snapshot(), nullptr);
}

TEST(GameServerQuestBookReplicationTest, EmitsOnlyTheAuthenticatedAccountsSnapshot) {
    GameContentRegistry content;
    QuestRegistry quests(content);
    const PlayerIdentity identity = make_local_identity("ReplicatedQuestPlayer");
    ASSERT_FALSE(identity.account_id.empty());
    ASSERT_TRUE(quests.restore_progress(
        identity.account_id, make_snapshot(identity.account_id, 0, 7).progress));

    auto source = GameServerQuestBookReplication::create(quests);
    ASSERT_TRUE(source) << source.error().format();
    const GameAuthenticatedPeer peer{.peer = 17, .identity = identity};
    const GameReplicationBudget budget{
        .max_reliable_bytes_per_tick = 4096,
        .max_value_snapshots_per_tick = 1,
    };
    const snt::network::ReplicationTickContext context{.tick_index = 8, .delta_seconds = 0.05f};
    auto values = (*source)->collect_values(
        peer, GameReplicationInterest{}, budget, context,
        snt::game::replication::GameReplicationValueCollectionPhase::kInitialSnapshot);
    ASSERT_TRUE(values) << values.error().format();
    ASSERT_EQ(values->size(), 1u);
    EXPECT_EQ(values->front().kind, GameReplicationValueKind::kQuestBook);
    EXPECT_EQ(values->front().operation, GameReplicationValueOperation::kUpsert);
    auto decoded = snt::game::replication::decode_game_quest_book_snapshot(values->front().payload);
    ASSERT_TRUE(decoded) << decoded.error().format();
    EXPECT_EQ(decoded->account_id, identity.account_id);
    ASSERT_EQ(decoded->progress.size(), 1u);
    EXPECT_EQ(decoded->progress.front().objective_counts.at("smelt_iron"), 7);

    GameAuthenticatedPeer unauthenticated = peer;
    unauthenticated.peer = snt::network::kInvalidPeerId;
    EXPECT_FALSE((*source)->collect_values(
        unauthenticated, GameReplicationInterest{}, budget, context,
        snt::game::replication::GameReplicationValueCollectionPhase::kInitialSnapshot));
}

}  // namespace
